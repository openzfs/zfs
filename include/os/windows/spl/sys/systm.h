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

#ifndef _SPL_SYSTM_H
#define	_SPL_SYSTM_H

#include <sys/sunddi.h>

typedef uintptr_t pc_t;

// Find a header to place this?
struct bsd_timeout_wrapper {
	uint32_t flag;  // Must be first
	uint32_t init;
	void(*func)(void *);
	void *arg;
	KTIMER timer;
};

/*
 * bsd_timeout will create a new thread, and the new thread will
 * first sleep the desired duration, then call the wanted function
 */
static inline void bsd_timeout_handler(void *arg)
{
	struct bsd_timeout_wrapper *btw = arg;
	KeWaitForSingleObject(&btw->timer, Executive, KernelMode, TRUE, NULL);
	if (btw->init == 0x42994299) btw->func(btw->arg);
	thread_exit();
}

#define	BSD_TIMEOUT_MAGIC 0x42994299
static inline void bsd_untimeout(void(*func)(void *), void *ID)
{
/*
 * Unfortunately, calling KeSetTimer() does not Signal (or abort) any thread
 * sitting in KeWaitForSingleObject() so they would wait forever. Instead we
 * change the timeout to be now, so that the threads can exit.
 */
	struct bsd_timeout_wrapper *btw = (struct bsd_timeout_wrapper *)ID;
	LARGE_INTEGER p = { .QuadPart = -1 };
	btw->init = 0;
	// Investigate why this assert triggers on Unload
	// ASSERT(btw->init == BSD_TIMEOUT_MAGIC); // timer was not initialized
	if (btw->init == BSD_TIMEOUT_MAGIC)
		KeSetTimer(&btw->timer, p, NULL);
}

static inline void bsd_timeout(void *FUNC, void *ID, struct timespec *TIM)
{
	LARGE_INTEGER duetime;
	struct bsd_timeout_wrapper *btw = (struct bsd_timeout_wrapper *)ID;
	void(*func)(void *) = FUNC;
	ASSERT(btw != NULL);
	duetime.QuadPart = -((int64_t)(SEC2NSEC100(TIM->tv_sec) +
	    NSEC2NSEC100(TIM->tv_nsec)));
	btw->func = func;
	btw->arg = ID;
	/* Global vars are guaranteed set to 0, still is this secure enough? */
	if (btw->init != BSD_TIMEOUT_MAGIC) {
		btw->init = BSD_TIMEOUT_MAGIC;
		KeInitializeTimer(&btw->timer);
	}
	if (!KeSetTimer(&btw->timer, duetime, NULL)) {
		func((ID));
	} else {
		/* Another option would have been to use taskq, it can cancel */
		thread_create(NULL, 0, bsd_timeout_handler, ID, 0, NULL,
		    TS_RUN, minclsyspri);
	}
}

#endif /* SPL_SYSTM_H */
