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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright (C) 2016 Gvozden Nešković. All rights reserved.
 */

/*
 * Fletcher Checksums
 * ------------------
 *
 * ZFS's 2nd and 4th order Fletcher checksums are defined by the following
 * recurrence relations:
 *
 *	a  = a    + f
 *	 i    i-1    i-1
 *
 *	b  = b    + a
 *	 i    i-1    i
 *
 *	c  = c    + b		(fletcher-4 only)
 *	 i    i-1    i
 *
 *	d  = d    + c		(fletcher-4 only)
 *	 i    i-1    i
 *
 * Where
 *	a_0 = b_0 = c_0 = d_0 = 0
 * and
 *	f_0 .. f_(n-1) are the input data.
 *
 * Using standard techniques, these translate into the following series:
 *
 *	     __n_			     __n_
 *	     \   |			     \   |
 *	a  =  >     f			b  =  >     i * f
 *	 n   /___|   n - i		 n   /___|	 n - i
 *	     i = 1			     i = 1
 *
 *
 *	     __n_			     __n_
 *	     \   |  i*(i+1)		     \   |  i*(i+1)*(i+2)
 *	c  =  >     ------- f		d  =  >     ------------- f
 *	 n   /___|     2     n - i	 n   /___|	  6	   n - i
 *	     i = 1			     i = 1
 *
 * For fletcher-2, the f_is are 64-bit, and [ab]_i are 64-bit accumulators.
 * Since the additions are done mod (2^64), errors in the high bits may not
 * be noticed.  For this reason, fletcher-2 is deprecated.
 *
 * For fletcher-4, the f_is are 32-bit, and [abcd]_i are 64-bit accumulators.
 * A conservative estimate of how big the buffer can get before we overflow
 * can be estimated using f_i = 0xffffffff for all i:
 *
 * % bc
 *  f=2^32-1;d=0; for (i = 1; d<2^64; i++) { d += f*i*(i+1)*(i+2)/6 }; (i-1)*4
 * 2264
 *  quit
 * %
 *
 * So blocks of up to 2k will not overflow.  Our largest block size is
 * 128k, which has 32k 4-byte words, so we can compute the largest possible
 * accumulators, then divide by 2^64 to figure the max amount of overflow:
 *
 * % bc
 *  a=b=c=d=0; f=2^32-1; for (i=1; i<=32*1024; i++) { a+=f; b+=a; c+=b; d+=c }
 *  a/2^64;b/2^64;c/2^64;d/2^64
 * 0
 * 0
 * 1365
 * 11186858
 *  quit
 * %
 *
 * So a and b cannot overflow.  To make sure each bit of input has some
 * effect on the contents of c and d, we can look at what the factors of
 * the coefficients in the equations for c_n and d_n are.  The number of 2s
 * in the factors determines the lowest set bit in the multiplier.  Running
 * through the cases for n*(n+1)/2 reveals that the highest power of 2 is
 * 2^14, and for n*(n+1)*(n+2)/6 it is 2^15.  So while some data may overflow
 * the 64-bit accumulators, every bit of every f_i effects every accumulator,
 * even for 128k blocks.
 *
 * If we wanted to make a stronger version of fletcher4 (fletcher4c?),
 * we could do our calculations mod (2^32 - 1) by adding in the carries
 * periodically, and store the number of carries in the top 32-bits.
 *
 * --------------------
 * Checksum Performance
 * --------------------
 *
 * There are two interesting components to checksum performance: cached and
 * uncached performance.  With cached data, fletcher-2 is about four times
 * faster than fletcher-4.  With uncached data, the performance difference is
 * negligible, since the cost of a cache fill dominates the processing time.
 * Even though fletcher-4 is slower than fletcher-2, it is still a pretty
 * efficient pass over the data.
 *
 * In normal operation, the data which is being checksummed is in a buffer
 * which has been filled either by:
 *
 *	1. a compression step, which will be mostly cached, or
 *	2. a bcopy() or copyin(), which will be uncached (because the
 *	   copy is cache-bypassing).
 *
 * For both cached and uncached data, both fletcher checksums are much faster
 * than sha-256, and slower than 'off', which doesn't touch the data at all.
 */

#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/byteorder.h>
#include <sys/spa.h>
#include <sys/zio_checksum.h>
#include <sys/zfs_context.h>
#include <zfs_fletcher.h>


static void fletcher_4_scalar_init(zio_cksum_t *zcp);
static void fletcher_4_scalar_native(const void *buf, uint64_t size,
    zio_cksum_t *zcp);
static void fletcher_4_scalar_byteswap(const void *buf, uint64_t size,
    zio_cksum_t *zcp);
