
#ifndef _SPL_PROC_H
#define _SPL_PROC_H

#include <sys/types.h>

typedef struct _KPROCESS proc_t;

extern proc_t p0;

#define	current_proc PsGetCurrentProcess
#define getpid() PsGetProcessId(PsGetCurrentProcess())

static inline boolean_t
zfs_proc_is_caller(proc_t *p)
{
	return (p == PsGetCurrentProcess());
}

static inline char *
getcomm(void)
{
	return "procname"; // WIN32 me
}

#endif /* SPL_PROC_H */
