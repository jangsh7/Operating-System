#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include <linux/list.h>

#include "ftracehooking.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("2023202062");
MODULE_DESCRIPTION("Assignment 2-3: IO trace hooker with fd->filename mapping");

typedef asmlinkage long (*sys_call_t)(const struct pt_regs *); // syscall typedef

// Pointers to syscall table and helpers for page permission changes
static sys_call_t *syscall_table = NULL;
typedef int (*fn_setmem_t)(unsigned long addr, int numpages);
static fn_setmem_t p_set_memory_rw = NULL;
static fn_setmem_t p_set_memory_ro = NULL;

// Saved original syscalls (to restore later)
static sys_call_t real_openat = NULL;
static sys_call_t real_read   = NULL;
static sys_call_t real_write  = NULL;
static sys_call_t real_lseek  = NULL;
static sys_call_t real_close  = NULL;

// fd -> filename mapping entry (linked list node)
struct fd_map_entry {
    int fd;
    char *name;  // filename duplicated with kstrdup
    struct list_head list;  // linked list linkage
};

// List head for fd map and spinlock for concurrency
static LIST_HEAD(map_list);
static spinlock_t map_lock;

// Forward declarations for hooked syscall functions
static asmlinkage long ftrace_openat(const struct pt_regs *regs);
static asmlinkage long ftrace_read(const struct pt_regs *regs);
static asmlinkage long ftrace_write(const struct pt_regs *regs);
static asmlinkage long ftrace_lseek(const struct pt_regs *regs);
static asmlinkage long ftrace_close(const struct pt_regs *regs);

// Change page permissions to writable
static int try_set_memory_rw(void *addr)
{
    unsigned long p = (unsigned long)addr & PAGE_MASK;
    if (p_set_memory_rw) {
        int r = p_set_memory_rw(p, 1);
        if (r)
            pr_warn("iotrace: set_memory_rw(%px) failed: %d\n", (void *)p, r);
        return r;
    }
    pr_err("iotrace: set_memory_rw not available\n");
    return -ENOSYS;
}

// Change page permissions back to read-only
static int try_set_memory_ro(void *addr)
{
    unsigned long p = (unsigned long)addr & PAGE_MASK;
    if (p_set_memory_ro) {
        int r = p_set_memory_ro(p, 1);
        if (r)
            pr_warn("iotrace: set_memory_ro(%px) failed: %d\n", (void *)p, r);
        return r;
    }
    pr_err("iotrace: set_memory_ro not available\n");
    return -ENOSYS;
}

// Find mapping by fd (lock must be held)
static struct fd_map_entry *map_find_locked(int fd)
{
    struct fd_map_entry *e;
    list_for_each_entry(e, &map_list, list) {
        if (e->fd == fd)
            return e;
    }
    return NULL;
}

// Add new fd->filename mapping
static struct fd_map_entry *map_add_locked(int fd, const char *name)
{
    struct fd_map_entry *e;
    e = kmalloc(sizeof(*e), GFP_KERNEL);
    if (!e)
        return NULL;
    e->fd = fd;
    e->name = kstrdup(name ? name : "<unknown>", GFP_KERNEL);
    if (!e->name) {
        kfree(e);
        return NULL;
    }
    INIT_LIST_HEAD(&e->list);
    list_add_tail(&e->list, &map_list);
    return e;
}

// Remove mapping by fd
static void map_remove_locked(int fd)
{
    struct fd_map_entry *e, *tmp;
    list_for_each_entry_safe(e, tmp, &map_list, list) {
        if (e->fd == fd) {
            list_del(&e->list);
            kfree(e->name);
            kfree(e);
            break;
        }
    }
}

// Clear all fd->filename mappings
static void map_clear_all(void)
{
    struct fd_map_entry *e, *tmp;
    unsigned long flags;
    spin_lock_irqsave(&map_lock, flags);
    list_for_each_entry_safe(e, tmp, &map_list, list) {
        list_del(&e->list);
        kfree(e->name);
        kfree(e);
    }
    spin_unlock_irqrestore(&map_lock, flags);
}

