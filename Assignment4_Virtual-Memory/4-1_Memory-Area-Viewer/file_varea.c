#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/kallsyms.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/sched/mm.h>
#include <linux/mm.h>
#include <linux/mm_types.h>
#include <linux/fs.h>
#include <linux/dcache.h>
#include <linux/slab.h>
#include <linux/ptrace.h>
#include <linux/limits.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("2023202062");
MODULE_DESCRIPTION("Assignment 4-1: file_varea wrapper for syscall #336");

#define OS_FTRACE_NR 336

// 쓰기권한 on/off
static void make_rw(void *addr);
static void make_ro(void *addr);

// sys_call_table 포인터 & 원본 함수 포인터
static unsigned long **syscall_table;
static asmlinkage long (*real_os_ftrace)(const struct pt_regs *regs);

// sys_call_table 주소 얻기
static unsigned long **get_sys_call_table(void)
{
    return (unsigned long **)kallsyms_lookup_name("sys_call_table");
}

// PID 기반으로 VMA를 순회하면서 출력
static int do_file_varea(pid_t pid)
{
    struct task_struct *task;
    struct mm_struct   *mm;
    struct vm_area_struct *vma;

    // PID -> task_struct
    rcu_read_lock();
    task = pid_task(find_vpid(pid), PIDTYPE_PID);
    if (!task) {
        rcu_read_unlock();
        printk(KERN_INFO "[4-1] pid %d not found\n", pid);
        return -1;
    }
    get_task_struct(task);  // 사용 중 참조 보호
    rcu_read_unlock();

    // task -> mm_struct
    mm = get_task_mm(task);
    if (!mm) {
        printk(KERN_INFO "[4-1] pid %d has no mm (kernel thread?)\n", pid);
        put_task_struct(task);
        return -1;
    }

    printk(KERN_INFO
           "######## Loaded files of a process '%s(%d)' in VM ########\n",
           task->comm, task->pid);

    // 각 VMA 순회
    for (vma = mm->mmap; vma; vma = vma->vm_next) {
        char *pathbuf = NULL;
        char *pathname = NULL;

        // 파일에 매핑된 VMA만 출력
        if (!vma->vm_file)
            continue;

        pathbuf = kmalloc(PATH_MAX, GFP_KERNEL);
        if (!pathbuf)
            continue;

        pathname = d_path(&vma->vm_file->f_path, pathbuf, PATH_MAX);
        if (!IS_ERR(pathname)) {
            printk(KERN_INFO
                   "mem(%lx-%lx) code(%lx-%lx) data(%lx-%lx) heap(%lx-%lx) %s\n",
                   vma->vm_start, vma->vm_end,
                   mm->start_code, mm->end_code,
                   mm->start_data, mm->end_data,
                   mm->start_brk, mm->brk,
                   pathname);
        }

        kfree(pathbuf);
    }

    printk(KERN_INFO
           "######## ############################################ ########\n");

    mmput(mm);
    put_task_struct(task);
    return 0;
}

// os_ftrace(336)을 file_varea로 대체
static asmlinkage long file_varea_syscall(const struct pt_regs *regs)
{
    pid_t pid = (pid_t)regs->di;  // 첫 번째 인자 (rdi 레지스터)
    int ret = do_file_varea(pid);

    if (ret == 0)
        return pid;  // 성공 시 입력 받은 PID 리턴
    else
        return -1;  // 실패 시 -1
}

static int __init file_varea_init(void)
{
    syscall_table = get_sys_call_table();
    if (!syscall_table) {
        printk(KERN_ERR "[4-1] cannot find sys_call_table\n");
        return -EINVAL;
    }

    make_rw(syscall_table);
    real_os_ftrace = (void *)syscall_table[OS_FTRACE_NR];
    syscall_table[OS_FTRACE_NR] = (unsigned long *)file_varea_syscall;
    make_ro(syscall_table);

    printk(KERN_INFO "[4-1] file_varea loaded, os_ftrace hooked\n");
    return 0;
}

static void __exit file_varea_exit(void)
{
    if (syscall_table && real_os_ftrace) {
        make_rw(syscall_table);
        syscall_table[OS_FTRACE_NR] = (unsigned long *)real_os_ftrace;
        make_ro(syscall_table);
    }
    printk(KERN_INFO "[4-1] file_varea unloaded, os_ftrace restored\n");
}

module_init(file_varea_init);
module_exit(file_varea_exit);

#include <asm/pgtable.h>

static void make_rw(void *addr)
{
    unsigned int level;
    pte_t *pte = lookup_address((unsigned long)addr, &level);
    if (pte->pte & ~_PAGE_RW)
        pte->pte |= _PAGE_RW;
}

static void make_ro(void *addr)
{
    unsigned int level;
    pte_t *pte = lookup_address((unsigned long)addr, &level);
    pte->pte = pte->pte & ~_PAGE_RW;
}

