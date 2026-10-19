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

#include <sys/types.h>
#include <sys/condvar.h>
#include <sys/debug.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/rwlock.h>
#include <sys/timer.h>
#include <string.h>

#include "unit.h"

static void
barrier_wait(pthread_barrier_t *barrier)
{
	int error = pthread_barrier_wait(barrier);

	VERIFY(error == 0 || error == PTHREAD_BARRIER_SERIAL_THREAD);
}

static MunitResult
test_mutex_held(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	kmutex_t lock;

	mutex_init(&lock, NULL, MUTEX_DEFAULT, NULL);
	unit_false(MUTEX_HELD(&lock));
	mutex_enter(&lock);
	unit_true(MUTEX_HELD(&lock));
	mutex_exit(&lock);
	unit_false(MUTEX_HELD(&lock));
	unit_true(mutex_tryenter(&lock));
	unit_true(MUTEX_HELD(&lock));
	mutex_exit(&lock);
	unit_ok(mutex_enter_check_return(&lock));
	unit_true(MUTEX_HELD(&lock));
	mutex_exit(&lock);
	unit_false(MUTEX_HELD(&lock));
	mutex_destroy(&lock);

	return (MUNIT_OK);
}

static const MunitParameterEnum rwlock_params[] = {
	UNIT_PARAM("mode", "reader", "writer"),
	{ 0 },
};

static MunitResult
test_rwlock_held(const MunitParameter params[], void *data)
{
	(void) data;
	boolean_t reader =
	    strcmp(munit_parameters_get(params, "mode"), "reader") == 0;
	krw_t mode = reader ? RW_READER : RW_WRITER;
	krwlock_t lock;

	rw_init(&lock, NULL, RW_DEFAULT, NULL);
	unit_false(RW_LOCK_HELD(&lock));
	rw_enter(&lock, mode);
	unit_eq(!!RW_READ_HELD(&lock), reader);
	unit_eq(!!RW_WRITE_HELD(&lock), !reader);
	rw_exit(&lock);
	unit_false(RW_LOCK_HELD(&lock));
	unit_true(rw_tryenter(&lock, mode));
	unit_eq(!!RW_READ_HELD(&lock), reader);
	unit_eq(!!RW_WRITE_HELD(&lock), !reader);
	rw_exit(&lock);
	unit_false(RW_LOCK_HELD(&lock));
	rw_destroy(&lock);

	return (MUNIT_OK);
}

typedef struct rwlock_test {
	krwlock_t lock;
	pthread_barrier_t start;
} rwlock_test_t;

static void *
rwlock_reader(void *arg)
{
	rwlock_test_t *test = arg;
	boolean_t held = B_TRUE;

	barrier_wait(&test->start);
	for (int i = 0; i < 1000; i++) {
		rw_enter(&test->lock, RW_READER);
		held &= RW_READ_HELD(&test->lock);
		held &= !RW_WRITE_HELD(&test->lock);
		rw_exit(&test->lock);
	}
	return ((void *)(uintptr_t)held);
}

static MunitResult
test_rwlock_readers(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	rwlock_test_t test;
	pthread_t thread;
	void *other;

	rw_init(&test.lock, NULL, RW_DEFAULT, NULL);
	unit_ok(pthread_barrier_init(&test.start, NULL, 2));
	unit_ok(pthread_create(&thread, NULL, rwlock_reader, &test));
	void *held = rwlock_reader(&test);
	unit_ok(pthread_join(thread, &other));
	unit_ok(pthread_barrier_destroy(&test.start));
	boolean_t unlocked = !RW_LOCK_HELD(&test.lock);
	rw_destroy(&test.lock);
	unit_true((uintptr_t)held);
	unit_true((uintptr_t)other);
	unit_true(unlocked);

	return (MUNIT_OK);
}

typedef struct mutex_test {
	kmutex_t lock;
	pthread_barrier_t start;
} mutex_test_t;

static void *
mutex_owner(void *arg)
{
	mutex_test_t *test = arg;
	boolean_t held = B_TRUE;

	barrier_wait(&test->start);
	for (int i = 0; i < 1000; i++) {
		switch (i % 3) {
		case 0:
			mutex_enter(&test->lock);
			break;
		case 1:
			VERIFY(mutex_tryenter(&test->lock));
			break;
		default:
			VERIFY0(mutex_enter_check_return(&test->lock));
			break;
		}
		held &= MUTEX_HELD(&test->lock);
		mutex_exit(&test->lock);
	}
	return ((void *)(uintptr_t)held);
}

/* Use heap locks for non-owner queries; TSan can elide direct stack reads. */
static MunitResult
test_mutex_nonowner(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	mutex_test_t *test = kmem_alloc(sizeof (*test), KM_SLEEP);
	pthread_t thread;
	void *other;
	boolean_t notheld = B_TRUE;

	mutex_init(&test->lock, NULL, MUTEX_DEFAULT, NULL);
	unit_ok(pthread_barrier_init(&test->start, NULL, 2));
	unit_ok(pthread_create(&thread, NULL, mutex_owner, test));
	barrier_wait(&test->start);
	for (int i = 0; i < 1000; i++)
		notheld &= MUTEX_NOT_HELD(&test->lock);
	unit_ok(pthread_join(thread, &other));
	unit_ok(pthread_barrier_destroy(&test->start));
	mutex_destroy(&test->lock);
	kmem_free(test, sizeof (*test));
	unit_true(notheld);
	unit_true((uintptr_t)other);

	return (MUNIT_OK);
}

