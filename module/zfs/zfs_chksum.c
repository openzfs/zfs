// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2021-2022 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <sys/zfs_chksum.h>
#include <sys/zfs_impl.h>

#include <sys/blake3.h>
#include <sys/sha2.h>

/* limit benchmarking to max 256KiB, when EdonR is slower then this: */
#define	LIMIT_PERF_MBS	300

typedef struct {
	const char *name;
	const char *impl;
	uint64_t bs1k;
	uint64_t bs4k;
	uint64_t bs16k;
	uint64_t bs64k;
	uint64_t bs256k;
	uint64_t bs1m;
	uint64_t bs4m;
	uint64_t bs16m;
	zio_cksum_salt_t salt;
	zio_checksum_t *(func);
	zio_checksum_tmpl_init_t *(init);
	zio_checksum_tmpl_free_t *(free);
} chksum_stat_t;

static chksum_stat_t *chksum_stat_data = 0;
static int chksum_stat_cnt = 0;
static kstat_t *chksum_kstat = NULL;

/*
 * Sample output on i3-1005G1 System:
 *
 * implementation   1k      4k     16k     64k    256k      1m      4m     16m
 * edonr-generic  1278    1625    1769    1776    1783    1778    1771    1767
 * skein-generic   548     594     613     623     621     623     621     486
 * sha256-generic  255     270     281     278     279     281     283     283
 * sha256-x64      288     310     316     317     318     317     317     316
 * sha256-ssse3    304     342     351     355     356     357     356     356
 * sha256-avx      311     348     359     362     362     363     363     362
 * sha256-avx2     330     378     389     395     395     395     395     395
 * sha256-shani    908    1127    1212    1230    1233    1234    1223    1230
 * sha512-generic  359     409     431     427     429     430     428     423
 * sha512-x64      420     473     490     496     497     497     496     495
 * sha512-avx      406     522     546     560     560     560     556     560
 * sha512-avx2     464     568     601     606     609     610     607     608
 * blake3-generic  330     327     324     323     324     320     323     322
 * blake3-sse2     424    1366    1449    1468    1458    1453    1395    1408
 * blake3-sse41    453    1554    1658    1703    1689    1669    1622    1630
 * blake3-avx2     452    2013    3225    3351    3356    3261    3076    3101
 * blake3-avx512   498    2869    5269    5926    5872    5643    5014    5005
 */
static int
chksum_kstat_headers(char *buf, size_t size)
{
	ssize_t off = 0;

	off += kmem_scnprintf(buf + off, size, "%-23s", "implementation");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "1k");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "4k");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "16k");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "64k");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "256k");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "1m");
	off += kmem_scnprintf(buf + off, size - off, "%8s", "4m");
	(void) kmem_scnprintf(buf + off, size - off, "%8s\n", "16m");

	return (0);
}

static int
chksum_kstat_data(char *buf, size_t size, void *data)
{
	chksum_stat_t *cs;
	ssize_t off = 0;
	char b[24];

	cs = (chksum_stat_t *)data;
	kmem_scnprintf(b, 23, "%s-%s", cs->name, cs->impl);
	off += kmem_scnprintf(buf + off, size - off, "%-23s", b);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs1k);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs4k);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs16k);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs64k);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs256k);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs1m);
	off += kmem_scnprintf(buf + off, size - off, "%8llu",
	    (u_longlong_t)cs->bs4m);
	(void) kmem_scnprintf(buf + off, size - off, "%8llu\n",
	    (u_longlong_t)cs->bs16m);

	return (0);
}

static void *
chksum_kstat_addr(kstat_t *ksp, loff_t n)
{
	if (n < chksum_stat_cnt)
		ksp->ks_private = (void *)(chksum_stat_data + n);
	else
		ksp->ks_private = NULL;

	return (ksp->ks_private);
}

