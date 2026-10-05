// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 *
 * Copyright (C) 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_SYS_SIGNAL_H
#define	_SPL_SYS_SIGNAL_H

#include <mach/mach_types.h>
#include <sys/kernel_types.h>
#include <sys/vm.h>
#include_next <sys/signal.h>

typedef struct thread *thread_t;

#define	FORREAL			0		/* Usual side-effects */
#define	JUSTLOOKING		1		/* Don't stop the process */

extern int thread_issignal(proc_t, thread_t, sigset_t);

#define	THREADMASK (sigmask(SIGILL)|sigmask(SIGTRAP)|\
		sigmask(SIGIOT)|sigmask(SIGEMT)|\
		sigmask(SIGFPE)|sigmask(SIGBUS)|\
		sigmask(SIGSEGV)|sigmask(SIGSYS)|\
		sigmask(SIGPIPE)|sigmask(SIGKILL)|\
		sigmask(SIGTERM)|sigmask(SIGINT))

extern int issig(void);

/* Always called with curthread */
#define	signal_pending(p) issig()

#endif /* SPL_SYS_SIGNAL_H */
