
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
 * Copyright (c) 2021 Tino Reichardt <milky-zfs@mcmilk.de>
 */

#include <sys/types.h>
#include <sys/spa.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <sys/zfs_chksum.h>

#include <sys/blake3.h>

static kstat_t *chksum_stat_kstat;
static int chksum_stat_data_cnt = 0;

static struct chksum_stat_kstat {
	const char *name;
	const char *impl;
	uint16_t digest;
	uint64_t bs1k;
	uint64_t bs2k;
	uint64_t bs4k;
	uint64_t bs8k;
	uint64_t bs16k;
	uint64_t bs32k;
	uint64_t bs64k;
	uint64_t bs128k;
	uint64_t bs256k;
	uint64_t bs512k;
	uint64_t bs1m;
	zio_checksum_t *(func);
	zio_checksum_tmpl_init_t *(init);
	zio_checksum_tmpl_free_t *(free);
} chksum_stat_data[5+7]; /* current max */

/*
 * implementation       digest       1k   2k   4k  16k  32k  64k 128k 256k 512k
 *
 * fletcher-4                4      22M  22M   22   22   22   22   22   22   22
 * edonr-generic           256      22M  22M   22   22   22   22   22   22   22
 * skein-generic           256      22M  22M   22   22   22   22   22   22   22
 * sha256-generic          256      22M  22M   22   22   22   22   22   22   22
 * sha512-generic          512      22M  22M   22   22   22   22   22   22   22
 *
 * blake3-generic          256      22M  22M   22   22   22   22   22   22   22
 * blake3-sse2             256      22M  22M   22   22   22   22   22   22   22
 * blake3-sse41            256      22M  22M   22   22   22   22   22   22   22
 * blake3-avx              256      22M  22M   22   22   22   22   22   22   22
 * blake3-avx2             256      22M  22M   22   22   22   22   22   22   22
 * blake3-avx512           256      22M  22M   22   22   22   22   22   22   22
 * blake3-neon             256      22M  22M   22   22   22   22   22   22   22
 */
static int
chksum_stat_kstat_headers(char *buf, size_t size)
{
	ssize_t off = 0;

	off += snprintf(buf + off, size, "%-17s", "implementation");
	off += snprintf(buf + off, size - off, "%-10s", "digest");
	off += snprintf(buf + off, size - off, "%-10s", "1k");
	off += snprintf(buf + off, size - off, "%-10s", "2k");
	off += snprintf(buf + off, size - off, "%-10s", "4k");
	off += snprintf(buf + off, size - off, "%-10s", "8k");
	off += snprintf(buf + off, size - off, "%-10s", "16k");
	off += snprintf(buf + off, size - off, "%-10s", "32k");
	off += snprintf(buf + off, size - off, "%-10s", "64k");
	off += snprintf(buf + off, size - off, "%-10s", "128k");
	off += snprintf(buf + off, size - off, "%-10s", "256k");
	off += snprintf(buf + off, size - off, "%-10s", "512k");
	(void) snprintf(buf + off, size - off, "%-10s\n", "1m");

	return (0);
}

static int
chksum_stat_kstat_data(char *buf, size_t size, void *data)
{
	ssize_t off = 0;
	char b[20];
	struct chksum_stat_kstat *stat;

	stat = (struct chksum_stat_kstat *)data;
	snprintf(b, 19, "%s-%s", stat->name, stat->impl);
	off += snprintf(buf + off, size - off, "%-17s", b);
	off += snprintf(buf + off, size - off, "%-10u",
	    (unsigned)stat->digest);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs1k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs2k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs4k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs8k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs16k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs32k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs64k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs128k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs256k);
	off += snprintf(buf + off, size - off, "%-10llu",
	    (u_longlong_t)stat->bs512k);
	(void) snprintf(buf + off, size - off, "%-10llu\n",
	    (u_longlong_t)stat->bs1m);

	return (0);
}

static void *
chksum_stat_kstat_addr(kstat_t *ksp, loff_t n)
{
	if (n < chksum_stat_data_cnt)
		ksp->ks_private = (void *)(chksum_stat_data + n);
	else
		ksp->ks_private = NULL;

	return (ksp->ks_private);
}

static void
chksum_run(struct chksum_stat_kstat *ks, abd_t *abd, void *ctx, uint64_t size,
    uint64_t *result)
{
	hrtime_t start;
	uint64_t run_bw, run_time_ns, run_count = 0;
	uint32_t l;
	zio_cksum_t zcp;

	kpreempt_disable();
	start = gethrtime();
	do {
		for (l = 0; l < 64; l++, run_count++)
			ks->func(abd, size, ctx, &zcp);

		run_time_ns = gethrtime() - start;
	} while (run_time_ns < MSEC2NSEC(1));
	kpreempt_enable();

	run_bw = size * run_count * NANOSEC;
	run_bw /= run_time_ns;	/* B/s */
	*result = run_bw/1024/1024; /* MiB/s */
}

