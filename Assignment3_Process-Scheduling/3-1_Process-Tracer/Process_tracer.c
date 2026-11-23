#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/types.h>
#include <linux/errno.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/cred.h>
#include <linux/kallsyms.h>
#include <linux/spinlock.h>
#include <linux/ptrace.h>   // for struct pt_regs

MODULE_LICENSE("GPL");
MODULE_AUTHOR("2023202062");
MODULE_DESCRIPTION("Assignment 3-1: process_tracer for syscall #336");

/* state -> human readable */
static const char *state_to_text(const struct task_struct *p)
{
    if (READ_ONCE(p->exit_state) & EXIT_ZOMBIE) return "Zombie process";
    if (READ_ONCE(p->exit_state) & EXIT_DEAD)   return "Dead";

    {
        long s = READ_ONCE(p->state);
        if (s == TASK_RUNNING || s == 0)              return "Running or ready";
        if (s & TASK_INTERRUPTIBLE)                   return "Wait";
        if (s & TASK_UNINTERRUPTIBLE)                 return "Wait with ignoring all signals";
    #ifdef __TASK_STOPPED
        if (s & __TASK_STOPPED)                       return "Stopped";
    #elif defined(TASK_STOPPED)
        if (s & TASK_STOPPED)                         return "Stopped";
    #endif
    }
    return "etc.";
}


/* print process info for given pid */
static int dump_task_info(pid_t target_pid)
{
    struct task_struct *task, *iter;
    struct task_struct *parent;
    unsigned long ctx_switches;
    int sibling_cnt = 0;
    int child_cnt = 0;

    rcu_read_lock();

    /* find task_struct by pid */
    task = NULL;
    for_each_process(iter) {
        if (iter->pid == target_pid) {
            task = iter;
            break;
        }
    }

    if (!task) {
        rcu_read_unlock();
        pr_info("[process_tracer] PID %d not found\n", target_pid);
        return -1;
    }

    parent = task->real_parent;
    ctx_switches = task->nvcsw + task->nivcsw;

    pr_info("=== Process Trace Start ===\n");

    pr_info("(1) Name                  : %s\n", task->comm);

    pr_info("(2) State                 : %s\n",
            state_to_text(task));

    pr_info("(3) Group (PGID)          : %d (%s)\n",
            task->tgid, task->comm);

    pr_info("(4) Context Switches      : %lu\n", ctx_switches);

    pr_info("(5) Fork Count            : %d\n", task->fork_count);

    if (parent)
        pr_info("(6) Parent                : PID %d / %s\n",
                parent->pid, parent->comm);
    else
        pr_info("(6) Parent                : (none)\n");

    {
        struct task_struct *sib;
        int sibling_cnt_local = 0;
        pr_info("(7) Siblings:\n");
        if (parent) {
            list_for_each_entry(sib, &parent->children, sibling) {
                if (sib->pid == task->pid)
                    continue;
                pr_info("    - PID %d / %s\n", sib->pid, sib->comm);
                sibling_cnt_local++;
            }
            pr_info("    Total siblings        : %d\n", sibling_cnt_local);
        } else {
            pr_info("    (no parent)\n");
            pr_info("    Total siblings        : 0\n");
        }
    }

    {
        struct task_struct *child;
        int child_cnt_local = 0;
        pr_info("(8) Children:\n");
        list_for_each_entry(child, &task->children, sibling) {
            pr_info("    - PID %d / %s\n", child->pid, child->comm);
            child_cnt_local++;
        }
        if (child_cnt_local == 0)
            pr_info("    (no child process)\n");
        pr_info("    Total children        : %d\n", child_cnt_local);
    }

    pr_info("=== Process Trace End ===\n");

    rcu_read_unlock();
    return 0;
}

/* our replacement for syscall #336
 * NOTE: this kernel expects pt_regs-style ABI for syscalls
 */
asmlinkage long process_tracer(const struct pt_regs *regs)
{
    pid_t trace_task;
    int ret;

    /* first arg passed in rdi on x86_64 → regs->di */
    trace_task = (pid_t)regs->di;

    if (trace_task <= 0) {
        pr_info("[process_tracer] invalid pid %d\n", trace_task);
        return -1;
    }

    ret = dump_task_info(trace_task);
    if (ret < 0)
        return -1;

    return trace_task;
}

/* ---------- syscall hook machinery ---------- */

static unsigned long **sys_call_table;

/* backup of original syscall #336 */
static asmlinkage long (*orig_syscall_336)(const struct pt_regs *);

/* disable CR0 write-protect */
static void disable_wp(void)
{
    unsigned long cr0 = read_cr0();
    clear_bit(16, &cr0);
    write_cr0(cr0);
}

/* enable CR0 write-protect */
static void enable_wp(void)
{
    unsigned long cr0 = read_cr0();
    set_bit(16, &cr0);
    write_cr0(cr0);
}

/* overwrite sys_call_table[nr] with new_addr, save original */
static void hook_syscall(unsigned int nr,
                         unsigned long *new_addr,
                         unsigned long **table,
                         unsigned long **backup_slot)
{
    disable_wp();
    if (backup_slot)
        *backup_slot = (unsigned long *)table[nr];
    table[nr] = new_addr;
    enable_wp();
}

/* restore sys_call_table[nr] from backup */
static void unhook_syscall(unsigned int nr,
                           unsigned long **table,
                           unsigned long **backup_slot)
{
    disable_wp();
    if (backup_slot && *backup_slot)
        table[nr] = *backup_slot;
    enable_wp();
}

static int __init process_tracer_init(void)
{
    pr_info("[process_tracer] load\n");

    sys_call_table = (unsigned long **)kallsyms_lookup_name("sys_call_table");
    if (!sys_call_table) {
        pr_alert("[process_tracer] sys_call_table not found\n");
        return -1;
    }

    hook_syscall(336,
                 (unsigned long *)process_tracer,
                 sys_call_table,
                 (unsigned long **)&orig_syscall_336);

    pr_info("[process_tracer] hooked syscall 336\n");
    return 0;
}

static void __exit process_tracer_exit(void)
{
    pr_info("[process_tracer] unload\n");

    if (sys_call_table) {
        unhook_syscall(336,
                       sys_call_table,
                       (unsigned long **)&orig_syscall_336);
        pr_info("[process_tracer] restored syscall 336\n");
    }
}

module_init(process_tracer_init);
module_exit(process_tracer_exit);

