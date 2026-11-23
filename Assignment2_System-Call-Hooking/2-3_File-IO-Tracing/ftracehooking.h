#ifndef _FTRACEHOOKING_H_
#define _FTRACEHOOKING_H_

#include <linux/types.h>
#include <linux/list.h>

#define FTRACE_MAX_PATH 4096  // Max filename length to store in stats

// Enum: operation types tracked by ftrace
enum ftrace_op {
    FTRACE_OP_OPEN = 0,   // open() call
    FTRACE_OP_CLOSE,      // close() call
    FTRACE_OP_READ,       // read() call
    FTRACE_OP_WRITE,      // write() call
    FTRACE_OP_LSEEK       // lseek() call
};

// Per-file statistics structure
struct ftrace_file_stat {
    char filename[FTRACE_MAX_PATH];  // Tracked filename
    unsigned long open_count;        // Number of open() calls
    unsigned long close_count;       // Number of close() calls
    unsigned long read_count;        // Number of read() calls
    unsigned long write_count;       // Number of write() calls
    unsigned long lseek_count;       // Number of lseek() calls
    unsigned long read_bytes;        // Total bytes read
    unsigned long write_bytes;       // Total bytes written
    struct list_head list;           // Linked list node
};

// Exported globals (defined in ftracehooking.c)
extern pid_t ftrace_target_pid;    // PID currently being traced
extern bool ftrace_trace_active;   // Flag: trace active or not

// Exported functions (used by iotracehooking.c and others)
extern void ftrace_add_update(const char *name, enum ftrace_op op, ssize_t bytes);  
// Update stats for a given filename and operation

extern void ftrace_register_last_open(const char *name);  
// Save last opened filename (for trace context)

extern void ftrace_print_summary_and_clear(void);  
// Print collected stats and reset state

#endif

