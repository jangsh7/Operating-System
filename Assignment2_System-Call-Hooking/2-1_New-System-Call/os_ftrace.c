#include <linux/kernel.h>
#include <linux/syscalls.h>
#include <linux/printk.h>
#include <linux/types.h>

// Define system call os_ftrace with one argument (pid)
SYSCALL_DEFINE1(os_ftrace, pid_t, pid)
{
    printk(KERN_INFO "ORIGINAL ftrace() called! PID is [%d]\n", pid);  // Print log message with given PID
    return 0;
}
