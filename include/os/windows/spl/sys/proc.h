
#ifndef _SPL_PROC_H
#define _SPL_PROC_H

//#include <sys/ucred.h>
//#include_next <sys/proc.h>
//#include <sys/kernel_types.h>
typedef struct proc { void *something; } proc_t;
extern proc_t p0;              /* process 0 */
#define current_proc PsGetCurrentProcess

#define current_proc PsGetCurrentProcess

#endif /* SPL_PROC_H */
