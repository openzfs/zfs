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
#include <sys/zfs_context.h>
#include <zfs_fletcher.h>

static void fletcher_4_scalar_init(zio_cksum_t *zcp);
static void fletcher_4_scalar(const void *buf, uint64_t size,
    zio_cksum_t *zcp);
static void fletcher_4_scalar_byteswap(const void *buf, uint64_t size,
    zio_cksum_t *zcp);
static boolean_t fletcher_4_scalar_valid(void);

static const fletcher_4_ops_t fletcher_4_scalar_ops = {
	.init = fletcher_4_scalar_init,
	.compute = fletcher_4_scalar,
	.compute_byteswap = fletcher_4_scalar_byteswap,
	.valid = fletcher_4_scalar_valid,
	.name = "scalar"
};

static const fletcher_4_ops_t *fletcher_4_algos[] = {
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
};

static enum fletcher_selector {
	FLETCHER_FASTEST = 0,
	FLETCHER_SCALAR,
#if defined(HAVE_SSE2)
	FLETCHER_SSE2,
#endif
#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
	FLETCHER_SSSE3,
#endif
#if defined(HAVE_AVX) && defined(HAVE_AVX2)
	FLETCHER_AVX2,
#endif
	FLETCHER_CYCLE
} fletcher_4_impl_chosen = FLETCHER_SCALAR;

static struct fletcher_4_impl_selector {
	const char		*fis_name;
	const fletcher_4_ops_t	*fis_ops;
} fletcher_4_impl_selectors[] = {
	[ FLETCHER_FASTEST ]	= { "fastest", NULL },
	[ FLETCHER_SCALAR ]	= { "scalar", &fletcher_4_scalar_ops },
#if defined(HAVE_SSE2)
	[ FLETCHER_SSE2 ]	= { "sse2", &fletcher_4_sse2_ops },
#endif
#if defined(HAVE_SSE2) && defined(HAVE_SSSE3)
	[ FLETCHER_SSSE3 ]	= { "ssse3", &fletcher_4_ssse3_ops },
#endif
#if defined(HAVE_AVX) && defined(HAVE_AVX2)
	[ FLETCHER_AVX2 ]	= { "avx2", &fletcher_4_avx2_ops },
#endif
#if !defined(_KERNEL)
	[ FLETCHER_CYCLE ]	= { "cycle", &fletcher_4_scalar_ops }
#endif
};

static kmutex_t fletcher_4_impl_lock;

static kstat_t *fletcher_4_kstat;

static kstat_named_t fletcher_4_kstat_data[ARRAY_SIZE(fletcher_4_algos)];

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

static void fletcher_4_scalar_init(zio_cksum_t *zcp)
{
	ZIO_SET_CHECKSUM(zcp, 0, 0, 0, 0);
}

static void
fletcher_4_scalar(const void *buf, uint64_t size, zio_cksum_t *zcp)
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
	const fletcher_4_ops_t *ops;
	enum fletcher_selector idx;
	size_t val_len;
	unsigned i;

	val_len = strlen(val);
	while ((val_len > 0) && !!isspace(val[val_len-1])) /* trim '\n' */
		val_len--;

	for (i = 0; i < ARRAY_SIZE(fletcher_4_impl_selectors); i++) {
		const char *name = fletcher_4_impl_selectors[i].fis_name;

		if (val_len == strlen(name) &&
		    strncmp(val, name, val_len) == 0) {
			idx = i;
			break;
		}
	}
	if (i >= ARRAY_SIZE(fletcher_4_impl_selectors))
		return (-EINVAL);

	ops = fletcher_4_impl_selectors[idx].fis_ops;
	if (ops == NULL || !ops->valid())
		return (-ENOTSUP);

	mutex_enter(&fletcher_4_impl_lock);
	if (fletcher_4_impl_chosen != idx)
		fletcher_4_impl_chosen = idx;
	mutex_exit(&fletcher_4_impl_lock);

	return (0);
}

static inline const fletcher_4_ops_t *
fletcher_4_impl_get(void)
{
#if !defined(_KERNEL)
	if (fletcher_4_impl_chosen == FLETCHER_CYCLE) {
		static volatile unsigned int cycle_count = 0;
		const fletcher_4_ops_t *ops = NULL;
		unsigned int index;

		while (1) {
			index = atomic_inc_uint_nv(&cycle_count);
			ops = fletcher_4_algos[
			    index % ARRAY_SIZE(fletcher_4_algos)];
			if (ops->valid())
				break;
		}
		return (ops);
	}
#endif
	membar_producer();
	return (fletcher_4_impl_selectors[fletcher_4_impl_chosen].fis_ops);
}

void
fletcher_4_native(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const fletcher_4_ops_t *ops;

	if (IS_P2ALIGNED(size, 4 * sizeof (uint32_t)))
		ops = fletcher_4_impl_get();
	else
		ops = &fletcher_4_scalar_ops;

	ops->init(zcp);
	ops->compute(buf, size, zcp);
	if (ops->fini != NULL)
		ops->fini(zcp);
}