// Copy user path string into kernel buffer (safe method)
static int safe_get_user_path_alloc(const char __user *u_path, char **out)
{
    char *kpath;
    long n;

    if (!out) return -EINVAL;
    *out = NULL;
    if (!u_path) return -EINVAL;

    kpath = kzalloc(FTRACE_MAX_PATH, GFP_KERNEL);
    if (!kpath) return -ENOMEM;

    n = strncpy_from_user(kpath, u_path, FTRACE_MAX_PATH);
    if (n <= 0) {
        kfree(kpath);
        *out = NULL;
        return -EFAULT;
    }

    // Ensure null termination
    if (n >= FTRACE_MAX_PATH)
        kpath[FTRACE_MAX_PATH - 1] = '\0';
    else
        kpath[n] = '\0';

    *out = kpath;
    return 0;
}

/* ---- Hooked syscall implementations ---- */

// Hooked openat: intercepts file open
static asmlinkage long ftrace_openat(const struct pt_regs *regs)
{
    const char __user *u_path = (const char __user *) regs->si;
    char *kpath = NULL;
    long ret;
    int rc;

    rc = safe_get_user_path_alloc(u_path, &kpath);

    // Call original openat
    if (real_openat)
        ret = real_openat(regs);
    else
        ret = -ENOSYS;

    // If successful and tracing active, add mapping and update stats
    if (ret >= 0 && ftrace_trace_active && current && current->pid == ftrace_target_pid) {
        unsigned long flags;
        int fd = (int) ret;

        spin_lock_irqsave(&map_lock, flags);
        if (!map_find_locked(fd))
            map_add_locked(fd, kpath ? kpath : "<unknown>");
        spin_unlock_irqrestore(&map_lock, flags);

        if (kpath) {
            ftrace_register_last_open(kpath);
            ftrace_add_update(kpath, FTRACE_OP_OPEN, 0);
        } else {
            ftrace_add_update("<unknown>", FTRACE_OP_OPEN, 0);
        }
    }

    kfree(kpath);
    return ret;
}

// Hooked read: update read counters
static asmlinkage long ftrace_read(const struct pt_regs *regs)
{
    int fd = (int) regs->di;
    long ret;
    char *name_dup = NULL;
    unsigned long flags;

    if (real_read)
        ret = real_read(regs);
    else
        ret = -ENOSYS;

    if (ret > 0 && ftrace_trace_active && current && current->pid == ftrace_target_pid) {
        spin_lock_irqsave(&map_lock, flags);
        {
            struct fd_map_entry *e = map_find_locked(fd);
            if (e && e->name)
                name_dup = kstrdup(e->name, GFP_ATOMIC);
        }
        spin_unlock_irqrestore(&map_lock, flags);

        if (name_dup) {
            ftrace_add_update(name_dup, FTRACE_OP_READ, ret);
            kfree(name_dup);
        } else {
            ftrace_add_update("<unknown>", FTRACE_OP_READ, ret);
        }
    }
    return ret;
}

// Hooked write: update write counters
static asmlinkage long ftrace_write(const struct pt_regs *regs)
{
    int fd = (int) regs->di;
    long ret;
    char *name_dup = NULL;
    unsigned long flags;

    if (real_write)
        ret = real_write(regs);
    else
        ret = -ENOSYS;

    if (ret > 0 && ftrace_trace_active && current && current->pid == ftrace_target_pid) {
        spin_lock_irqsave(&map_lock, flags);
        {
            struct fd_map_entry *e = map_find_locked(fd);
            if (e && e->name)
                name_dup = kstrdup(e->name, GFP_ATOMIC);
        }
        spin_unlock_irqrestore(&map_lock, flags);

        if (name_dup) {
            ftrace_add_update(name_dup, FTRACE_OP_WRITE, ret);
            kfree(name_dup);
        } else {
            ftrace_add_update("<unknown>", FTRACE_OP_WRITE, ret);
        }
    }
    return ret;
}

