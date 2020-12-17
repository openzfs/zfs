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

#ifndef _SPL_SIGNAL_H
#define _SPL_SIGNAL_H

// #include <sys/sched.h>
// #include <sys/vm.h>
// #include <sys/proc.h>
// #include <sys/thread.h>
#include_next <signal.h>

#define	FORREAL		0	/* Usual side-effects */
#define	JUSTLOOKING	1	/* Don't stop the process */


#define	SIGPIPE	    0

struct proc;

//extern int
//thread_issignal(struct proc *, thread_t, sigset_t);
#define	SA_SIGINFO	0x00000008

typedef struct __siginfo {
	/* Windows version goes here */
	void *si_addr;
} siginfo_t;

typedef struct {
	unsigned long sig[1];
} sigset_t;

typedef void (*sighandler_t) (int);

struct sigaction {
	union
	{
	/* Used if SA_SIGINFO is not set.  */
		sighandler_t sa_handler;
	/* Used if SA_SIGINFO is set.  */
		void (*sa_sigaction) (int, siginfo_t *, void *);
	};
	sigset_t sa_mask;
	/* Special flags.  */
	int sa_flags;
};

/* The "why" argument indicates the allowable side-effects of the call:
 *
 * FORREAL:  Extract the next pending signal from p_sig into p_cursig;
 * stop the process if a stop has been requested or if a traced signal
 * is pending.
 *
 * JUSTLOOKING:  Don't stop the process, just indicate whether or not
 * a signal might be pending (FORREAL is needed to tell for sure).
 */
#define threadmask (sigmask(SIGILL)|sigmask(SIGTRAP)|\
                    sigmask(SIGIOT)|sigmask(SIGEMT)|\
                    sigmask(SIGFPE)|sigmask(SIGBUS)|\
                    sigmask(SIGSEGV)|sigmask(SIGSYS)|\
                    sigmask(SIGPIPE)|sigmask(SIGKILL)|\
                    sigmask(SIGTERM)|sigmask(SIGINT))

static inline int
issig(int why)
{
	return 0;
}

#define signal_pending(p) issig(0)

#endif /* SPL_SIGNAL_H */
