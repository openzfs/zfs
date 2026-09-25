/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 *
 * Copyright (C) 2019 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/thread.h>
#include <sys/kmem.h>
#include <sys/tsd.h>
#include <spl-debug.h>
#include <sys/vnode.h>
#include <sys/callb.h>
#include <sys/systm.h>

#include <ntddk.h>

#include <Trace.h>

uint64_t zfs_threads = 0;

kthread_t *
spl_thread_create(
    caddr_t	stk,
    size_t	stksize,
    void	(*proc)(void *),
    void	*arg,
    size_t	len,
    int	state,
#ifdef SPL_DEBUG_THREAD
    char	*filename,
    int	line,
#endif
    pri_t	pri)
{
	NTSTATUS result;

#ifdef SPL_DEBUG_THREAD
	dprintf("Start thread pri %d by '%s':%d\n", pri,
	    filename, line);
#endif
	HANDLE hThread = NULL;
	PETHREAD eThread = NULL;

	result = PsCreateSystemThread(
	    &hThread,
	    0,
	    NULL,
	    NULL,
	    NULL,
	    proc,
	    arg);

	if (!NT_SUCCESS(result))
		return (NULL);

	atomic_inc_64(&zfs_threads);

	result = ObReferenceObjectByHandle(
	    hThread,
	    0,
	    *PsThreadType,
	    KernelMode,
	    (PVOID *)&eThread,
	    NULL);

	if (!NT_SUCCESS(result)) {
		ZwClose(hThread);
		return (NULL);
	}

	if (pri > wtqclsyspri) {
		dprintf("Set thread attempted priority %d -- clamped\n", pri);
		pri = defclsyspri;
	}

	if (pri >= wtqclsyspri)
		KeSetPriorityThread((PKTHREAD)eThread, pri);

	/*
	 * Pin thread to processor group 0 to avoid cross-group
	 * scheduling bugs on unpatched multi-group systems
	 */
	GROUP_AFFINITY groupAffinity = { 0 };
	groupAffinity.Group = 0;
	groupAffinity.Mask = (KAFFINITY)(-1);  /* all CPUs in group 0 */
	ZwSetInformationThread(hThread, ThreadGroupInformation,
	    &groupAffinity, sizeof (groupAffinity));

	ObDereferenceObject(eThread);
	ZwClose(hThread);
	return ((kthread_t *)eThread);
}

kthread_t *
spl_current_thread(void)
{
	thread_t *cur_thread = current_thread();
	return ((kthread_t *)cur_thread);
}

extern __declspec(noreturn) NTSTATUS PsTerminateSystemThread(NTSTATUS);

__declspec(noreturn) void
spl_thread_exit(void)
{
	atomic_dec_64(&zfs_threads);

	tsd_thread_exit();
	(void) PsTerminateSystemThread(0);
}


/*
 * IllumOS has callout.c - place it here until we find a better place
 */
callout_id_t
timeout_generic(int type, void (*func)(void *), void *arg,
    hrtime_t expiration, hrtime_t resolution, int flags)
{
	//	struct timespec ts;
	//	hrt2ts(expiration, &ts);
	// bsd_timeout(func, arg, &ts);
	/*
	 * bsd_untimeout() requires func and arg to cancel the timeout, so
	 * pass it back as the callout_id. If we one day were to implement
	 * untimeout_generic() they would pass it back to us
	 */
	return ((callout_id_t)arg);
}

/*
 * Check if the current thread is a memory reclaim thread.
 * Everything in XNU is secret.
 */
int
current_is_reclaim_thread(void)
{
	return (0);
}
