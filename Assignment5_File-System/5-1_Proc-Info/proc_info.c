#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/sched/signal.h>
#include <linux/cred.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>

#define PROC_DIRNAME   "proc_2023202062"
#define PROC_FILENAME  "processInfo"
#define BUF_SIZE       4096

static struct proc_dir_entry *proc_dir;
static struct proc_dir_entry *proc_file;

// -1 : print information of all processes, >=0 : print only the process that has this PID
static pid_t filter_pid = -1;

// Flag to indicate whether read() already returned data once. After the first read, subsequent reads return 0 (EOF)
static int oslab_is_processed = 0;

// Kernel buffer for building output
static char kbuf[BUF_SIZE];

// Return full state name string for a given state character
static const char *state_fullname(char s)
{
    switch (s) {
        case 'R': return "running";
        case 'S': return "sleeping";
        case 'D': return "disk sleep";
        case 'I': return "idle";
        case 'T': return "stopped";
        case 't': return "tracing stop";
        case 'Z': return "zombie";
        case 'X': return "dead";
        default:  return "unknown";
    }
}

// Called when the proc file is opened
static int proc_info_open(struct inode *inode, struct file *file)
{
    // Reset flag so that read() outputs data again
    oslab_is_processed = 0;
    return 0;
}

// Helper function to append one task's info into kbuf
static int append_task_info(char *buf, size_t buf_size, int *offset,
                            struct task_struct *task)
{
    char state_ch = task_state_to_char(task);
    const char *state_long = state_fullname(state_ch);

    kuid_t uid = task->cred->uid;
    kgid_t gid = task->cred->gid;

    pid_t pid  = task->pid;
    pid_t ppid = task->real_parent ? task->real_parent->pid : 0;

    unsigned long long utime = task->utime;
    unsigned long long stime = task->stime;

    char state_buf[32];
    int written;

    if (*offset >= buf_size)
        return -ENOMEM;

    // Build full state string
    snprintf(state_buf, sizeof(state_buf), "%c (%s)", state_ch, state_long);

    // Output
    written = scnprintf(buf + *offset, buf_size - *offset,
                        "%-5d %-5d %-6u %-6u %-12llu %-12llu %-16s %s\n",
                        pid,
                        ppid,
                        __kuid_val(uid),
                        __kgid_val(gid),
                        utime,
                        stime,
                        state_buf,
                        task->comm);

    if (written <= 0)
        return -ENOMEM;

    *offset += written;
    return 0;
}

// read() implementation for /proc/proc_2023202062/processInfo
static ssize_t proc_info_read(struct file *file,
                              char __user *ubuf,
                              size_t count,
                              loff_t *ppos)
{
    int offset = 0;
    struct task_struct *task;
    int ret;

    // If already processed once, return EOF
    if (oslab_is_processed)
        return 0;

    // Header lines
    offset += scnprintf(kbuf + offset, BUF_SIZE - offset,
                        "Pid   PPid   Uid    Gid    utime         stime       State           Name\n");
    offset += scnprintf(kbuf + offset, BUF_SIZE - offset,
                        "---------------------------------------------------------------------------\n");

    rcu_read_lock();

    if (filter_pid < 0) {
        // Case 1: filter_pid == -1 -> print all processes
        for_each_process(task) {
            ret = append_task_info(kbuf, BUF_SIZE, &offset, task);
            if (ret < 0)
                break;
        }
    } else {
        // Case 2: print only the specific PID
        task = pid_task(find_vpid(filter_pid), PIDTYPE_PID);
        if (task) {
            append_task_info(kbuf, BUF_SIZE, &offset, task);
        } else {
            // If process with given PID does not exist
            offset += scnprintf(kbuf + offset, BUF_SIZE - offset,
                                "No such process: %d\n", filter_pid);
        }
    }

    rcu_read_unlock();

    // If user buffer is smaller than we need, return error
    if (count < (size_t)offset)
        return -EINVAL;

    if (copy_to_user(ubuf, kbuf, offset))
        return -EFAULT;

    oslab_is_processed = 1;
    *ppos += offset;
    return offset;
}

// write() implementation for /proc/proc_2023202062/processInfo
static ssize_t proc_info_write(struct file *file,
                               const char __user *ubuf,
                               size_t len,
                               loff_t *ppos)
{
    char buf[32];
    long value;
    size_t copy_len;

    // Limit input length to avoid overflow
    if (len >= sizeof(buf))
        copy_len = sizeof(buf) - 1;
    else
        copy_len = len;

    if (copy_from_user(buf, ubuf, copy_len))
        return -EFAULT;

    buf[copy_len] = '\0';

    // Parse the written value as a long integer. If parsing fails, set filter_pid = -1 (print all)
    if (kstrtol(buf, 10, &value) == 0) {
        filter_pid = (pid_t)value;
    } else {
        filter_pid = -1;
    }

    return len;
}

// file_operations for the proc file
static const struct file_operations proc_info_fops = {
    .owner = THIS_MODULE,
    .open  = proc_info_open,
    .read  = proc_info_read,
    .write = proc_info_write,
};

// Module init: create /proc/proc_2023202062 and processInfo under it
static int __init proc_info_init(void)
{
    proc_dir = proc_mkdir(PROC_DIRNAME, NULL);
    if (!proc_dir) {
        pr_err("proc_info: failed to create /proc/%s\n", PROC_DIRNAME);
        return -ENOMEM;
    }

    proc_file = proc_create(PROC_FILENAME, 0666, proc_dir, &proc_info_fops);
    if (!proc_file) {
        pr_err("proc_info: failed to create /proc/%s/%s\n",
               PROC_DIRNAME, PROC_FILENAME);
        remove_proc_entry(PROC_DIRNAME, NULL);
        return -ENOMEM;
    }

    pr_info("proc_info: loaded (/proc/%s/%s)\n", PROC_DIRNAME, PROC_FILENAME);
    return 0;
}

// Module exit: remove file then directory
static void __exit proc_info_exit(void)
{
    if (proc_file)
        remove_proc_entry(PROC_FILENAME, proc_dir);
    if (proc_dir)
        remove_proc_entry(PROC_DIRNAME, NULL);

    pr_info("proc_info: unloaded\n");
}

module_init(proc_info_init);
module_exit(proc_info_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("os2023202062");
MODULE_DESCRIPTION("OSLab Assignment 5-1: proc process info module");

