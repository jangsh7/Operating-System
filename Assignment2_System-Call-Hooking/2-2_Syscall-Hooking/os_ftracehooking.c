#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <asm/ptrace.h>
#include <linux/smp.h>
#include <linux/irqflags.h>
#include <linux/stop_machine.h>
#include <linux/types.h>
#include <linux/errno.h>

#ifdef CONFIG_X86
#include <asm/processor.h>
#endif

MODULE_LICENSE("GPL");
MODULE_AUTHOR("2023202062");
MODULE_DESCRIPTION("Assignment 2-2: Hook os_ftrace to my_ftrace");

typedef asmlinkage long (*sys_call_t)(const struct pt_regs *);

// Define syscall number for os_ftrace (created in Assignment 2-1)
#define NR_OS_FTRACE 336

// Flags to control dry-run and CR0 fallback usage
#define DRY_RUN 0
#define ALLOW_CR0_FALLBACK 0

// Pointers for system call table and original function
static sys_call_t *syscall_table = NULL;
static sys_call_t  real_os_ftrace = NULL;

// Function pointers to set memory permissions
typedef int (*fn_setmem_t)(unsigned long addr, int numpages);
static fn_setmem_t p_set_memory_rw = NULL;
static fn_setmem_t p_set_memory_ro = NULL;

#if ALLOW_CR0_FALLBACK && defined(CONFIG_X86)
// Backup state when using CR0 fallback
static bool cr0_wp_cleared = false;
static unsigned long cr0_irqflags = 0;
#endif

// Try to make target memory region read-write
static int try_set_memory_rw(void *addr)
{
    unsigned long p = (unsigned long)addr & PAGE_MASK;

    if (p_set_memory_rw) {
        int r = p_set_memory_rw(p, 1);
        if (r)
            pr_warn("set_memory_rw(%px) failed: %d\n", (void *)p, r);
        else
            pr_info("set_memory_rw(%px) succeeded\n", (void *)p);
        return r;
    }

#if ALLOW_CR0_FALLBACK && defined(CONFIG_X86)
    {
        unsigned long flags;
        unsigned long cr0;

        // Fallback: disable CR0 write-protection bit
        pr_warn("SETMEM_RW: set_memory_rw not available, using CR0 fallback\n");
        preempt_disable();
        local_irq_save(flags);
        cr0 = read_cr0();
        write_cr0(cr0 & ~X86_CR0_WP);
        cr0_wp_cleared = true;
        cr0_irqflags = flags;
        pr_info("SETMEM_RW: CR0 WP cleared (flags=%#lx)\n", flags);
        return 0;
    }
#else
    pr_err("set_memory_rw unavailable (not exported) and CR0 fallback disabled\n");
    return -ENOSYS;
#endif
}

// Try to restore memory back to read-only
static int try_set_memory_ro(void *addr)
{
    unsigned long p = (unsigned long)addr & PAGE_MASK;

    if (p_set_memory_ro) {
        int r = p_set_memory_ro(p, 1);
        if (r)
            pr_warn("set_memory_ro(%px) failed: %d\n", (void *)p, r);
        else
            pr_info("set_memory_ro(%px) succeeded\n", (void *)p);
        return r;
    }

#if ALLOW_CR0_FALLBACK && defined(CONFIG_X86)
    if (cr0_wp_cleared) {
        unsigned long cr0;
        // Restore CR0 write-protection bit
        pr_warn("SETMEM_RO: restoring CR0 WP (flags=%#lx)\n", cr0_irqflags);
        cr0 = read_cr0();
        write_cr0(cr0 | X86_CR0_WP);
        local_irq_restore(cr0_irqflags);
        preempt_enable();
        cr0_wp_cleared = false;
        cr0_irqflags = 0;
        pr_info("SETMEM_RO: CR0 WP restored\n");
        return 0;
    } else {
        pr_warn("SETMEM_RO: CR0 fallback not active; nothing to restore\n");
        return 0;
    }
#else
    pr_err("set_memory_ro unavailable (not exported) and CR0 fallback disabled\n");
    return -ENOSYS;
#endif
}

// Our replacement for os_ftrace
static asmlinkage long my_ftrace(const struct pt_regs *regs)
{
    pid_t pid = (pid_t)regs->di;
    long ret = -ENOSYS;

    // Call original os_ftrace if available
    if (real_os_ftrace)
        ret = real_os_ftrace(regs);

    // Print log message for verification
    pr_info("os_ftrace() hooked! os_ftrace -> my_ftrace, PID is [%d]\n", pid);
    return ret;
}

#if 0
// Alternative implementation for updating syscall table with stop_machine
struct table_update_args {
    sys_call_t *table;
    sys_call_t  new_fn;
    int slot;
    sys_call_t *old_fn_out;
};

