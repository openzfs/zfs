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
 * Copyright(c) 2019 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef _SPL_SYSTM_H
#define	_SPL_SYSTM_H

#include <sys/sunddi.h>
#include <Trace.h>

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
#define	BSD_TIMEOUT_MAGIC 0x42994299
static inline void bsd_timeout_handler(void *arg)
{
	struct bsd_timeout_wrapper *btw = arg;
	KeWaitForSingleObject(&btw->timer, Executive, KernelMode, TRUE, NULL);
	if (btw->init == BSD_TIMEOUT_MAGIC)
		btw->func(btw->arg);
	thread_exit();
}

static inline void bsd_untimeout(void(*func)(void *), void *ID)
{
/*
 * Unfortunately, calling KeCancelTimer() does not Signal (or abort) any thread
 * sitting in KeWaitForSingleObject() so they would wait forever. Instead we
 * change the timeout to be now, so that the threads can exit.
 */
	struct bsd_timeout_wrapper *btw = (struct bsd_timeout_wrapper *)ID;
	LARGE_INTEGER p = { .QuadPart = -1 };
	VERIFY3P(btw, !=, NULL);
	// If timer was armed, release it.
	if (btw->init == BSD_TIMEOUT_MAGIC) {
		btw->init = 0; // stop it from running func()
		KeSetTimer(&btw->timer, p, NULL);
	}
}

static inline void bsd_timeout(void *FUNC, void *ID, struct timespec *TIM)
{
	LARGE_INTEGER duetime;
	struct bsd_timeout_wrapper *btw = (struct bsd_timeout_wrapper *)ID;
	void(*func)(void *) = FUNC;
	if (btw == NULL) {
		dprintf("%s NULL ID is not implemented\n", __func__);
		return;
	}
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

/*
 * Unfortunately, calling KeCancelTimer() does not Signal (or abort) any thread
 * sitting in KeWaitForSingleObject() so they would wait forever. Call this
 * function only when there are no threads waiting in bsd_timeout_handler().
 * Unloading the driver with loaded timer object can cause bugcheck when the
 * timer fires.
 */
static inline void bsd_timeout_cancel(void *ID)
{
	struct bsd_timeout_wrapper *btw = (struct bsd_timeout_wrapper *)ID;

	if (btw == NULL) {
		dprintf("%s NULL ID is not implemented\n", __func__);
		return;
	}

	if (btw->func != NULL) {
		if (KeCancelTimer(&btw->timer)) {
			dprintf("timer object was loaded.Cancelled it.\n");
		} else {
			dprintf("timer object is not loaded.\n");
		}
	}
}

#endif /* SPL_SYSTM_H */
