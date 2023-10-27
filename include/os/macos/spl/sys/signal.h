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
 * Copyright (C) 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_SYS_SIGNAL_H
#define	_SPL_SYS_SIGNAL_H

#include <sys/vm.h>
#include_next <sys/signal.h>
#include <kern/thread.h>

#define	FORREAL			0		/* Usual side-effects */
#define	JUSTLOOKING		1		/* Don't stop the process */

struct proc;

extern int thread_issignal(struct proc *, thread_t, sigset_t);

#define	THREADMASK (sigmask(SIGILL)|sigmask(SIGTRAP)|\
		sigmask(SIGIOT)|sigmask(SIGEMT)|\
		sigmask(SIGFPE)|sigmask(SIGBUS)|\
		sigmask(SIGSEGV)|sigmask(SIGSYS)|\
		sigmask(SIGPIPE)|sigmask(SIGKILL)|\
		sigmask(SIGTERM)|sigmask(SIGINT))

static __inline__ int
issig(int why)
{
	return (thread_issignal(current_proc(), current_thread(),
	    THREADMASK));
}

/* Always called with curthread */
#define	signal_pending(p) issig(0)

#endif /* SPL_SYS_SIGNAL_H */