static void *
rwlock_writer(void *arg)
{
	rwlock_test_t *test = arg;
	boolean_t held = B_TRUE;

	barrier_wait(&test->start);
	for (int i = 0; i < 1000; i++) {
		if (i % 2 == 0)
			rw_enter(&test->lock, RW_WRITER);
		else
			VERIFY(rw_tryenter(&test->lock, RW_WRITER));
		held &= RW_WRITE_HELD(&test->lock);
		rw_exit(&test->lock);
	}
	return ((void *)(uintptr_t)held);
}

static MunitResult
test_rwlock_nonowner(const MunitParameter params[], void *data)
{
	(void) params, (void) data;
	rwlock_test_t *test = kmem_alloc(sizeof (*test), KM_SLEEP);
	pthread_t thread;
	void *other;
	boolean_t notheld = B_TRUE;

	rw_init(&test->lock, NULL, RW_DEFAULT, NULL);
	unit_ok(pthread_barrier_init(&test->start, NULL, 2));
	unit_ok(pthread_create(&thread, NULL, rwlock_writer, test));
	barrier_wait(&test->start);
	for (int i = 0; i < 1000; i++)
		notheld &= !RW_WRITE_HELD(&test->lock);
	unit_ok(pthread_join(thread, &other));
	unit_ok(pthread_barrier_destroy(&test->start));
	rw_destroy(&test->lock);
	kmem_free(test, sizeof (*test));
	unit_true(notheld);
	unit_true((uintptr_t)other);

	return (MUNIT_OK);
}

typedef struct condvar_test {
	kmutex_t lock;
	kcondvar_t cv;
	boolean_t ready;
} condvar_test_t;

static void *
condvar_signal(void *arg)
{
	condvar_test_t *test = arg;
	boolean_t held;

	mutex_enter(&test->lock);
	held = MUTEX_HELD(&test->lock);
	test->ready = B_TRUE;
	cv_signal(&test->cv);
	mutex_exit(&test->lock);
	for (int i = 0; i < 1000; i++)
		held &= MUTEX_NOT_HELD(&test->lock);
	return ((void *)(uintptr_t)held);
}

static const MunitParameterEnum condvar_params[] = {
	UNIT_PARAM("wait", "untimed", "timed", "hires", "hires_absolute"),
	{ 0 },
};

static MunitResult
test_condvar_held(const MunitParameter params[], void *data)
{
	(void) data;
	const char *wait = munit_parameters_get(params, "wait");
	condvar_test_t test = { 0 };
	pthread_t thread;
	void *other;

	mutex_init(&test.lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&test.cv, NULL, CV_DEFAULT, NULL);
	mutex_enter(&test.lock);
	unit_ok(pthread_create(&thread, NULL, condvar_signal, &test));
	while (!test.ready) {
		if (strcmp(wait, "untimed") == 0) {
			cv_wait(&test.cv, &test.lock);
		} else if (strcmp(wait, "timed") == 0) {
			VERIFY3S(cv_timedwait(&test.cv, &test.lock,
			    ddi_get_lbolt() + 30 * hz), !=, -1);
		} else {
			int flags = strcmp(wait, "hires_absolute") == 0 ?
			    CALLOUT_FLAG_ABSOLUTE : 0;
			hrtime_t deadline = SEC2NSEC(30);
			if (flags != 0)
				deadline += gethrtime();
			VERIFY3S(cv_timedwait_hires(&test.cv, &test.lock,
			    deadline, 1, flags), !=, -1);
		}
	}
	boolean_t held = MUTEX_HELD(&test.lock);
	mutex_exit(&test.lock);
	unit_ok(pthread_join(thread, &other));
	cv_destroy(&test.cv);
	mutex_destroy(&test.lock);
	unit_true(held);
	unit_true((uintptr_t)other);

	return (MUNIT_OK);
}

static const MunitTest lock_tests[] = {
	UNIT_TEST("mutex.held",		test_mutex_held),
	UNIT_TEST("mutex.nonowner",	test_mutex_nonowner),
	UNIT_TEST("rwlock.held",
	    test_rwlock_held, rwlock_params),
	UNIT_TEST("rwlock.readers",	test_rwlock_readers),
	UNIT_TEST("rwlock.nonowner",	test_rwlock_nonowner),
	UNIT_TEST("condvar.held",
	    test_condvar_held, condvar_params),
	{ 0 },
};

static const MunitSuite lock_test_suite = {
	"locks.",
	lock_tests,
	NULL,
	1,
	MUNIT_SUITE_OPTION_NONE,
};

int
main(int argc, char **argv)
{
	return (munit_suite_main(&lock_test_suite, NULL, argc, argv));
}
