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

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/time.h>
#include <sys/timer.h>
#include <sys/mutex.h>
#include <sys/condvar.h>
#include <sys/taskq.h>
#include <errno.h>

#include "unit.h"

/* ========== */

/*
 * taskq_cancel_id() must behave as it does in the kernel: a pending task
 * is removed and never runs (0), a running task is waited for (ENOENT) or
 * reported (EBUSY), and a finished or unknown task returns ENOENT.
 *
 * Each test uses a taskq with a single thread, which a first task keeps
 * busy, so that the tasks dispatched after it stay pending.
 */

typedef struct {
	kmutex_t	tt_lock;
	kcondvar_t	tt_cv;
	boolean_t	tt_started;	/* blocking task is running */
	boolean_t	tt_release;	/* blocking task may return */
	int		tt_done;	/* blocking task has returned */
} taskq_test_t;

static void
tt_init(taskq_test_t *tt)
{
	mutex_init(&tt->tt_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&tt->tt_cv, NULL, CV_DEFAULT, NULL);
	tt->tt_started = B_FALSE;
	tt->tt_release = B_FALSE;
	tt->tt_done = 0;
}

static void
tt_fini(taskq_test_t *tt)
{
	cv_destroy(&tt->tt_cv);
	mutex_destroy(&tt->tt_lock);
}

/* Runs until tt_release is set. */
static void
block_task(void *arg)
{
	taskq_test_t *tt = arg;

	mutex_enter(&tt->tt_lock);
	tt->tt_started = B_TRUE;
	cv_broadcast(&tt->tt_cv);
	while (!tt->tt_release)
		cv_wait(&tt->tt_cv, &tt->tt_lock);
	tt->tt_done++;
	mutex_exit(&tt->tt_lock);
}

/*
 * Returns on its own after a fixed time, so that the test can block in
 * taskq_cancel_id() while it runs.
 */
static void
sleep_task(void *arg)
{
	taskq_test_t *tt = arg;

	mutex_enter(&tt->tt_lock);
	tt->tt_started = B_TRUE;
	cv_broadcast(&tt->tt_cv);
	mutex_exit(&tt->tt_lock);

	usleep_range(200000, 200100);

	mutex_enter(&tt->tt_lock);
	tt->tt_done++;
	mutex_exit(&tt->tt_lock);
}

/*
 * Records that it ran.  The caller reads the flag after taskq_wait(),
 * which orders it after the write through the taskq lock.
 */
static void
mark_task(void *arg)
{
	int *ran = arg;

	*ran = 1;
}

static void
wait_started(taskq_test_t *tt)
{
	mutex_enter(&tt->tt_lock);
	while (!tt->tt_started)
		cv_wait(&tt->tt_cv, &tt->tt_lock);
	mutex_exit(&tt->tt_lock);
}

static void
release(taskq_test_t *tt)
{
	mutex_enter(&tt->tt_lock);
	tt->tt_release = B_TRUE;
	cv_broadcast(&tt->tt_cv);
	mutex_exit(&tt->tt_lock);
}

static int
done_count(taskq_test_t *tt)
{
	int done;

	mutex_enter(&tt->tt_lock);
	done = tt->tt_done;
	mutex_exit(&tt->tt_lock);
	return (done);
}

/*
 * Dispatch a task that occupies the only thread of a new taskq, and wait
 * until it runs.
 */
static taskq_t *
busy_taskq(taskq_test_t *tt, task_func_t *func, taskqid_t *idp)
{
	taskq_t *tq = taskq_create("test_taskq", 1, minclsyspri, 0, INT_MAX,
	    0);
	unit_notnull(tq);

	tt_init(tt);
	*idp = taskq_dispatch(tq, func, tt, TQ_SLEEP);
	unit_ne(*idp, TASKQID_INVALID);
	wait_started(tt);

	return (tq);
}