static boolean_t fletcher_4_scalar_valid(void);

static const fletcher_4_ops_t fletcher_4_scalar_ops = {
	.init_native = fletcher_4_scalar_init,
	.compute_native = fletcher_4_scalar_native,
	.init_byteswap = fletcher_4_scalar_init,
	.compute_byteswap = fletcher_4_scalar_byteswap,
	.valid = fletcher_4_scalar_valid,
	.name = "scalar"
};

static fletcher_4_ops_t fletcher_4_fastest_impl = {
	.name = "fastest",
	.valid = fletcher_4_scalar_valid
};

static const fletcher_4_ops_t *fletcher_4_impls[] = {
	&fletcher_4_scalar_ops,
#if defined(HAVE_SSE2)
	&fletcher_4_sse2_ops,
#endif
#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
	&fletcher_4_ssse3_ops,
#endif
#if defined(HAVE_AVX) && defined(HAVE_AVX2)
	&fletcher_4_avx2_ops,
#endif
#if defined(__x86_64) && defined(HAVE_AVX512F)
	&fletcher_4_avx512f_ops,
#endif
};

/* Hold all supported implementations */
static uint32_t fletcher_4_supp_impls_cnt = 0;
static fletcher_4_ops_t *fletcher_4_supp_impls[ARRAY_SIZE(fletcher_4_impls)];

/* Select fletcher4 implementation */
#define	IMPL_FASTEST	(UINT32_MAX)
#define	IMPL_CYCLE	(UINT32_MAX - 1)
#define	IMPL_SCALAR	(0)

static uint32_t fletcher_4_impl_chosen = IMPL_FASTEST;

#define	IMPL_READ(i)	(*(volatile uint32_t *) &(i))

static struct fletcher_4_impl_selector {
	const char	*fis_name;
	uint32_t	fis_sel;
} fletcher_4_impl_selectors[] = {
#if !defined(_KERNEL)
	{ "cycle",	IMPL_CYCLE },
#endif
	{ "fastest",	IMPL_FASTEST },
	{ "scalar",	IMPL_SCALAR }
};

static kstat_t *fletcher_4_kstat;

static struct fletcher_4_kstat {
	uint64_t native;
	uint64_t byteswap;
} fletcher_4_stat_data[ARRAY_SIZE(fletcher_4_impls) + 1];

/* Indicate that benchmark has been completed */
static boolean_t fletcher_4_initialized = B_FALSE;

void
fletcher_2_native(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const uint64_t *ip = buf;
	const uint64_t *ipend = ip + (size / sizeof (uint64_t));
	uint64_t a0, b0, a1, b1;

	for (a0 = b0 = a1 = b1 = 0; ip < ipend; ip += 2) {
		a0 += ip[0];
		a1 += ip[1];
		b0 += a0;
		b1 += a1;
	}

	ZIO_SET_CHECKSUM(zcp, a0, a1, b0, b1);
}

void
fletcher_2_byteswap(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const uint64_t *ip = buf;
	const uint64_t *ipend = ip + (size / sizeof (uint64_t));
	uint64_t a0, b0, a1, b1;

	for (a0 = b0 = a1 = b1 = 0; ip < ipend; ip += 2) {
		a0 += BSWAP_64(ip[0]);
		a1 += BSWAP_64(ip[1]);
		b0 += a0;
		b1 += a1;
	}

	ZIO_SET_CHECKSUM(zcp, a0, a1, b0, b1);
}