// Hooked lseek: update lseek counters
static asmlinkage long ftrace_lseek(const struct pt_regs *regs)
{
    int fd = (int) regs->di;
    long ret;
    char *name_dup = NULL;
    unsigned long flags;

    if (real_lseek)
        ret = real_lseek(regs);
    else
        ret = -ENOSYS;

    if (ftrace_trace_active && current && current->pid == ftrace_target_pid) {
        spin_lock_irqsave(&map_lock, flags);
        {
            struct fd_map_entry *e = map_find_locked(fd);
            if (e && e->name)
                name_dup = kstrdup(e->name, GFP_ATOMIC);
        }
        spin_unlock_irqrestore(&map_lock, flags);

        if (name_dup) {
            ftrace_add_update(name_dup, FTRACE_OP_LSEEK, 0);
            kfree(name_dup);
        } else {
            ftrace_add_update("<unknown>", FTRACE_OP_LSEEK, 0);
        }
    }
    return ret;
}

// Hooked close: remove fd mapping and update stats
static asmlinkage long ftrace_close(const struct pt_regs *regs)
{
    int fd = (int) regs->di;
    long ret;
    char *name_dup = NULL;
    unsigned long flags;

    if (real_close)
        ret = real_close(regs);
    else
        ret = -ENOSYS;

    if (ftrace_trace_active && current && current->pid == ftrace_target_pid) {
        spin_lock_irqsave(&map_lock, flags);
        {
            struct fd_map_entry *e = map_find_locked(fd);
            if (e && e->name)
                name_dup = kstrdup(e->name, GFP_ATOMIC);
            map_remove_locked(fd);
        }
        spin_unlock_irqrestore(&map_lock, flags);

        if (name_dup) {
            ftrace_add_update(name_dup, FTRACE_OP_CLOSE, 0);
            kfree(name_dup);
        } else {
            ftrace_add_update("<unknown>", FTRACE_OP_CLOSE, 0);
        }
    }
    return ret;
}

/* ---- Module lifecycle ---- */

// Module initialization: hook syscalls
static int __init iotrace_init(void)
{
    pr_info("iotrace: init\n");
    spin_lock_init(&map_lock);

    // Locate syscall table
    syscall_table = (sys_call_t *) kallsyms_lookup_name("sys_call_table");
    if (!syscall_table)
        syscall_table = (sys_call_t *) kallsyms_lookup_name("__x64_sys_call_table");
    if (!syscall_table)
        return -ENOENT;

    // Resolve memory helpers
    p_set_memory_rw = (fn_setmem_t)kallsyms_lookup_name("set_memory_rw");
    p_set_memory_ro = (fn_setmem_t)kallsyms_lookup_name("set_memory_ro");
    if (!p_set_memory_rw || !p_set_memory_ro)
        return -ENOSYS;

    // Save original syscalls
    real_openat = syscall_table[__NR_openat];
    real_read   = syscall_table[__NR_read];
    real_write  = syscall_table[__NR_write];
#ifdef __NR_lseek
    real_lseek  = syscall_table[__NR_lseek];
#endif
    real_close  = syscall_table[__NR_close];

    if (try_set_memory_rw(syscall_table))
        return -EPERM;

    // Install our hooks
    syscall_table[__NR_openat] = (sys_call_t) ftrace_openat;
    syscall_table[__NR_read]   = (sys_call_t) ftrace_read;
    syscall_table[__NR_write]  = (sys_call_t) ftrace_write;
#ifdef __NR_lseek
    syscall_table[__NR_lseek]  = (sys_call_t) ftrace_lseek;
#endif
    syscall_table[__NR_close]  = (sys_call_t) ftrace_close;
    smp_wmb();

    try_set_memory_ro(syscall_table);

    pr_info("iotrace: installed wrappers for openat/read/write/lseek/close\n");
    return 0;
}

// Module exit: restore syscalls and cleanup resources
static void __exit iotrace_exit(void)
{
    pr_info("iotrace: exit\n");
    if (!syscall_table) return;

    if (!try_set_memory_rw(syscall_table)) {
        if (real_openat) syscall_table[__NR_openat] = real_openat;
        if (real_read)   syscall_table[__NR_read]   = real_read;
        if (real_write)  syscall_table[__NR_write]  = real_write;
#ifdef __NR_lseek
        if (real_lseek)  syscall_table[__NR_lseek]  = real_lseek;
#endif
        if (real_close)  syscall_table[__NR_close]  = real_close;
        smp_wmb();
        try_set_memory_ro(syscall_table);
        pr_info("iotrace: restored original syscalls\n");
    }

    // Clear fd->filename mappings
    map_clear_all();
}

module_init(iotrace_init);
module_exit(iotrace_exit);