static void
chksum_benchit(struct chksum_stat_kstat *ks)
{
	abd_t *abd;
	void *ctx = 0;
	zio_cksum_salt_t salt;

	/* allocate test memory via default abd interface */
	abd = abd_alloc_linear(1024*1024, B_FALSE);
	bzero(salt.zcs_bytes, sizeof (zio_cksum_salt_t));
	if (ks->init) {
		ctx = ks->init(&salt);
	}

	chksum_run(ks, abd, ctx, 1024, &ks->bs1k);
	chksum_run(ks, abd, ctx, 1024*2, &ks->bs2k);
	chksum_run(ks, abd, ctx, 1024*4, &ks->bs4k);
	chksum_run(ks, abd, ctx, 1024*8, &ks->bs8k);
	chksum_run(ks, abd, ctx, 1024*16, &ks->bs16k);
	chksum_run(ks, abd, ctx, 1024*32, &ks->bs32k);
	chksum_run(ks, abd, ctx, 1024*64, &ks->bs64k);
	chksum_run(ks, abd, ctx, 1024*128, &ks->bs128k);
	chksum_run(ks, abd, ctx, 1024*256, &ks->bs256k);
	chksum_run(ks, abd, ctx, 1024*512, &ks->bs512k);
	chksum_run(ks, abd, ctx, 1024*1024, &ks->bs1m);

	/* free up temp memory */
	if (ks->free) {
		ks->free(ctx);
	}
	abd_free(abd);
}

/*
 * Initialize and benchmark all supported implementations.
 */
static void
chksum_benchmark(void)
{
	struct chksum_stat_kstat *ks;
	int i = 0, id, id_max = 0;
	uint64_t max = 0;

	/* fletcher */
	ks = &chksum_stat_data[i++];
	ks->init = 0;
	ks->func = abd_fletcher_4_native;
	ks->free = 0;
	ks->name = "fletcher";
	ks->impl = "4";
	ks->digest = 4;
	chksum_benchit(ks);

	/* edonr */
	ks = &chksum_stat_data[i++];
	ks->init = abd_checksum_edonr_tmpl_init;
	ks->func = abd_checksum_edonr_native;
	ks->free = abd_checksum_edonr_tmpl_free;
	ks->name = "edonr";
	ks->impl = "generic";
	ks->digest = 256;
	chksum_benchit(ks);

	/* skein */
	ks = &chksum_stat_data[i++];
	ks->init = abd_checksum_skein_tmpl_init;
	ks->func = abd_checksum_skein_native;
	ks->free = abd_checksum_skein_tmpl_free;
	ks->name = "skein";
	ks->impl = "generic";
	ks->digest = 256;
	chksum_benchit(ks);

	/* sha256 */
	ks = &chksum_stat_data[i++];
	ks->init = 0;
	ks->func = abd_checksum_SHA256;
	ks->free = 0;
	ks->name = "sha256";
	ks->impl = "generic";
	ks->digest = 256;
	chksum_benchit(ks);

	/* sha512 */
	ks = &chksum_stat_data[i++];
	ks->init = 0;
	ks->func = abd_checksum_SHA512_native;
	ks->free = 0;
	ks->name = "sha512";
	ks->impl = "generic";
	ks->digest = 512;
	chksum_benchit(ks);

	/* blake3 */
	for (id = 0; id < blake3_get_impl_count(); id++) {
		blake3_set_impl_id(id);
		ks = &chksum_stat_data[i++];
		ks->init = abd_checksum_blake3_tmpl_init;
		ks->func = abd_checksum_blake3_native;
		ks->free = abd_checksum_blake3_tmpl_free;
		ks->name = "blake3";
		ks->impl = blake3_get_impl_name();
		ks->digest = 256;
		chksum_benchit(ks);
		if (ks->bs128k > max) {
			max = ks->bs128k;
			id_max = id;
		}
	}

	/* switch blake to the fastest method */
	blake3_set_impl_id(id_max);

	/* remember currently filled benchmark data */
	chksum_stat_data_cnt = i;
}

void
chksum_init(void)
{
	/* Benchmark supported implementations */
	chksum_benchmark();

	/* Install kstats for all implementations */
	chksum_stat_kstat = kstat_create("zfs", 0, "chksum_bench", "misc",
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

	if (chksum_stat_kstat != NULL) {
		chksum_stat_kstat->ks_data = NULL;
		chksum_stat_kstat->ks_ndata = UINT32_MAX;
		kstat_set_raw_ops(chksum_stat_kstat,
		    chksum_stat_kstat_headers,
		    chksum_stat_kstat_data,
		    chksum_stat_kstat_addr);
		kstat_install(chksum_stat_kstat);
	}
}

void
chksum_fini(void)
{
	if (chksum_stat_kstat != NULL) {
		kstat_delete(chksum_stat_kstat);
		chksum_stat_kstat = NULL;
	}
}
