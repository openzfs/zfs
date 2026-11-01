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
 * Copyright (c) 2026, Tigerblue77.
 */

/*
 * ZED's FMD timer emulation (cmd/zed/agents/fmd_api.c).  A timer's expiry
 * must reach the module only through fmd_module_timeout(), on the thread
 * that dispatches agent events, and must be dropped once the timer has
 * been removed, even if its notification already ran.
 */

#include <sys/types.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <libnvpair.h>

#include "agents/fmd_api.h"
#include "agents/zfs_agents.h"
#include "zed_log.h"
#include "unit.h"

/* ========== */

/*
 * Stand-ins for the parts of ZED that fmd_api.c calls.  Posted events are
 * kept so that a test can dispatch them itself, as the agents thread does.
 */

#define	MAX_EVENTS	8

static pthread_mutex_t ev_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ev_cv = PTHREAD_COND_INITIALIZER;
static nvlist_t *ev_queue[MAX_EVENTS];
static int ev_count;

void
zed_log_msg(int priority, const char *fmt, ...)
{
	(void) priority, (void) fmt;
}

void
zed_log_die(const char *fmt, ...)
{
	va_list vargs;

	va_start(vargs, fmt);
	(void) vfprintf(stderr, fmt, vargs);
	va_end(vargs);
	abort();
}

void
zfs_agent_post_event(const char *class, const char *subclass, nvlist_t *nvl)
{
	(void) subclass;

	if (strcmp(class, FMD_TIMEOUT_CLASS) != 0)
		return;

	(void) pthread_mutex_lock(&ev_lock);
	if (ev_count < MAX_EVENTS)
		ev_queue[ev_count++] = fnvlist_dup(nvl);
	(void) pthread_cond_broadcast(&ev_cv);
	(void) pthread_mutex_unlock(&ev_lock);
}

/* Wait up to 10 seconds for a timeout event and take it off the queue. */
static nvlist_t *
wait_event(void)
{
	struct timespec deadline;
	nvlist_t *nvl = NULL;

	(void) clock_gettime(CLOCK_REALTIME, &deadline);
	deadline.tv_sec += 10;

	(void) pthread_mutex_lock(&ev_lock);
	while (ev_count == 0 &&
	    pthread_cond_timedwait(&ev_cv, &ev_lock, &deadline) == 0)
		;
	if (ev_count > 0) {
		nvl = ev_queue[0];
		memmove(&ev_queue[0], &ev_queue[1],
		    --ev_count * sizeof (nvlist_t *));
	}
	(void) pthread_mutex_unlock(&ev_lock);

	return (nvl);
}

static void
dispatch(nvlist_t *nvl)
{
	fmd_module_timeout(nvl);
	nvlist_free(nvl);
}

/* ========== */

/* Calls of the module's fmdo_timeout, and those off the dispatcher */
static int timeout_calls;
static int offthread_calls;
static fmd_timer_t *timeout_id;
static void *timeout_arg;
static pthread_t dispatcher;

static void
mod_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	(void) hdl, (void) ep, (void) nvl, (void) class;
}

static void
mod_timeout(fmd_hdl_t *hdl, id_t id, void *arg)
{
	(void) hdl;

	(void) pthread_mutex_lock(&ev_lock);
	timeout_calls++;
	if (!pthread_equal(pthread_self(), dispatcher))
		offthread_calls++;
	timeout_id = id;
	timeout_arg = arg;
	(void) pthread_mutex_unlock(&ev_lock);
}

/*
 * No notification thread may call the module.  Give one that wrongly does
 * time to finish before the count is checked.
 */
static int
offthread_count(void)
{
	int n;

	(void) usleep(100000);
	(void) pthread_mutex_lock(&ev_lock);
	n = offthread_calls;
	(void) pthread_mutex_unlock(&ev_lock);

	return (n);
}

static const fmd_hdl_ops_t mod_ops = {
	mod_recv,	/* fmdo_recv */
	mod_timeout,	/* fmdo_timeout */
	NULL,		/* fmdo_close */
	NULL,		/* fmdo_stats */
	NULL,		/* fmdo_gc */
};