static void
chksum_run(chksum_stat_t *cs, abd_t *abd, void *ctx, int round,
    uint64_t *result)
{
	hrtime_t start;
	uint64_t run_bw, run_time_ns, run_count = 0, size = 0;
	uint32_t l, loops = 0;
	zio_cksum_t zcp;

	switch (round) {
	case 1: /* 1k */
		size = 1<<10; loops = 128; break;
	case 2: /* 2k */
		size = 1<<12; loops = 64; break;
	case 3: /* 4k */
		size = 1<<14; loops = 32; break;
	case 4: /* 16k */
		size = 1<<16; loops = 16; break;
	case 5: /* 256k */
		size = 1<<18; loops = 8; break;
	case 6: /* 1m */
		size = 1<<20; loops = 4; break;
	case 7: /* 4m */
		size = 1<<22; loops = 1; break;
	case 8: /* 16m */
		size = 1<<24; loops = 1; break;
	}

	kpreempt_disable();
	start = gethrtime();
	do {
		for (l = 0; l < loops; l++, run_count++)
			cs->func(abd, size, ctx, &zcp);

		run_time_ns = gethrtime() - start;
	} while (run_time_ns < MSEC2NSEC(1));
	kpreempt_enable();

	run_bw = size * run_count * NANOSEC;
	run_bw /= run_time_ns;	/* B/s */
	*result = run_bw/1024/1024; /* MiB/s */
}

#define	LIMIT_INIT	0
#define	LIMIT_NEEDED	1
#define	LIMIT_NOLIMIT	2

static void
chksum_benchit(chksum_stat_t *cs)
{
	abd_t *abd;
	void *ctx = 0;
	void *salt = &cs->salt.zcs_bytes;
	static int chksum_stat_limit = LIMIT_INIT;

	memset(salt, 0, sizeof (cs->salt.zcs_bytes));
	if (cs->init)
		ctx = cs->init(&cs->salt);

	/* allocate test memory via abd linear interface */
	abd = abd_alloc_linear(1<<20, B_FALSE);
	chksum_run(cs, abd, ctx, 1, &cs->bs1k);
	chksum_run(cs, abd, ctx, 2, &cs->bs4k);
	chksum_run(cs, abd, ctx, 3, &cs->bs16k);
	chksum_run(cs, abd, ctx, 4, &cs->bs64k);
	chksum_run(cs, abd, ctx, 5, &cs->bs256k);

	/* check if we ran on a slow cpu */
	if (chksum_stat_limit == LIMIT_INIT) {
		if (cs->bs1k < LIMIT_PERF_MBS) {
			chksum_stat_limit = LIMIT_NEEDED;
		} else {
			chksum_stat_limit = LIMIT_NOLIMIT;
		}
	}

	/* skip benchmarks >= 1MiB when the CPU is to slow */
	if (chksum_stat_limit == LIMIT_NEEDED)
		goto abort;

	chksum_run(cs, abd, ctx, 6, &cs->bs1m);
	abd_free(abd);

	/* allocate test memory via abd non linear interface */
	abd = abd_alloc(1<<24, B_FALSE);
	chksum_run(cs, abd, ctx, 7, &cs->bs4m);
	chksum_run(cs, abd, ctx, 8, &cs->bs16m);

abort:
	abd_free(abd);

	/* free up temp memory */
	if (cs->free)
		cs->free(ctx);
}

/*
 * Initialize and benchmark all supported implementations.
 */
