#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/list.h>
#include <linux/sched.h>
#include <linux/pid.h>
#include <linux/sched/signal.h>
#include <linux/errno.h>
#include <asm/ptrace.h>

#include "ftracehooking.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("2023202062");
MODULE_DESCRIPTION("Assignment 2-3: ftrace controller (ftracehooking)");

#define STUDENT_ID "2023202062"
typedef asmlinkage long (*sys_call_t)(const struct pt_regs *); // syscall typedef

#define NR_OS_FTRACE 336  // syscall number for os_ftrace

// Function pointers for changing memory page permissions
typedef int (*fn_setmem_t)(unsigned long addr, int numpages);
static fn_setmem_t p_set_memory_rw = NULL;
static fn_setmem_t p_set_memory_ro = NULL;

// Syscall table pointer and original os_ftrace pointer
static sys_call_t *syscall_table = NULL;
static sys_call_t real_os_ftrace = NULL;

// Exported global state
pid_t ftrace_target_pid = -1;
bool ftrace_trace_active = false;

// Linked list for per-file statistics and its lock
static LIST_HEAD(stats_list);
static spinlock_t stats_lock;

// Last opened file name (duplicated string, protected by lock)
static char *last_opened_name = NULL;

// Allocate and initialize new stat node for a file
static struct ftrace_file_stat *alloc_file_stat(const char *name)
{
    struct ftrace_file_stat *s;

    s = kmalloc(sizeof(*s), GFP_KERNEL);
    if (!s)
        return NULL;

    memset(s, 0, sizeof(*s));
    strlcpy(s->filename, name ? name : "<unknown>", FTRACE_MAX_PATH);
    INIT_LIST_HEAD(&s->list);
    return s;
}

// Find existing stat entry for filename (lock must be held)
static struct ftrace_file_stat *find_file_stat_locked(const char *name)
{
    struct ftrace_file_stat *ent;
    list_for_each_entry(ent, &stats_list, list) {
        if (strcmp(ent->filename, name) == 0)
            return ent;
    }
    return NULL;
}

// Exported function: update statistics for file operations
void ftrace_add_update(const char *name, enum ftrace_op op, ssize_t bytes)
{
    unsigned long flags;
    struct ftrace_file_stat *ent;

    if (!ftrace_trace_active)
        return;

    if (!name)
        name = "<unknown>";

    spin_lock_irqsave(&stats_lock, flags);

    // Lookup or create stat entry
    ent = find_file_stat_locked(name);
    if (!ent) {
        ent = alloc_file_stat(name);
        if (ent)
            list_add_tail(&ent->list, &stats_list);
    }

    // Update counters based on operation type
    if (ent) {
        switch (op) {
        case FTRACE_OP_OPEN:
            ent->open_count++;
            break;
        case FTRACE_OP_CLOSE:
            ent->close_count++;
            break;
        case FTRACE_OP_READ:
            ent->read_count++;
            if (bytes > 0)
                ent->read_bytes += bytes;
            break;
        case FTRACE_OP_WRITE:
            ent->write_count++;
            if (bytes > 0)
                ent->write_bytes += bytes;
            break;
        case FTRACE_OP_LSEEK:
            ent->lseek_count++;
            break;
        default:
            break;
        }
    }

    spin_unlock_irqrestore(&stats_lock, flags);
}
EXPORT_SYMBOL(ftrace_add_update);

// Exported function: record the last opened filename
void ftrace_register_last_open(const char *name)
{
    char *tmp;

    if (!ftrace_trace_active || !name)
        return;

    tmp = kstrdup(name, GFP_KERNEL);
    if (!tmp)
        return;

    spin_lock(&stats_lock);
    kfree(last_opened_name);
    last_opened_name = tmp;
    spin_unlock(&stats_lock);
}
EXPORT_SYMBOL(ftrace_register_last_open);

// Print collected statistics and reset state
void ftrace_print_summary_and_clear(void)
{
    unsigned long flags;
    struct ftrace_file_stat *ent, *tmp;
    struct task_struct *task;
    char comm[TASK_COMM_LEN];
    pid_t pid;

    if (ftrace_target_pid == -1 && list_empty(&stats_list))
        return;

    pid = ftrace_target_pid;

    // Get process name for given PID
    rcu_read_lock();
    task = (pid != -1) ? pid_task(find_vpid(pid), PIDTYPE_PID) : NULL;
    if (task)
        get_task_comm(comm, task);
    else
        strlcpy(comm, "<unknown>", TASK_COMM_LEN);
    rcu_read_unlock();

    // Print start header
    printk(KERN_INFO "OS Assignment 2 ftrace [%d] Start\n", pid);

    // Print stats for each file
    spin_lock_irqsave(&stats_lock, flags);
    list_for_each_entry(ent, &stats_list, list) {
        unsigned long total_ops = ent->open_count + ent->close_count +
                                  ent->read_count + ent->write_count +
                                  ent->lseek_count;

        printk(KERN_INFO "[%s] %s file[%s] stats [%lu] read - %lu / written - %lu\n",
               STUDENT_ID, comm, ent->filename, total_ops,
               ent->read_bytes, ent->write_bytes);

        printk(KERN_INFO "open[%lu] close[%lu] read[%lu] write[%lu] lseek[%lu]\n",
               ent->open_count, ent->close_count,
               ent->read_count, ent->write_count, ent->lseek_count);
    }

    // Free allocated nodes
    list_for_each_entry_safe(ent, tmp, &stats_list, list) {
        list_del(&ent->list);
        kfree(ent);
    }

    kfree(last_opened_name);
    last_opened_name = NULL;

    ftrace_trace_active = false;
    ftrace_target_pid = -1;

    spin_unlock_irqrestore(&stats_lock, flags);

    // Print end footer
    printk(KERN_INFO "OS Assignment 2 ftrace [%d] End\n", pid);
}
EXPORT_SYMBOL(ftrace_print_summary_and_clear);

