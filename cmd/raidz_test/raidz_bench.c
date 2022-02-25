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

#include <sys/zfs_context.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <sys/zio.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <stdio.h>

#include "raidz_test.h"

#define	GEN_BENCH_MEMORY	(((uint64_t)1ULL)<<32)
#define	REC_BENCH_MEMORY	(((uint64_t)1ULL)<<29)
#define	BENCH_ASHIFT		12
#define	MIN_CS_SHIFT		BENCH_ASHIFT
#define	MAX_CS_SHIFT		SPA_MAXBLOCKSHIFT

static zio_t zio_bench;
static raidz_map_t *rm_bench;
static size_t max_data_size = SPA_MAXBLOCKSIZE;

static void
bench_init_raidz_map(void)
{
	zio_bench.io_offset = 0;
	zio_bench.io_size = max_data_size;

	/*
	 * To permit larger column sizes these have to be done
	 * allocated using aligned alloc instead of zio_abd_buf_alloc
	 */
	zio_bench.io_abd = raidz_alloc(max_data_size);

	init_zio_abd(&zio_bench);
}

static void
bench_fini_raidz_maps(void)
{
	/* tear down golden zio */
	raidz_free(zio_bench.io_abd, max_data_size);
	memset(&zio_bench, 0, sizeof (zio_t));
}

static inline void
run_gen_bench_impl(const char *impl)
{
	int fn, ncols;
	uint64_t ds, iter_cnt, iter, disksize;
	hrtime_t start;
	double elapsed, d_bw;

	/* Benchmark generate functions */
	for (fn = 0; fn < RAIDZ_GEN_NUM; fn++) {

		for (ds = MIN_CS_SHIFT; ds <= MAX_CS_SHIFT; ds++) {
			/* create suitable raidz_map */
			ncols = rto_opts.rto_dcols + fn + 1;
			zio_bench.io_size = 1ULL << ds;

			if (rto_opts.rto_expand) {
				rm_bench = vdev_raidz_map_alloc_expanded(
				    zio_bench.io_abd,
				    zio_bench.io_size, zio_bench.io_offset,
				    rto_opts.rto_ashift, ncols+1, ncols,
				    fn+1, rto_opts.rto_expand_offset);
			} else {
				rm_bench = vdev_raidz_map_alloc(&zio_bench,
				    BENCH_ASHIFT, ncols, fn+1);
			}

			/* estimate iteration count */
			iter_cnt = GEN_BENCH_MEMORY;
			iter_cnt /= zio_bench.io_size;

			start = gethrtime();
			for (iter = 0; iter < iter_cnt; iter++)
				vdev_raidz_generate_parity(rm_bench);
			elapsed = NSEC2SEC((double)(gethrtime() - start));

			disksize = (1ULL << ds) / rto_opts.rto_dcols;
			d_bw = (double)iter_cnt * (double)disksize;
			d_bw /= (1024.0 * 1024.0 * elapsed);

			LOG(D_ALL, "%10s, %8s, %zu, %10llu, %lf, %lf, %u\n",
			    impl,
			    raidz_gen_name[fn],
			    rto_opts.rto_dcols,
			    (1ULL<<ds),
			    d_bw,
			    d_bw * (double)(ncols),
			    (unsigned)iter_cnt);

			vdev_raidz_map_free(rm_bench);
		}
	}
}

static void
run_gen_bench(void)
{
	char **impl_name;

	LOG(D_INFO, DBLSEP "\nBenchmarking parity generation...\n\n");
	LOG(D_ALL, "impl, math, dcols, iosize, disk_bw, total_bw, iter\n");

	for (impl_name = (char **)raidz_impl_names; *impl_name != NULL;
	    impl_name++) {

		if (vdev_raidz_impl_set(*impl_name) != 0)
			continue;

		run_gen_bench_impl(*impl_name);
	}
}

static void
run_rec_bench_impl(const char *impl)
{
	int fn, ncols, nbad;
	uint64_t ds, iter_cnt, iter, disksize;
	hrtime_t start;
	double elapsed, d_bw;
	static const int tgt[7][3] = {
		{1, 2, 3},	/* rec_p:   bad QR & D[0]	*/
		{0, 2, 3},	/* rec_q:   bad PR & D[0]	*/
		{0, 1, 3},	/* rec_r:   bad PQ & D[0]	*/
		{2, 3, 4},	/* rec_pq:  bad R  & D[0][1]	*/
		{1, 3, 4},	/* rec_pr:  bad Q  & D[0][1]	*/
		{0, 3, 4},	/* rec_qr:  bad P  & D[0][1]	*/
		{3, 4, 5}	/* rec_pqr: bad    & D[0][1][2] */
	};

	for (fn = 0; fn < RAIDZ_REC_NUM; fn++) {
		for (ds = MIN_CS_SHIFT; ds <= MAX_CS_SHIFT; ds++) {

			/* create suitable raidz_map */
			ncols = rto_opts.rto_dcols + PARITY_PQR;
			zio_bench.io_size = 1ULL << ds;

			/*
			 * raidz block is too short to test
			 * the requested method
			 */
			if (zio_bench.io_size / rto_opts.rto_dcols <
			    (1ULL << BENCH_ASHIFT))
				continue;

			if (rto_opts.rto_expand) {
				rm_bench = vdev_raidz_map_alloc_expanded(
				    zio_bench.io_abd,
				    zio_bench.io_size, zio_bench.io_offset,
				    BENCH_ASHIFT, ncols+1, ncols,
				    PARITY_PQR, rto_opts.rto_expand_offset);
			} else {
				rm_bench = vdev_raidz_map_alloc(&zio_bench,
				    BENCH_ASHIFT, ncols, PARITY_PQR);
			}

			/* estimate iteration count */
			iter_cnt = (REC_BENCH_MEMORY);
			iter_cnt /= zio_bench.io_size;

			/* calculate how many bad columns there are */
			nbad = MIN(3, raidz_ncols(rm_bench) -
			    raidz_parity(rm_bench));

			start = gethrtime();
			for (iter = 0; iter < iter_cnt; iter++)
				vdev_raidz_reconstruct(rm_bench, tgt[fn], nbad);
			elapsed = NSEC2SEC((double)(gethrtime() - start));

			disksize = (1ULL << ds) / rto_opts.rto_dcols;
			d_bw = (double)iter_cnt * (double)(disksize);
			d_bw /= (1024.0 * 1024.0 * elapsed);

			LOG(D_ALL, "%10s, %8s, %zu, %10llu, %lf, %lf, %u\n",
			    impl,
			    raidz_rec_name[fn],
			    rto_opts.rto_dcols,
			    (1ULL<<ds),
			    d_bw,
			    d_bw * (double)ncols,
			    (unsigned)iter_cnt);

			vdev_raidz_map_free(rm_bench);
		}
	}
}

static void
run_rec_bench(void)
{
	char **impl_name;

	LOG(D_INFO, DBLSEP "\nBenchmarking data reconstruction...\n\n");
	LOG(D_ALL, "impl, math, dcols, iosize, disk_bw, total_bw, iter\n");

	for (impl_name = (char **)raidz_impl_names; *impl_name != NULL;
	    impl_name++) {

		if (vdev_raidz_impl_set(*impl_name) != 0)
			continue;

		run_rec_bench_impl(*impl_name);
	}
}

void
run_raidz_benchmark(void)
{
	bench_init_raidz_map();

	run_gen_bench();
	run_rec_bench();

	bench_fini_raidz_maps();
}