static const fmd_prop_t mod_props[] = {
	{ NULL, 0, NULL }
};

static const fmd_hdl_info_t mod_info = {
	"ZFS Timer Test", "1.0", &mod_ops, mod_props
};

static fmd_hdl_t *
mod_register(void)
{
	fmd_hdl_t *hdl = fmd_module_hdl("zfs-diagnosis");

	unit_ok(fmd_hdl_register(hdl, FMD_API_VERSION, &mod_info));
	timeout_calls = 0;
	offthread_calls = 0;
	timeout_id = NULL;
	timeout_arg = NULL;
	dispatcher = pthread_self();

	return (hdl);
}

/* ========== */

/* The module is called when the event is dispatched, and not before. */
static MunitResult
test_fmd_timer_fires_on_dispatch(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *ftp;
	nvlist_t *nvl;
	int arg;

	(void) params, (void) data;

	ftp = fmd_timer_install(hdl, &arg, NULL, MSEC2NSEC(1));
	nvl = wait_event();
	unit_notnull(nvl);
	unit_zero(timeout_calls);

	dispatch(nvl);
	unit_eq(timeout_calls, 1);
	unit_true(timeout_id == ftp);
	unit_true(timeout_arg == &arg);

	fmd_timer_remove(hdl, ftp);
	fmd_hdl_unregister(hdl);
	unit_zero(offthread_count());

	return (MUNIT_OK);
}

/* A timer removed after its notification has run is not delivered. */
static MunitResult
test_fmd_timer_removed(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *ftp;
	nvlist_t *nvl;

	(void) params, (void) data;

	ftp = fmd_timer_install(hdl, NULL, NULL, MSEC2NSEC(1));
	nvl = wait_event();
	unit_notnull(nvl);

	fmd_timer_remove(hdl, ftp);
	dispatch(nvl);
	unit_zero(timeout_calls);

	fmd_hdl_unregister(hdl);
	unit_zero(offthread_count());

	return (MUNIT_OK);
}

/*
 * A timer installed after its predecessor expired and was removed, as
 * zfs_fm_recv() re-arms zc_remove_timer, is not fired by the
 * predecessor's event.
 */
static MunitResult
test_fmd_timer_rearmed(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *ftp;
	nvlist_t *nvl;

	(void) params, (void) data;

	ftp = fmd_timer_install(hdl, NULL, NULL, MSEC2NSEC(1));
	nvl = wait_event();
	unit_notnull(nvl);

	fmd_timer_remove(hdl, ftp);
	ftp = fmd_timer_install(hdl, NULL, NULL, SEC2NSEC(3600));
	dispatch(nvl);
	unit_zero(timeout_calls);

	fmd_timer_remove(hdl, ftp);
	fmd_hdl_unregister(hdl);
	unit_zero(offthread_count());

	return (MUNIT_OK);
}

/* Unregistering a module removes the timers it left installed. */
static MunitResult
test_fmd_timer_unregister(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	nvlist_t *nvl;

	(void) params, (void) data;

	(void) fmd_timer_install(hdl, NULL, NULL, SEC2NSEC(3600));
	(void) fmd_timer_install(hdl, NULL, NULL, MSEC2NSEC(1));
	nvl = wait_event();
	unit_notnull(nvl);

	fmd_hdl_unregister(hdl);
	dispatch(nvl);
	unit_zero(timeout_calls);
	unit_zero(offthread_count());

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest fmd_timer_tests[] = {
	UNIT_TEST("fires_on_dispatch",	test_fmd_timer_fires_on_dispatch),
	UNIT_TEST("removed",		test_fmd_timer_removed),
	UNIT_TEST("rearmed",		test_fmd_timer_rearmed),
	UNIT_TEST("unregister",		test_fmd_timer_unregister),
	{ 0 },
};

static const MunitSuite fmd_timer_test_suite = {
	"fmd_timer.",
	fmd_timer_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&fmd_timer_test_suite, NULL, argc, argv));
}