static void
chksum_benchmark(void)
{
#ifndef _KERNEL
	/* we need the benchmark only for the kernel module */
	return;
#endif

	chksum_stat_t *cs;
	uint64_t max;
	uint32_t id, cbid = 0, id_save;
	const zfs_impl_t *blake3 = zfs_impl_get_ops("blake3");
	const zfs_impl_t *sha256 = zfs_impl_get_ops("sha256");
	const zfs_impl_t *sha512 = zfs_impl_get_ops("sha512");

	/* count implementations */
	chksum_stat_cnt = 2;
	chksum_stat_cnt += sha256->getcnt();
	chksum_stat_cnt += sha512->getcnt();
	chksum_stat_cnt += blake3->getcnt();
	chksum_stat_data = kmem_zalloc(
	    sizeof (chksum_stat_t) * chksum_stat_cnt, KM_SLEEP);

	/* edonr - needs to be the first one here (slow CPU check) */
	cs = &chksum_stat_data[cbid++];

	/* edonr */
	cs->init = abd_checksum_edonr_tmpl_init;
	cs->func = abd_checksum_edonr_native;
	cs->free = abd_checksum_edonr_tmpl_free;
	cs->name = "edonr";
	cs->impl = "generic";
	chksum_benchit(cs);

	/* skein */
	cs = &chksum_stat_data[cbid++];
	cs->init = abd_checksum_skein_tmpl_init;
	cs->func = abd_checksum_skein_native;
	cs->free = abd_checksum_skein_tmpl_free;
	cs->name = "skein";
	cs->impl = "generic";
	chksum_benchit(cs);

	/* sha256 */
	id_save = sha256->getid();
	for (max = 0, id = 0; id < sha256->getcnt(); id++) {
		sha256->setid(id);
		cs = &chksum_stat_data[cbid++];
		cs->init = 0;
		cs->func = abd_checksum_sha256;
		cs->free = 0;
		cs->name = sha256->name;
		cs->impl = sha256->getname();
		chksum_benchit(cs);
		if (cs->bs256k > max) {
			max = cs->bs256k;
			sha256->set_fastest(id);
		}
	}
	sha256->setid(id_save);

	/* sha512 */
	id_save = sha512->getid();
	for (max = 0, id = 0; id < sha512->getcnt(); id++) {
		sha512->setid(id);
		cs = &chksum_stat_data[cbid++];
		cs->init = 0;
		cs->func = abd_checksum_sha512_native;
		cs->free = 0;
		cs->name = sha512->name;
		cs->impl = sha512->getname();
		chksum_benchit(cs);
		if (cs->bs256k > max) {
			max = cs->bs256k;
			sha512->set_fastest(id);
		}
	}
	sha512->setid(id_save);

	/* blake3 */
	id_save = blake3->getid();
	for (max = 0, id = 0; id < blake3->getcnt(); id++) {
		blake3->setid(id);
		cs = &chksum_stat_data[cbid++];
		cs->init = abd_checksum_blake3_tmpl_init;
		cs->func = abd_checksum_blake3_native;
		cs->free = abd_checksum_blake3_tmpl_free;
		cs->name = blake3->name;
		cs->impl = blake3->getname();
		chksum_benchit(cs);
		if (cs->bs256k > max) {
			max = cs->bs256k;
			blake3->set_fastest(id);
		}
	}
	blake3->setid(id_save);
}

void
chksum_init(void)
{
#ifdef _KERNEL
	blake3_per_cpu_ctx_init();
#endif

	/* Benchmark supported implementations */
	chksum_benchmark();

	/* Install kstats for all implementations */
	chksum_kstat = kstat_create("zfs", 0, "chksum_bench", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

	if (chksum_kstat != NULL) {
		chksum_kstat->ks_data = NULL;
		chksum_kstat->ks_ndata = UINT32_MAX;
		kstat_set_raw_ops(chksum_kstat,
		    chksum_kstat_headers,
		    chksum_kstat_data,
		    chksum_kstat_addr);
		kstat_install(chksum_kstat);
	}
}

void
chksum_fini(void)
{
	if (chksum_kstat != NULL) {
		kstat_delete(chksum_kstat);
		chksum_kstat = NULL;
	}

	if (chksum_stat_cnt) {
		kmem_free(chksum_stat_data,
		    sizeof (chksum_stat_t) * chksum_stat_cnt);
		chksum_stat_cnt = 0;
		chksum_stat_data = 0;
	}

#ifdef _KERNEL
	blake3_per_cpu_ctx_fini();
#endif
}
