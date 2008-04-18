#ifndef _DEBUG_CTL_H
#define _DEBUG_CTL_H

/* Contains shared definitions which both the userspace
 * and kernelspace portions of splat must agree on.
 */

typedef struct spl_debug_header {
        int ph_len;
        int ph_flags;
        int ph_subsys;
        int ph_mask;
        int ph_cpu_id;
        int ph_sec;
        long ph_usec;
        int ph_stack;
        int ph_pid;
        int ph_line_num;
} spl_debug_header_t;

#endif /* _DEBUG_CTL_H */
