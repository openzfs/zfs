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
 * ZED's FMD timer emulation (cmd/zed/agents/fmd_api.c).  An expired timer
 * must reach its module only through fmd_timer_expire(), which the agents
 * thread calls between two events, and never from another thread.
 */

#include <sys/types.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libnvpair.h>

#include "agents/fmd_api.h"
#include "agents/zfs_agents.h"
#include "zed_log.h"
#include "unit.h"

/* ========== */

/* Stand-ins for the parts of ZED that fmd_api.c calls. */

/* Messages logged at LOG_ERR or above */
static int err_msgs;

void
zed_log_msg(int priority, const char *fmt, ...)
{
	(void) fmt;

	if (priority <= LOG_ERR)
		err_msgs++;
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
	(void) class, (void) subclass, (void) nvl;
}

/* ========== */

#define	MAX_CALLS	8

/* The module's fmdo_timeout calls, in order */
static int ncalls;
static fmd_timer_t *call_id[MAX_CALLS];
static void *call_arg[MAX_CALLS];

/* What fmdo_timeout does besides recording the call */
static boolean_t remove_in_timeout;	/* remove the timer, each time */
static boolean_t install_in_timeout;	/* install an expired one, once */

static void
mod_recv(fmd_hdl_t *hdl, fmd_event_t *ep, nvlist_t *nvl, const char *class)
{
	(void) hdl, (void) ep, (void) nvl, (void) class;
}

static void
mod_timeout(fmd_hdl_t *hdl, id_t id, void *arg)
{
	if (ncalls < MAX_CALLS) {
		call_id[ncalls] = id;
		call_arg[ncalls] = arg;
	}
	ncalls++;

	if (remove_in_timeout)
		fmd_timer_remove(hdl, id);
	if (install_in_timeout) {
		install_in_timeout = B_FALSE;
		(void) fmd_timer_install(hdl, NULL, NULL, 0);
	}
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
	ncalls = 0;
	err_msgs = 0;
	remove_in_timeout = B_FALSE;
	install_in_timeout = B_FALSE;

	return (hdl);
}

/* ========== */

/*
 * Nothing calls the module when a timer expires: only fmd_timer_expire()
 * does, from the thread that calls it, once.
 */
static MunitResult
test_fmd_timer_no_async_call(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *ftp;
	hrtime_t next;
	int arg;

	(void) params, (void) data;

	ftp = fmd_timer_install(hdl, &arg, NULL, MSEC2NSEC(1));
	(void) usleep(100000);
	unit_zero(ncalls);

	unit_false(fmd_timer_expire(&next));
	unit_eq(ncalls, 1);
	unit_true(call_id[0] == ftp);
	unit_true(call_arg[0] == &arg);

	/* it stays installed, and does not expire again */
	unit_false(fmd_timer_expire(&next));
	unit_eq(ncalls, 1);
	fmd_timer_remove(hdl, ftp);
	unit_zero(err_msgs);

	fmd_hdl_unregister(hdl);

	return (MUNIT_OK);
}

/* A timer that has not expired yet gives the time to wait for. */
static MunitResult
test_fmd_timer_next(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *later;
	fmd_timer_t *sooner;
	hrtime_t before;
	hrtime_t next;

	(void) params, (void) data;

	before = gethrtime();
	later = fmd_timer_install(hdl, NULL, NULL, SEC2NSEC(7200));
	sooner = fmd_timer_install(hdl, NULL, NULL, SEC2NSEC(3600));

	unit_true(fmd_timer_expire(&next));
	unit_zero(ncalls);
	unit_true(next >= before + SEC2NSEC(3600));
	unit_true(next <= gethrtime() + SEC2NSEC(3600));

	fmd_timer_remove(hdl, sooner);
	unit_true(fmd_timer_expire(&next));
	unit_true(next >= before + SEC2NSEC(7200));

	fmd_timer_remove(hdl, later);
	unit_false(fmd_timer_expire(&next));
	unit_zero(ncalls);

	fmd_hdl_unregister(hdl);

	return (MUNIT_OK);
}

/* The expired timers fire earliest first; the others wait. */
static MunitResult
test_fmd_timer_order(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *second;
	fmd_timer_t *first;
	fmd_timer_t *third;
	hrtime_t next;

	(void) params, (void) data;

	second = fmd_timer_install(hdl, NULL, NULL, MSEC2NSEC(200));
	third = fmd_timer_install(hdl, NULL, NULL, SEC2NSEC(3600));
	first = fmd_timer_install(hdl, NULL, NULL, MSEC2NSEC(1));
	(void) usleep(300000);

	unit_true(fmd_timer_expire(&next));
	unit_eq(ncalls, 2);
	unit_true(call_id[0] == first);
	unit_true(call_id[1] == second);
	unit_true(next == third->ft_expire);

	fmd_hdl_unregister(hdl);

	return (MUNIT_OK);
}

/*
 * A module can remove the expired timer and install another one from its
 * fmdo_timeout, as zfs_diagnosis does when the timeout closes its case.
 */
static MunitResult
test_fmd_timer_remove_in_timeout(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	hrtime_t next;

	(void) params, (void) data;

	(void) fmd_timer_install(hdl, NULL, NULL, 0);
	(void) fmd_timer_install(hdl, NULL, NULL, 0);
	remove_in_timeout = B_TRUE;
	install_in_timeout = B_TRUE;

	/* the timer installed by the first call has expired too */
	unit_false(fmd_timer_expire(&next));
	unit_eq(ncalls, 3);
	unit_zero(err_msgs);

	fmd_hdl_unregister(hdl);

	return (MUNIT_OK);
}

/* A timer removed before it expires is never delivered. */
static MunitResult
test_fmd_timer_removed(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	fmd_timer_t *ftp;
	hrtime_t next;

	(void) params, (void) data;

	ftp = fmd_timer_install(hdl, NULL, NULL, MSEC2NSEC(1));
	fmd_timer_remove(hdl, ftp);
	(void) usleep(10000);

	unit_false(fmd_timer_expire(&next));
	unit_zero(ncalls);

	/* removing it again is an error that is logged */
	fmd_timer_remove(hdl, ftp);
	unit_eq(err_msgs, 1);

	fmd_hdl_unregister(hdl);

	return (MUNIT_OK);
}

/*
 * Unregistering a module removes the timers it left installed, so that none
 * of them reaches the module once it is registered again.
 */
static MunitResult
test_fmd_timer_unregister(const MunitParameter params[], void *data)
{
	fmd_hdl_t *hdl = mod_register();
	hrtime_t next;

	(void) params, (void) data;

	(void) fmd_timer_install(hdl, NULL, NULL, SEC2NSEC(3600));
	(void) fmd_timer_install(hdl, NULL, NULL, 0);
	fmd_hdl_unregister(hdl);

	hdl = mod_register();
	unit_false(fmd_timer_expire(&next));
	unit_zero(ncalls);
	fmd_hdl_unregister(hdl);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest fmd_timer_tests[] = {
	UNIT_TEST("no_async_call",	test_fmd_timer_no_async_call),
	UNIT_TEST("next",		test_fmd_timer_next),
	UNIT_TEST("order",		test_fmd_timer_order),
	UNIT_TEST("remove_in_timeout",	test_fmd_timer_remove_in_timeout),
	UNIT_TEST("removed",		test_fmd_timer_removed),
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
