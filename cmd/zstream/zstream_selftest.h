// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the Common
 * Development and Distribution License ("CDDL"), version 1.0. You may only use
 * this file in accordance with the terms of version 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this source. A
 * copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef	_ZSTREAM_SELFTEST_H
#define	_ZSTREAM_SELFTEST_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <sys/stdtypes.h>

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

/*
 * The seed for this run, set by the harness before any test runs. Printed
 * at startup and settable with -s so failures can be replayed.
 */
extern uint64_t selftest_seed;

/*
 * A small deterministic PRNG (splitmix64). Tests derive per-thread
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

#ifdef	__cplusplus
}
#endif

#endif	/* _ZSTREAM_SELFTEST_H */