static void
fletcher_4_scalar_init(zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

static void
fletcher_4_scalar_native(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = ip + (size / sizeof (uint32_t));
	uint64_t a, b, c, d;

	a = zcp->zc_word[0];
	b = zcp->zc_word[1];
	c = zcp->zc_word[2];
	d = zcp->zc_word[3];

	for (; ip < ipend; ip++) {
		a += ip[0];
		b += a;
		c += b;
		d += c;
	}

	ZIO_SET_CHECKSUM(zcp, a, b, c, d);
}

static void
fletcher_4_scalar_byteswap(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const uint32_t *ip = buf;
	const uint32_t *ipend = ip + (size / sizeof (uint32_t));
	uint64_t a, b, c, d;

	a = zcp->zc_word[0];
	b = zcp->zc_word[1];
	c = zcp->zc_word[2];
	d = zcp->zc_word[3];

	for (; ip < ipend; ip++) {
		a += BSWAP_32(ip[0]);
		b += a;
		c += b;
		d += c;
	}

	ZIO_SET_CHECKSUM(zcp, a, b, c, d);
}

static boolean_t
fletcher_4_scalar_valid(void)
{
	return (B_TRUE);
}

int
fletcher_4_impl_set(const char *val)
{
	int err = -EINVAL;
	uint32_t impl = IMPL_READ(fletcher_4_impl_chosen);
	size_t i, val_len;

	val_len = strlen(val);
	while ((val_len > 0) && !!isspace(val[val_len-1])) /* trim '\n' */
		val_len--;

	/* check mandatory implementations */
	for (i = 0; i < ARRAY_SIZE(fletcher_4_impl_selectors); i++) {
		const char *name = fletcher_4_impl_selectors[i].fis_name;

		if (val_len == strlen(name) &&
		    strncmp(val, name, val_len) == 0) {
			impl = fletcher_4_impl_selectors[i].fis_sel;
			err = 0;
			break;
		}
	}

	if (err != 0 && fletcher_4_initialized) {
		/* check all supported implementations */
		for (i = 0; i < fletcher_4_supp_impls_cnt; i++) {
			const char *name = fletcher_4_supp_impls[i]->name;

			if (val_len == strlen(name) &&
			    strncmp(val, name, val_len) == 0) {
				impl = i;
				err = 0;
				break;
			}
		}
	}

	if (err == 0) {
		atomic_swap_32(&fletcher_4_impl_chosen, impl);
		membar_producer();
	}

	return (err);
}

static inline const fletcher_4_ops_t *
fletcher_4_impl_get(void)
{
	fletcher_4_ops_t *ops = NULL;
	const uint32_t impl = IMPL_READ(fletcher_4_impl_chosen);

	switch (impl) {
	case IMPL_FASTEST:
		ASSERT(fletcher_4_initialized);
		ops = &fletcher_4_fastest_impl;
		break;
#if !defined(_KERNEL)
	case IMPL_CYCLE: {
		ASSERT(fletcher_4_initialized);
		ASSERT3U(fletcher_4_supp_impls_cnt, >, 0);

		static uint32_t cycle_count = 0;
		uint32_t idx = (++cycle_count) % fletcher_4_supp_impls_cnt;
		ops = fletcher_4_supp_impls[idx];
	}
	break;
#endif
	default:
		ASSERT3U(fletcher_4_supp_impls_cnt, >, 0);
		ASSERT3U(impl, <, fletcher_4_supp_impls_cnt);

		ops = fletcher_4_supp_impls[impl];
		break;
	}

	ASSERT3P(ops, !=, NULL);

	return (ops);
}

void
fletcher_4_incremental_native(const void *buf, uint64_t size,
    zio_cksum_t *zcp)
{
	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	fletcher_4_scalar_native(buf, size, zcp);
}

void
fletcher_4_incremental_byteswap(const void *buf, uint64_t size,
    zio_cksum_t *zcp)
{
	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	fletcher_4_scalar_byteswap(buf, size, zcp);
}

static inline void
fletcher_4_native_impl(const fletcher_4_ops_t *ops, const void *buf,
	uint64_t size, zio_cksum_t *zcp)
{
	ops->init_native(zcp);
	ops->compute_native(buf, size, zcp);
	if (ops->fini_native != NULL)
		ops->fini_native(zcp);
}

void
fletcher_4_native(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const fletcher_4_ops_t *ops;
	uint64_t p2size = P2ALIGN(size, 64);

	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	if (size == 0) {
		ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
	} else if (p2size == 0) {
		ops = &fletcher_4_scalar_ops;
		fletcher_4_native_impl(ops, buf, size, zcp);
	} else {
		ops = fletcher_4_impl_get();
		fletcher_4_native_impl(ops, buf, p2size, zcp);

		if (p2size < size)
			fletcher_4_incremental_native((char *)buf + p2size,
			    size - p2size, zcp);
	}
}

void
fletcher_4_native_varsize(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	fletcher_4_native_impl(&fletcher_4_scalar_ops, buf, size, zcp);
}

static inline void
fletcher_4_byteswap_impl(const fletcher_4_ops_t *ops, const void *buf,
	uint64_t size, zio_cksum_t *zcp)
{
	ops->init_byteswap(zcp);
	ops->compute_byteswap(buf, size, zcp);
	if (ops->fini_byteswap != NULL)
		ops->fini_byteswap(zcp);
}

void
fletcher_4_byteswap(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const fletcher_4_ops_t *ops;
	uint64_t p2size = P2ALIGN(size, 64);

	ASSERT(IS_P2ALIGNED(size, sizeof (uint32_t)));

	if (size == 0) {
		ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
	} else if (p2size == 0) {
		ops = &fletcher_4_scalar_ops;
		fletcher_4_byteswap_impl(ops, buf, size, zcp);
	} else {
		ops = fletcher_4_impl_get();
		fletcher_4_byteswap_impl(ops, buf, p2size, zcp);

		if (p2size < size)
			fletcher_4_incremental_byteswap((char *)buf + p2size,
			    size - p2size, zcp);
	}
}

static int
fletcher_4_kstat_headers(char *buf, size_t size)
{
	ssize_t off = 0;

	off += snprintf(buf + off, size, "%-17s", "implementation");
	off += snprintf(buf + off, size - off, "%-15s", "native");
	(void) snprintf(buf + off, size - off, "%-15s\n", "byteswap");

	return (0);
}

static int
fletcher_4_kstat_data(char *buf, size_t size, void *data)
{
	struct fletcher_4_kstat *fastest_stat =
	    &fletcher_4_stat_data[fletcher_4_supp_impls_cnt];
	struct fletcher_4_kstat *curr_stat = (struct fletcher_4_kstat *) data;
	ssize_t off = 0;

	if (curr_stat == fastest_stat) {
		off += snprintf(buf + off, size - off, "%-17s", "fastest");
		off += snprintf(buf + off, size - off, "%-15s",
		    fletcher_4_supp_impls[fastest_stat->native]->name);
		off += snprintf(buf + off, size - off, "%-15s\n",
		    fletcher_4_supp_impls[fastest_stat->byteswap]->name);
	} else {
		ptrdiff_t id = curr_stat - fletcher_4_stat_data;

		off += snprintf(buf + off, size - off, "%-17s",
		    fletcher_4_supp_impls[id]->name);
		off += snprintf(buf + off, size - off, "%-15llu",
			    (u_longlong_t) curr_stat->native);
		off += snprintf(buf + off, size - off, "%-15llu\n",
			    (u_longlong_t) curr_stat->byteswap);
	}

	return (0);
}

static void *
fletcher_4_kstat_addr(kstat_t *ksp, loff_t n)
{
	if (n <= fletcher_4_supp_impls_cnt)
		ksp->ks_private = (void *) (fletcher_4_stat_data + n);
	else
		ksp->ks_private = NULL;

	return (ksp->ks_private);
}

#define	FLETCHER_4_FASTEST_FN_COPY(type, src)				  \
{									  \
	fletcher_4_fastest_impl.init_ ## type = src->init_ ## type;	  \
	fletcher_4_fastest_impl.fini_ ## type = src->fini_ ## type;	  \
	fletcher_4_fastest_impl.compute_ ## type = src->compute_ ## type; \
}

#define	FLETCHER_4_BENCH_NS	(MSEC2NSEC(50))		/* 50ms */

static void
fletcher_4_benchmark_impl(boolean_t native, char *data, uint64_t data_size)
{

	struct fletcher_4_kstat *fastest_stat =
	    &fletcher_4_stat_data[fletcher_4_supp_impls_cnt];
	hrtime_t start;
	uint64_t run_bw, run_time_ns, best_run = 0;
	zio_cksum_t zc;
	uint32_t i, l, sel_save = IMPL_READ(fletcher_4_impl_chosen);

	zio_checksum_func_t *fletcher_4_test = native ? fletcher_4_native :
	    fletcher_4_byteswap;

	for (i = 0; i < fletcher_4_supp_impls_cnt; i++) {
		struct fletcher_4_kstat *stat = &fletcher_4_stat_data[i];
		uint64_t run_count = 0;

		/* temporary set an implementation */
		fletcher_4_impl_chosen = i;

		kpreempt_disable();
		start = gethrtime();
		do {
			for (l = 0; l < 32; l++, run_count++)
				fletcher_4_test(data, data_size, &zc);

			run_time_ns = gethrtime() - start;
		} while (run_time_ns < FLETCHER_4_BENCH_NS);
		kpreempt_enable();

		run_bw = data_size * run_count * NANOSEC;
		run_bw /= run_time_ns;	/* B/s */

		if (native)
			stat->native = run_bw;
		else
			stat->byteswap = run_bw;

		if (run_bw > best_run) {
			best_run = run_bw;

			if (native) {
				fastest_stat->native = i;
				FLETCHER_4_FASTEST_FN_COPY(native,
				    fletcher_4_supp_impls[i]);
			} else {
				fastest_stat->byteswap = i;
				FLETCHER_4_FASTEST_FN_COPY(byteswap,
				    fletcher_4_supp_impls[i]);
			}
		}
	}

	/* restore original selection */
	atomic_swap_32(&fletcher_4_impl_chosen, sel_save);
}

void
fletcher_4_init(void)
{
	static const size_t data_size = 1 << SPA_OLD_MAXBLOCKSHIFT; /* 128kiB */
	fletcher_4_ops_t *curr_impl;
	char *databuf;
	int i, c;

	/* move supported impl into fletcher_4_supp_impls */
	for (i = 0, c = 0; i < ARRAY_SIZE(fletcher_4_impls); i++) {
		curr_impl = (fletcher_4_ops_t *) fletcher_4_impls[i];

		if (curr_impl->valid && curr_impl->valid())
			fletcher_4_supp_impls[c++] = curr_impl;
	}
	membar_producer();	/* complete fletcher_4_supp_impls[] init */
	fletcher_4_supp_impls_cnt = c;	/* number of supported impl */

#if !defined(_KERNEL)
	/* Skip benchmarking and use last implementation as fastest */
	memcpy(&fletcher_4_fastest_impl,
	    fletcher_4_supp_impls[fletcher_4_supp_impls_cnt-1],
	    sizeof (fletcher_4_fastest_impl));
	fletcher_4_fastest_impl.name = "fastest";
	membar_producer();

	fletcher_4_initialized = B_TRUE;

	/* Use 'cycle' math selection method for userspace */
	VERIFY0(fletcher_4_impl_set("cycle"));
	return;
#endif
	/* Benchmark all supported implementations */
	databuf = vmem_alloc(data_size, KM_SLEEP);
	for (i = 0; i < data_size / sizeof (uint64_t); i++)
		((uint64_t *)databuf)[i] = (uintptr_t)(databuf+i); /* warm-up */

	fletcher_4_benchmark_impl(B_FALSE, databuf, data_size);
	fletcher_4_benchmark_impl(B_TRUE, databuf, data_size);

	vmem_free(databuf, data_size);

	/* install kstats for all implementations */
	fletcher_4_kstat = kstat_create("zfs", 0, "fletcher_4_bench", "misc",
		KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);
	if (fletcher_4_kstat != NULL) {
		fletcher_4_kstat->ks_data = NULL;
		fletcher_4_kstat->ks_ndata = UINT32_MAX;
		kstat_set_raw_ops(fletcher_4_kstat,
		    fletcher_4_kstat_headers,
		    fletcher_4_kstat_data,
		    fletcher_4_kstat_addr);
		kstat_install(fletcher_4_kstat);
	}

	/* Finish initialization */
	fletcher_4_initialized = B_TRUE;
}

void
fletcher_4_fini(void)
{
	if (fletcher_4_kstat != NULL) {
		kstat_delete(fletcher_4_kstat);
		fletcher_4_kstat = NULL;
	}
}

#if defined(_KERNEL) && defined(HAVE_SPL)
#include <linux/mod_compat.h>

static int
fletcher_4_param_get(char *buffer, zfs_kernel_param_t *unused)
{
	const uint32_t impl = IMPL_READ(fletcher_4_impl_chosen);
	char *fmt;
	int i, cnt = 0;

	/* list fastest */
	fmt = (impl == IMPL_FASTEST) ? "[%s] " : "%s ";
	cnt += sprintf(buffer + cnt, fmt, "fastest");

	/* list all supported implementations */
	for (i = 0; i < fletcher_4_supp_impls_cnt; i++) {
		fmt = (i == impl) ? "[%s] " : "%s ";
		cnt += sprintf(buffer + cnt, fmt,
		    fletcher_4_supp_impls[i]->name);
	}

	return (cnt);
}

static int
fletcher_4_param_set(const char *val, zfs_kernel_param_t *unused)
{
	return (fletcher_4_impl_set(val));
}

/*
 * Choose a fletcher 4 implementation in ZFS.
 * Users can choose "cycle" to exercise all implementations, but this is
 * for testing purpose therefore it can only be set in user space.
 */
module_param_call(zfs_fletcher_4_impl,
    fletcher_4_param_set, fletcher_4_param_get, NULL, 0644);
MODULE_PARM_DESC(zfs_fletcher_4_impl, "Select fletcher 4 implementation.");

EXPORT_SYMBOL(fletcher_4_init);
EXPORT_SYMBOL(fletcher_4_fini);
EXPORT_SYMBOL(fletcher_2_native);
EXPORT_SYMBOL(fletcher_2_byteswap);
EXPORT_SYMBOL(fletcher_4_native);
EXPORT_SYMBOL(fletcher_4_native_varsize);
EXPORT_SYMBOL(fletcher_4_byteswap);
EXPORT_SYMBOL(fletcher_4_incremental_native);
EXPORT_SYMBOL(fletcher_4_incremental_byteswap);
#endif