void
fletcher_4_byteswap(const void *buf, uint64_t size, zio_cksum_t *zcp)
{
	const fletcher_4_ops_t *ops;

	if (IS_P2ALIGNED(size, 4 * sizeof (uint32_t)))
		ops = fletcher_4_impl_get();
	else
		ops = &fletcher_4_scalar_ops;

	ops->init(zcp);
	ops->compute_byteswap(buf, size, zcp);
	if (ops->fini != NULL)
		ops->fini(zcp);
}

void
fletcher_4_incremental_native(const void *buf, uint64_t size,
    zio_cksum_t *zcp)
{
	fletcher_4_scalar(buf, size, zcp);
}

void
fletcher_4_incremental_byteswap(const void *buf, uint64_t size,
    zio_cksum_t *zcp)
{
	fletcher_4_scalar_byteswap(buf, size, zcp);
}

void
fletcher_4_init(void)
{
	const uint64_t const bench_ns = (50 * MICROSEC); /* 50ms */
	unsigned long best_run_count = 0;
	unsigned long best_run_index = 0;
	const unsigned data_size = 4096;
	char *databuf;
	int i;

	databuf = kmem_alloc(data_size, KM_SLEEP);
	for (i = 0; i < ARRAY_SIZE(fletcher_4_algos); i++) {
		const fletcher_4_ops_t *ops = fletcher_4_algos[i];
		kstat_named_t *stat = &fletcher_4_kstat_data[i];
		unsigned long run_count = 0;
		hrtime_t start;
		zio_cksum_t zc;

		strncpy(stat->name, ops->name, sizeof (stat->name) - 1);
		stat->data_type = KSTAT_DATA_UINT64;
		stat->value.ui64 = 0;

		if (!ops->valid())
			continue;

		kpreempt_disable();
		start = gethrtime();
		ops->init(&zc);
		do {
			ops->compute(databuf, data_size, &zc);
			ops->compute_byteswap(databuf, data_size, &zc);
			run_count++;
		} while (gethrtime() < start + bench_ns);
		if (ops->fini != NULL)
			ops->fini(&zc);
		kpreempt_enable();

		if (run_count > best_run_count) {
			best_run_count = run_count;
			best_run_index = i;
		}

		/*
		 * Due to high overhead of gethrtime(), the performance data
		 * here is inaccurate and much slower than it could be.
		 * It's fine for our use though because only relative speed
		 * is important.
		 */
		stat->value.ui64 = data_size * run_count *
		    (NANOSEC / bench_ns) >> 20; /* by MB/s */
	}
	kmem_free(databuf, data_size);

	fletcher_4_impl_selectors[FLETCHER_FASTEST].fis_ops =
	    fletcher_4_algos[best_run_index];

	mutex_init(&fletcher_4_impl_lock, NULL, MUTEX_DEFAULT, NULL);
	fletcher_4_impl_set("fastest");

	fletcher_4_kstat = kstat_create("zfs", 0, "fletcher_4_bench",
	    "misc", KSTAT_TYPE_NAMED, ARRAY_SIZE(fletcher_4_algos),
	    KSTAT_FLAG_VIRTUAL);
	if (fletcher_4_kstat != NULL) {
		fletcher_4_kstat->ks_data = fletcher_4_kstat_data;
		kstat_install(fletcher_4_kstat);
	}
}

void
fletcher_4_fini(void)
{
	mutex_destroy(&fletcher_4_impl_lock);
	if (fletcher_4_kstat != NULL) {
		kstat_delete(fletcher_4_kstat);
		fletcher_4_kstat = NULL;
	}
}

#if defined(_KERNEL) && defined(HAVE_SPL)

static int
fletcher_4_param_get(char *buffer, struct kernel_param *unused)
{
	int i, cnt = 0;

	for (i = 0; i < ARRAY_SIZE(fletcher_4_impl_selectors); i++) {
		const fletcher_4_ops_t *ops;

		ops = fletcher_4_impl_selectors[i].fis_ops;
		if (!ops->valid())
			continue;

		cnt += sprintf(buffer + cnt,
		    fletcher_4_impl_chosen == i ? "[%s] " : "%s ",
		    fletcher_4_impl_selectors[i].fis_name);
	}

	return (cnt);
}

static int
fletcher_4_param_set(const char *val, struct kernel_param *unused)
{
	return (fletcher_4_impl_set(val));
}

/*
 * Choose a fletcher 4 implementation in ZFS.
 * Users can choose the "fastest" algorithm, or "scalar" and "avx2" which means
 * to compute fletcher 4 by CPU or vector instructions respectively.
 * Users can also choose "cycle" to exercise all implementions, but this is
 * for testing purpose therefore it can only be set in user space.
 */
module_param_call(zfs_fletcher_4_impl,
    fletcher_4_param_set, fletcher_4_param_get, NULL, 0644);
MODULE_PARM_DESC(zfs_fletcher_4_impl, "Select fletcher 4 algorithm");

EXPORT_SYMBOL(fletcher_4_init);
EXPORT_SYMBOL(fletcher_4_fini);
EXPORT_SYMBOL(fletcher_2_native);
EXPORT_SYMBOL(fletcher_2_byteswap);
EXPORT_SYMBOL(fletcher_4_native);
EXPORT_SYMBOL(fletcher_4_byteswap);
EXPORT_SYMBOL(fletcher_4_incremental_native);
EXPORT_SYMBOL(fletcher_4_incremental_byteswap);
#endif
