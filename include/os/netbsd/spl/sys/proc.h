// SPDX-License-Identifier: BSD-2-Clause
/*
 * OpenZFS proc / kthread wrappers for NetBSD.
 */

#ifndef _OPENSOLARIS_SYS_PROC_H_
#define	_OPENSOLARIS_SYS_PROC_H_

#include_next <sys/proc.h>
#include <sys/stdint.h>

#ifdef _KERNEL

#include <sys/kthread.h>
#include <sys/cpu.h>

#define	CPU		curcpu()
#define	minclsyspri	PRI_KTHREAD
#define	defclsyspri	PRI_KTHREAD
#define	wtqclsyspri	PRI_KTHREAD
#define	maxclsyspri	MAXPRI_KTHREAD
#define	max_ncpus	ncpu
#define	boot_max_ncpus	ncpu
/*
 * Illumos thread identity.  Shared sources (spa.c and the rest
 * of module/zfs) use curthread; this is the kernel SPL mapping
 * onto NetBSD.  Userland libspl provides a dummy curthread
 * (pthread_self), the same way Linux maps curthread → current
 * in-kernel and stubs it in libspl.
 */
#define	curthread	curlwp
/*
 * Illumos interrupt-context predicate.  Shared sources must
 * not call cpu_intr_p() / cpu_softintr_p() -- those are NetBSD.
 */
#define	servicing_interrupt()	(cpu_intr_p() || cpu_softintr_p())

#define	TS_RUN	0
#define	p0	proc0

typedef	struct lwp	_kthread;
typedef	struct lwp	kthread_t;
typedef	struct lwp	*kthread_id_t;

extern proc_t *system_proc;

kthread_t *solaris__thread_create(void *, size_t, void (*)(void *), void *,
    size_t, proc_t *, int, pri_t, const char *);
void	thread_exit(void);

#define	thread_create_named(name, stk, stksize, proc, arg, len,	\
    pp, state, pri) \
	solaris__thread_create(stk, stksize, proc, arg, len, pp, state, \
	    pri, name)
#define	thread_create(stk, stksize, proc, arg, len, pp, state, pri) \
	solaris__thread_create(stk, stksize, proc, arg, len, pp, state, \
	    pri, #proc)

static inline boolean_t
zfs_proc_is_caller(proc_t *p)
{
	return (p == curproc);
}

/*
 * Common OpenZFS (zfs_ioctl.c) calls groupmember().  Define the
 * wrapper here, after include_next <sys/proc.h>, so we do not
 * rewrite the kernel prototype.  cred.h must not define this.
 */
#define	groupmember(gid, cr)	zfs_cr_groupmember((gid), (cr))

#endif /* _KERNEL */

#endif /* _OPENSOLARIS_SYS_PROC_H_ */