static int table_update_fn(void *data)
{
    struct table_update_args *a = data;
    a->old_fn_out[0] = a->table[a->slot];
    a->table[a->slot] = a->new_fn;
    smp_wmb();
    return 0;
}
#endif

// Module initialization function
static int __init hooking_init(void)
{
    int ret = 0;

    pr_info("os_ftracehooking:init ENTER (DRY_RUN=%d)\n", (int)DRY_RUN);

    // Lookup the syscall table symbol
    syscall_table = (sys_call_t *)kallsyms_lookup_name("sys_call_table");
    if (!syscall_table)
        syscall_table = (sys_call_t *)kallsyms_lookup_name("__x64_sys_call_table");

    pr_info("os_ftracehooking:init: sys_call_table=%px\n", syscall_table);
    if (!syscall_table)
        return -ENOENT;

    // Lookup functions to change memory permissions
    p_set_memory_rw = (fn_setmem_t)kallsyms_lookup_name("set_memory_rw");
    p_set_memory_ro = (fn_setmem_t)kallsyms_lookup_name("set_memory_ro");
    pr_info("os_ftracehooking:init: kallsyms set_memory_rw=%px set_memory_ro=%px\n",
            p_set_memory_rw, p_set_memory_ro);

    if (!p_set_memory_rw || !p_set_memory_ro) {
        pr_warn("set_memory_* not exported; will not patch unless ALLOW_CR0_FALLBACK=1\n");
#if DRY_RUN
        pr_info("DRY_RUN: OK (nothing else to do)\n");
        return 0;
#else
#if !ALLOW_CR0_FALLBACK
        pr_err("Aborting: set_memory_* unavailable and CR0 fallback disabled\n");
        return -ENOSYS;
#endif
#endif
    }

#if DRY_RUN
    pr_info("DRY_RUN -> skip patching and return OK\n");
    return 0;
#else
    // Debug information
    pr_info("DEBUG: sys_call_table=%px\n", syscall_table);
    pr_info("DEBUG: target_slot(&syscall_table[%d])=%px\n", NR_OS_FTRACE, &syscall_table[NR_OS_FTRACE]);
    pr_info("DEBUG: my_ftrace=%px\n", my_ftrace);
    pr_info("DEBUG: kallsyms setmem rw=%px ro=%px\n", p_set_memory_rw, p_set_memory_ro);

    // Change memory to RW before patching
    pr_info("before set_memory_rw\n");
    ret = try_set_memory_rw(syscall_table);
    pr_info("DEBUG: try_set_memory_rw returned %d\n", ret);
    if (ret) {
        pr_err("ABORT: try_set_memory_rw failed (%d)\n", ret);
        return -EPERM;
    }

    // Save original os_ftrace
    real_os_ftrace = syscall_table[NR_OS_FTRACE];
    pr_info("old[%d] = %px\n", NR_OS_FTRACE, real_os_ftrace);
    if (!real_os_ftrace) {
        pr_err("original entry is NULL\n");
        (void)try_set_memory_ro(syscall_table);
        return -EINVAL;
    }

    // Overwrite syscall entry with my_ftrace
    pr_info("DEBUG: saved real_os_ftrace = %px\n", real_os_ftrace);
    pr_info("DEBUG: about to write my_ftrace into slot\n");

    pr_info("before write new entry (slot=%px)\n", &syscall_table[NR_OS_FTRACE]);
    syscall_table[NR_OS_FTRACE] = my_ftrace;

    smp_wmb();

    pr_info("after  write new entry (new=%px)\n", syscall_table[NR_OS_FTRACE]);

    // Restore page to read-only
    pr_info("before set_memory_ro\n");
    (void)try_set_memory_ro(syscall_table);
    pr_info("after  set_memory_ro\n");

    pr_info("installed wrapper for syscall %d (slot %px)\n", NR_OS_FTRACE, &syscall_table[NR_OS_FTRACE]);
    return 0;
#endif
}

// Module exit function
static void __exit hooking_exit(void)
{
#if !DRY_RUN
    // Restore original syscall entry on unload
    if (syscall_table && real_os_ftrace) {
        pr_info("restoring original entry\n");

        if (!try_set_memory_rw(syscall_table)) {
            pr_info("restoring slot %d: current=%px original=%px\n",
                    NR_OS_FTRACE, syscall_table[NR_OS_FTRACE], real_os_ftrace);
            syscall_table[NR_OS_FTRACE] = real_os_ftrace;

            smp_wmb();

            (void)try_set_memory_ro(syscall_table);
            pr_info("restored original syscall at slot %d\n", NR_OS_FTRACE);
        } else {
            pr_err("failed to set RW; cannot restore original syscall\n");
        }
    } else {
        pr_info("nothing to restore (either DRY_RUN or original not saved)\n");
    }
#endif
    pr_info("os_ftracehooking: exit\n");
}

module_init(hooking_init);
module_exit(hooking_exit);