// Custom os_ftrace replacement
// pid != 0: start tracing
// pid == 0: stop tracing and print summary
static asmlinkage long my_ftrace(const struct pt_regs *regs)
{
    pid_t pid = (pid_t) regs->di;

    if (pid != 0) {
        unsigned long flags;

        spin_lock_irqsave(&stats_lock, flags);

        // Clear existing stats before new trace
        {
            struct ftrace_file_stat *ent, *tmp;
            list_for_each_entry_safe(ent, tmp, &stats_list, list) {
                list_del(&ent->list);
                kfree(ent);
            }
            kfree(last_opened_name);
            last_opened_name = NULL;
        }

        ftrace_target_pid = pid;
        ftrace_trace_active = true;

        spin_unlock_irqrestore(&stats_lock, flags);

    } else {
        ftrace_print_summary_and_clear(); // Stop trace and print summary
    }

    return 0; // syscall return value
}

// Try to set page writable
static int try_set_memory_rw(void *addr)
{
    unsigned long p = (unsigned long)addr & PAGE_MASK;

    if (p_set_memory_rw) {
        int r = p_set_memory_rw(p, 1);
        if (r)
            pr_warn("ftrace: set_memory_rw(%px) failed: %d\n", (void *)p, r);
        return r;
    }
    pr_err("ftrace: set_memory_rw not available\n");
    return -ENOSYS;
}

// Try to set page read-only
static int try_set_memory_ro(void *addr)
{
    unsigned long p = (unsigned long)addr & PAGE_MASK;

    if (p_set_memory_ro) {
        int r = p_set_memory_ro(p, 1);
        if (r)
            pr_warn("ftrace: set_memory_ro(%px) failed: %d\n", (void *)p, r);
        return r;
    }
    pr_err("ftrace: set_memory_ro not available\n");
    return -ENOSYS;
}

// Module initialization: install my_ftrace
static int __init ftracehook_init(void)
{
    int rc = 0;

    spin_lock_init(&stats_lock);

    pr_info("ftracehooking: init\n");

    // Lookup syscall_table
    syscall_table = (sys_call_t *)kallsyms_lookup_name("sys_call_table");
    if (!syscall_table)
        syscall_table = (sys_call_t *)kallsyms_lookup_name("__x64_sys_call_table");
    if (!syscall_table)
        return -ENOENT;

    // Resolve memory helpers
    p_set_memory_rw = (fn_setmem_t)kallsyms_lookup_name("set_memory_rw");
    p_set_memory_ro = (fn_setmem_t)kallsyms_lookup_name("set_memory_ro");

    if (!p_set_memory_rw || !p_set_memory_ro)
        return -ENOSYS;

    // Allow write access
    rc = try_set_memory_rw(syscall_table);
    if (rc)
        return rc;

    // Save original os_ftrace
    real_os_ftrace = syscall_table[NR_OS_FTRACE];
    if (!real_os_ftrace) {
        try_set_memory_ro(syscall_table);
        return -EINVAL;
    }

    // Install my_ftrace
    syscall_table[NR_OS_FTRACE] = (sys_call_t) my_ftrace;
    smp_wmb();

    // Restore read-only protection
    try_set_memory_ro(syscall_table);

    pr_info("ftracehooking: installed my_ftrace at slot %d\n", NR_OS_FTRACE);
    return 0;
}

// Module cleanup: restore os_ftrace
static void __exit ftracehook_exit(void)
{
    pr_info("ftracehooking: exit\n");

    if (syscall_table && real_os_ftrace) {
        if (!try_set_memory_rw(syscall_table)) {
            syscall_table[NR_OS_FTRACE] = real_os_ftrace;
            smp_wmb();
            try_set_memory_ro(syscall_table);
            pr_info("ftracehooking: restored os_ftrace\n");
        }
    }

    // Print summary if still active
    if (ftrace_trace_active || !list_empty(&stats_list))
        ftrace_print_summary_and_clear();
}

module_init(ftracehook_init);
module_exit(ftracehook_exit);

EXPORT_SYMBOL(ftrace_target_pid);
EXPORT_SYMBOL(ftrace_trace_active);