static void
cancel_pending(boolean_t wait)
{
	taskq_test_t tt;
	taskqid_t bid;
	int ran1 = 0;
	int ran2 = 0;
	taskq_t *tq = busy_taskq(&tt, block_task, &bid);

	taskqid_t id1 = taskq_dispatch(tq, mark_task, &ran1, TQ_SLEEP);
	taskqid_t id2 = taskq_dispatch(tq, mark_task, &ran2, TQ_SLEEP);
	unit_ne(id1, TASKQID_INVALID);
	unit_ne(id2, TASKQID_INVALID);
	unit_ne(id1, id2);

	/* Removed at once, without waiting for the blocking task. */
	unit_ok(taskq_cancel_id(tq, id1, wait));
	unit_err(taskq_cancel_id(tq, id1, wait), ENOENT);

	release(&tt);
	taskq_wait(tq);

	/* The cancelled task never ran; the other one did. */
	unit_eq(ran1, 0);
	unit_eq(ran2, 1);
	unit_eq(done_count(&tt), 1);

	taskq_destroy(tq);
	tt_fini(&tt);
}

/* A pending task is removed and its function never called. */
static MunitResult
test_cancel_pending_nowait(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	cancel_pending(B_FALSE);

	return (MUNIT_OK);
}

/* With wait set, a pending task is still removed without waiting. */
static MunitResult
test_cancel_pending_wait(const MunitParameter params[], void *data)
{
	(void) params, (void) data;

	cancel_pending(B_TRUE);

	return (MUNIT_OK);
}

/* A running task is reported busy, and left to finish on its own. */
static MunitResult
test_cancel_running_nowait(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	taskq_test_t tt;
	taskqid_t id;
	taskq_t *tq = busy_taskq(&tt, block_task, &id);

	unit_err(taskq_cancel_id(tq, id, B_FALSE), EBUSY);
	unit_eq(done_count(&tt), 0);

	release(&tt);
	taskq_wait(tq);
	unit_eq(done_count(&tt), 1);
	unit_err(taskq_cancel_id(tq, id, B_FALSE), ENOENT);

	taskq_destroy(tq);
	tt_fini(&tt);

	return (MUNIT_OK);
}

/* With wait set, the caller blocks until a running task has returned. */
static MunitResult
test_cancel_running_wait(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	taskq_test_t tt;
	taskqid_t id;
	taskq_t *tq = busy_taskq(&tt, sleep_task, &id);

	unit_err(taskq_cancel_id(tq, id, B_TRUE), ENOENT);
	unit_eq(done_count(&tt), 1);

	taskq_destroy(tq);
	tt_fini(&tt);

	return (MUNIT_OK);
}

/* A task that has already run is not found. */
static MunitResult
test_cancel_done(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	taskq_test_t tt;
	taskqid_t id;
	taskq_t *tq = busy_taskq(&tt, block_task, &id);

	release(&tt);
	taskq_wait(tq);

	unit_err(taskq_cancel_id(tq, id, B_FALSE), ENOENT);
	unit_err(taskq_cancel_id(tq, id, B_TRUE), ENOENT);
	unit_err(taskq_cancel_id(tq, TASKQID_INVALID, B_TRUE), ENOENT);

	taskq_destroy(tq);
	tt_fini(&tt);

	return (MUNIT_OK);
}

/* ========== */

static const MunitTest taskq_tests[] = {
	UNIT_TEST("cancel_pending_nowait",	test_cancel_pending_nowait),
	UNIT_TEST("cancel_pending_wait",	test_cancel_pending_wait),
	UNIT_TEST("cancel_running_nowait",	test_cancel_running_nowait),
	UNIT_TEST("cancel_running_wait",	test_cancel_running_wait),
	UNIT_TEST("cancel_done",		test_cancel_done),
	{ 0 },
};

static const MunitSuite taskq_test_suite = {
	"taskq.",
	taskq_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&taskq_test_suite, NULL, argc, argv));
}
