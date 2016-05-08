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

/*
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

#ifndef	RAIDZ_TEST_H
#define	RAIDZ_TEST_H

#include <sys/spa.h>

#define	MIN_ASHIFT	9
#define	MAX_ASHIFT	12

#define	MIN_OFFSET	8
#define	MAX_OFFSET	12

#define	MIN_DCOLS	1
#define	MAX_DCOLS	8

#define	MIN_DCSIZE	9
#define	MAX_DCSIZE	23

typedef struct raidz_test_opts {
	size_t rto_ashift;
	size_t rto_offset;
	size_t rto_dcols;
	size_t rto_dsize;
	size_t rto_v;
	size_t rto_sweep;
	size_t rto_sweep_timeout;
	size_t rto_benchmark;
	size_t rto_sanity;
	size_t rto_gdb;

	zio_t *zio_golden;
	raidz_map_t *rm_golden;
} raidz_test_opts_t;

static const raidz_test_opts_t rto_opts_defaults = {
	.rto_ashift = 9,
	.rto_offset = 1ULL << 0,
	.rto_dcols = 8,
	.rto_dsize = 1<<19,
	.rto_v = 0,
	.rto_sweep = 0,
	.rto_benchmark = 0,
	.rto_sanity = 0,
	.rto_gdb = 0
};

extern raidz_test_opts_t rto_opts;

static inline size_t ilog2(size_t a)
{
	return (a > 1 ? 1 + ilog2(a >> 1) : 0);
}

#define	PRINT(a...) (void) fprintf(stderr, a)
#define	IPRINT(a...)	if (rto_opts.rto_v >= 1) PRINT(a)
#define	DPRINT(a...)	if (rto_opts.rto_v >= 2) PRINT(a)
#define	DBLSEP "================\n"
#define	SEP    "----------------\n"


void init_zio_data(zio_t *zio);

void run_raidz_benchmark(void);

#endif /* RAIDZ_TEST_H */
