/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

/* #pragma ident	"@(#)getpcstack.c	1.5	05/06/08 SMI" */

#include "config.h"
#include "misc.h"

#if HAVE_UCONTEXT_H
#include <ucontext.h>
#endif

#if HAVE_SYS_FRAME_H
#include <sys/frame.h>
#endif
#if HAVE_SYS_STACK_H
#include <sys/stack.h>
#endif

#include <stdio.h>

#if defined(__MACH__)
/*
 * Darwin doesn't have any exposed frame info, so give it some space.
 */
#define UMEM_FRAMESIZE (2 * sizeof(long long))

#elif defined(__sparc) || defined(__sparcv9)
extern void flush_windows(void);
#define	UMEM_FRAMESIZE	MINFRAME

#elif defined(__i386) || defined(__amd64)
/*
 * On x86, MINFRAME is defined to be 0, but we want to be sure we can
 * dereference the entire frame structure.
 */
#define	UMEM_FRAMESIZE	(sizeof (struct frame))

#elif !defined(EC_UMEM_DUMMY_PCSTACK)
#error needs update for new architecture
#endif

/*
 * Get a pc-only stacktrace.  Used for kmem_alloc() buffer ownership tracking.
 * Returns MIN(current stack depth, pcstack_limit).
 */
/*ARGSUSED*/
int
getpcstack(uintptr_t *pcstack, int pcstack_limit, int check_signal)
{
#ifdef EC_UMEM_DUMMY_PCSTACK
  return 0;
#else
	struct frame *fp;
	struct frame *nextfp, *minfp;
	int depth = 0;
	uintptr_t base = 0;
	size_t size = 0;
#ifndef UMEM_STANDALONE
	int on_altstack = 0;
	uintptr_t sigbase = 0;
	size_t sigsize = 0;

	stack_t st;

	if (stack_getbounds(&st) != 0) {
		if (thr_stksegment(&st) != 0 ||
		    (uintptr_t)st.ss_sp < st.ss_size) {
			return (0);		/* unable to get stack bounds */
		}
		/*
		 * thr_stksegment(3C) has a slightly different interface than
		 * stack_getbounds(3C) -- correct it
		 */
		st.ss_sp = (void *)(((uintptr_t)st.ss_sp) - st.ss_size);
		st.ss_flags = 0;		/* can't be on-stack */
	}
	on_altstack = (st.ss_flags & SS_ONSTACK);

	if (st.ss_size != 0) {
		base = (uintptr_t)st.ss_sp;
		size = st.ss_size;
	} else {
		/*
		 * If size == 0, then ss_sp is the *top* of the stack.
		 *
		 * Since we only allow increasing frame pointers, and we
		 * know our caller set his up correctly, we can treat ss_sp
		 * as an upper bound safely.
		 */
		base = 0;
		size = (uintptr_t)st.ss_sp;
	}

	if (check_signal != 0) {
		void (*sigfunc)() = NULL;
		int sigfuncsize = 0;
		extern void thr_sighndlrinfo(void (**)(), int *);

		thr_sighndlrinfo(&sigfunc, &sigfuncsize);
		sigbase = (uintptr_t)sigfunc;
		sigsize = sigfuncsize;
	}
#else /* UMEM_STANDALONE */
	base = (uintptr_t)umem_min_stack;
	size = umem_max_stack - umem_min_stack;
#endif

	/*
	 * shorten size so that fr_savfp and fr_savpc will be within the stack
	 * bounds.
	 */
	if (size >= UMEM_FRAMESIZE - 1)
		size -= (UMEM_FRAMESIZE - 1);
	else
		size = 0;

#if defined(__sparc) || defined(__sparcv9)
	flush_windows();
#endif

	/* LINTED alignment */
	fp = (struct frame *)((caddr_t)getfp() + STACK_BIAS);

	minfp = fp;

	if (((uintptr_t)fp - base) >= size)
		return (0);	/* the frame pointer isn't in our stack */

	while (depth < pcstack_limit) {
		uintptr_t tmp;

		/* LINTED alignment */
		nextfp = (struct frame *)((caddr_t)fp->fr_savfp + STACK_BIAS);
		tmp = (uintptr_t)nextfp;

		/*
		 * Check nextfp for validity.  It must be properly aligned,
		 * increasing compared to the last %fp (or the top of the
		 * stack we just switched to), and it must be inside
		 * [base, base + size).
		 */
		if (tmp != SA(tmp))
			break;
		else if (nextfp <= minfp || (tmp - base) >= size) {
#ifndef UMEM_STANDALONE
			if (tmp == NULL || !on_altstack)
				break;
			/*
			 * If we're on an alternate signal stack, try jumping
			 * to the main thread stack.
			 *
			 * If the main thread stack has an unlimited size, we
			 * punt, since we don't know where the frame pointer's
			 * been.
			 *
			 * (thr_stksegment() returns the *top of stack*
			 * in ss_sp, not the bottom)
			 */
			if (thr_stksegment(&st) == 0) {
				if (st.ss_size >= (uintptr_t)st.ss_sp ||
				    st.ss_size < UMEM_FRAMESIZE - 1)
					break;

				on_altstack = 0;
				base = (uintptr_t)st.ss_sp - st.ss_size;
				size = st.ss_size - (UMEM_FRAMESIZE - 1);
				minfp = (struct frame *)base;
				continue;		/* try again */
			}
#endif
			break;
		}

#ifndef UMEM_STANDALONE
		if (check_signal && (fp->fr_savpc - sigbase) <= sigsize)
			umem_panic("called from signal handler");
#endif
		pcstack[depth++] = fp->fr_savpc;
		fp = nextfp;
		minfp = fp;
	}
	return (depth);
#endif
}
