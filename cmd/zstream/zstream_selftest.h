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
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef	_ZSTREAM_SELFTEST_H
#define	_ZSTREAM_SELFTEST_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>

#include "zstream_alloc.h"

/*
 * Shared harness for "zstream selftest". Each module under test supplies a
 * NULL-terminated array of named test cases. The harness in
 * zstream_selftest.c handles argument parsing, test selection, seeding of
 * pseudo-random number generators, watchdog timeouts, and status output.
 *
 * Test cases report failure by exiting with a nonzero exit code. Any test
 * case that returns has passed.
 */

typedef void test_function_f(void);

typedef struct {
	const char	*tc_name;
	test_function_f	*tc_func;
} test_case_t;

/*
 * Modules with test cases to offer. Each array ends with a NULL tc_name.
 */
extern const test_case_t selftest_queue_cases[];
extern const test_case_t selftest_alloc_cases[];
extern const test_case_t selftest_hash_cases[];

/*
 * Directory for disk-backed test scratch storage. Honors the
 * ZSTREAM_SELFTEST_DIR environment variable if present and otherwise
 * defaults to /var/tmp, which unlike /tmp is unlikely to be a ramdisk;
 * memory-spillover tests are only meaningful on real storage.
 */
const char *
selftest_scratch_dir(void);

/*
 * Run fn(arg) in a forked child and require it to terminate either by
 * exiting with a nonzero status (err(), errx()) or by aborting (a failed
 * VERIFY). The child's stdout and stderr are captured.
 *
 * If the expect argument is non-NULL, the output must contain at least one
 * of the '|'-separated alternatives. The test fails under any of the
 * following conditions:
 *
 * - The child exits with status 0
 * - The child dies because of a signal other than SIGABRT
 * - The child runs longer than EXPECT_EXIT_TIMEOUT_SECS
 * - The "expect" pattern does not match the child's output
 *
 * Since the child process is forked, fn() must not depend on other
 * threads, e.g., zstream_queue or zstream_chain workers.
 */
#define	EXPECT_EXIT_TIMEOUT_SECS	30

typedef void selftest_child_f(void *arg);

void
selftest_expect_exit(const char *what, selftest_child_f *fn, void *arg,
    const char *expect);

/*
 * The converse of selftest_expect_exit(): fn(arg) runs in a forked child
 * that must return normally (exit status 0) and, if expect is non-NULL,
 * print a matching message. This is for tests that change process-wide
 * state, such as resource limits, that the rest of the run must not
 * inherit. The same restrictions on threads apply.
 */
void
selftest_expect_success(const char *what, selftest_child_f *fn, void *arg,
    const char *expect);

/*
 * The seed for this run, set by the harness before any test runs. Printed
 * at startup and settable with -s so failures can be replayed.
 */
extern uint64_t selftest_seed;

/*
 * A small, deterministic PRNG (splitmix64). Tests derive per-thread
 * generators from selftest_seed plus a caller-chosen stream number, so
 * workloads are reproducible for a given seed regardless of scheduling.
 */
typedef struct {
	uint64_t	sr_state;
} selftest_rng_t;

static inline uint64_t
selftest_mix64(uint64_t z)
{
	z += 0x9e3779b97f4a7c15ULL;
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
	z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
	return (z ^ (z >> 31));
}

static inline void
selftest_rng_init(selftest_rng_t *rng, uint64_t stream)
{
	rng->sr_state = selftest_seed ^ selftest_mix64(stream);
}

static inline uint64_t
selftest_rng_next(selftest_rng_t *rng)
{
	rng->sr_state += 0x9e3779b97f4a7c15ULL;
	return (selftest_mix64(rng->sr_state));
}

/* Uniform value in [0, bound); returns 0 if bound is 0 */
static inline uint64_t
selftest_rng_below(selftest_rng_t *rng, uint64_t bound)
{
	return (bound ? selftest_rng_next(rng) % bound : 0);
}

/*
 * Fill a buffer with bytes that are a function of selftest_seed and tag
 * alone, so a model need only remember the tag to regenerate the contents.
 */
static inline void
selftest_fill(uint8_t *buf, size_t len, uint64_t tag)
{
	uint64_t x = selftest_mix64(selftest_seed ^ tag);
	for (size_t i = 0; i < len; i++) {
		if ((i & 7) == 0)
			x = selftest_mix64(x + i);
		buf[i] = (uint8_t)(x >> ((i & 7) << 3));
	}
}

/*
 * Check the invariants of the allocator group that member belongs to,
 * using only the group's own state: exact memory accounting, cluster
 * alignment, split points within bounds, backing consistent with the
 * group's configuration, and allocator_get_stats() consistent with all of
 * it. Shared by the allocator and hash suites; exits on failure.
 */
void
selftest_audit_group(allocator_t *member);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZSTREAM_SELFTEST_H */
