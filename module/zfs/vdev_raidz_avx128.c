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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2015 AtoS <romain.dolbeau@atos.net>. All rights reserved.
 */

#if defined(_KERNEL) && defined(__x86_64__)
#include <asm/i387.h>
#include <asm/cpufeature.h>
#endif
#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>

#if defined(_KERNEL)
#define	kfpu_begin() kernel_fpu_begin()
#define	kfpu_end() kernel_fpu_end()
#else
#define	kfpu_begin() ((void)0)
#define	kfpu_end() ((void)0)
#endif

#include <sys/vdev_raidz.h>

#if defined(__x86_64__) && defined(_KERNEL) && defined(CONFIG_AS_AVX)
#define	MAKE_CST32_AVX128(reg, val)			\
	asm volatile("vmovd %0,%%"#reg : : "r"(val));	\
	asm volatile("vpshufd $0,%"#reg",%"#reg)

#define	COPY8P_AVX128						\
	asm volatile("vmovdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("vmovdqa %0,%%xmm1" : : "m" (*(src+2)));	\
	asm volatile("vmovdqa %0,%%xmm2" : : "m" (*(src+4)));	\
	asm volatile("vmovdqa %0,%%xmm3" : : "m" (*(src+6)));	\
	asm volatile("vmovdqa %%xmm0, %0" : "=m" (*(p+0)));	\
	asm volatile("vmovdqa %%xmm1, %0" : "=m" (*(p+2)));	\
	asm volatile("vmovdqa %%xmm2, %0" : "=m" (*(p+4)));	\
	asm volatile("vmovdqa %%xmm3, %0" : "=m" (*(p+6)))

#define	COPY8PQ_AVX128						\
	asm volatile("vmovdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("vmovdqa %0,%%xmm1" : : "m" (*(src+2)));	\
	asm volatile("vmovdqa %0,%%xmm2" : : "m" (*(src+4)));	\
	asm volatile("vmovdqa %0,%%xmm3" : : "m" (*(src+6)));	\
	asm volatile("vmovdqa %%xmm0, %0" : "=m" (*(p+0)));	\
	asm volatile("vmovdqa %%xmm1, %0" : "=m" (*(p+2)));	\
	asm volatile("vmovdqa %%xmm2, %0" : "=m" (*(p+4)));	\
	asm volatile("vmovdqa %%xmm3, %0" : "=m" (*(p+6)));	\
	asm volatile("vmovdqa %%xmm0, %0" : "=m" (*(q+0)));	\
	asm volatile("vmovdqa %%xmm1, %0" : "=m" (*(q+2)));	\
	asm volatile("vmovdqa %%xmm2, %0" : "=m" (*(q+4)));	\
	asm volatile("vmovdqa %%xmm3, %0" : "=m" (*(q+6)))

#define	COPY8PQR_AVX128						\
	asm volatile("vmovdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("vmovdqa %0,%%xmm1" : : "m" (*(src+2)));	\
	asm volatile("vmovdqa %0,%%xmm2" : : "m" (*(src+4)));	\
	asm volatile("vmovdqa %0,%%xmm3" : : "m" (*(src+6)));	\
	asm volatile("vmovdqa %%xmm0, %0" : "=m" (*(p+0)));	\
	asm volatile("vmovdqa %%xmm1, %0" : "=m" (*(p+2)));	\
	asm volatile("vmovdqa %%xmm2, %0" : "=m" (*(p+4)));	\
	asm volatile("vmovdqa %%xmm3, %0" : "=m" (*(p+6)));	\
	asm volatile("vmovdqa %%xmm0, %0" : "=m" (*(q+0)));	\
	asm volatile("vmovdqa %%xmm1, %0" : "=m" (*(q+2)));	\
	asm volatile("vmovdqa %%xmm2, %0" : "=m" (*(q+4)));	\
	asm volatile("vmovdqa %%xmm3, %0" : "=m" (*(q+6)));	\
	asm volatile("vmovdqa %%xmm0, %0" : "=m" (*(r+0)));	\
	asm volatile("vmovdqa %%xmm1, %0" : "=m" (*(r+2)));	\
	asm volatile("vmovdqa %%xmm2, %0" : "=m" (*(r+4)));	\
	asm volatile("vmovdqa %%xmm3, %0" : "=m" (*(r+6)))

#define	LOAD8_SRC_AVX128					\
	asm volatile("vmovdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("vmovdqa %0,%%xmm4" : : "m" (*(src+2)));	\
	asm volatile("vmovdqa %0,%%xmm8" : : "m" (*(src+4)));	\
	asm volatile("vmovdqa %0,%%xmm12" : : "m" (*(src+6)))

#define	COMPUTE8_P_AVX128					\
	asm volatile("vmovdqa %0,%%xmm1" : : "m" (*(p+0)));	\
	asm volatile("vmovdqa %0,%%xmm5" : : "m" (*(p+2)));	\
	asm volatile("vmovdqa %0,%%xmm9" : : "m" (*(p+4)));	\
	asm volatile("vmovdqa %0,%%xmm13" : : "m" (*(p+6)));	\
	asm volatile("vpxor %xmm0,%xmm1,%xmm1");		\
	asm volatile("vpxor %xmm4,%xmm5,%xmm5");		\
	asm volatile("vpxor %xmm8,%xmm9,%xmm9");		\
	asm volatile("vpxor %xmm12,%xmm13,%xmm13");		\
	asm volatile("vmovdqa %%xmm1,%0" : "=m" (*(p+0)));	\
	asm volatile("vmovdqa %%xmm5,%0" : "=m" (*(p+2)));	\
	asm volatile("vmovdqa %%xmm9,%0" : "=m" (*(p+4)));	\
	asm volatile("vmovdqa %%xmm13,%0" : "=m" (*(p+6)))

#define	COMPUTE8_Q_AVX128						\
	asm volatile("vmovdqa %0,%%xmm1" : : "m" (*(q+0)));		\
	asm volatile("vmovdqa %0,%%xmm5" : : "m" (*(q+2)));		\
	asm volatile("vmovdqa %0,%%xmm9" : : "m" (*(q+4)));		\
	asm volatile("vmovdqa %0,%%xmm13" : : "m" (*(q+6)));		\
	MAKE_CST32_AVX128(xmm3, 0x1d1d1d1d);				\
	asm volatile("vpxor %xmm14, %xmm14, %xmm14");			\
	asm volatile("vpcmpgtb %xmm1, %xmm14, %xmm2");			\
	asm volatile("vpcmpgtb %xmm5, %xmm14, %xmm6");			\
	asm volatile("vpcmpgtb %xmm9, %xmm14, %xmm10");			\
	asm volatile("vpcmpgtb %xmm13, %xmm14, %xmm14");		\
	asm volatile("vpaddb %xmm1,%xmm1,%xmm1");			\
	asm volatile("vpaddb %xmm5,%xmm5,%xmm5");			\
	asm volatile("vpaddb %xmm9,%xmm9,%xmm9");			\
	asm volatile("vpaddb %xmm13,%xmm13,%xmm13");			\
	asm volatile("vpand %xmm3,%xmm2,%xmm2");			\
	asm volatile("vpand %xmm3,%xmm6,%xmm6");			\
	asm volatile("vpand %xmm3,%xmm10,%xmm10");			\
	asm volatile("vpand %xmm3,%xmm14,%xmm14");			\
	asm volatile("vpxor %xmm2,%xmm1,%xmm1");			\
	asm volatile("vpxor %xmm6,%xmm5,%xmm5");			\
	asm volatile("vpxor %xmm10,%xmm9,%xmm9");			\
	asm volatile("vpxor %xmm14,%xmm13,%xmm13");			\
	asm volatile("vpxor %xmm0,%xmm1,%xmm1");			\
	asm volatile("vpxor %xmm4,%xmm5,%xmm5");			\
	asm volatile("vpxor %xmm8,%xmm9,%xmm9");			\
	asm volatile("vpxor %xmm12,%xmm13,%xmm13");			\
	asm volatile("vmovdqa %%xmm1,%0" : "=m" (*(q+0)));		\
	asm volatile("vmovdqa %%xmm5,%0" : "=m" (*(q+2)));		\
	asm volatile("vmovdqa %%xmm9,%0" : "=m" (*(q+4)));		\
	asm volatile("vmovdqa %%xmm13,%0" : "=m" (*(q+6)))

#define	COMPUTE8_R_AVX128						\
	asm volatile("vmovdqa %0,%%xmm1" : : "m" (*(r+0)));		\
	asm volatile("vmovdqa %0,%%xmm5" : : "m" (*(r+2)));		\
	asm volatile("vmovdqa %0,%%xmm9" : : "m" (*(r+4)));		\
	asm volatile("vmovdqa %0,%%xmm13" : : "m" (*(r+6)));		\
	MAKE_CST32_AVX128(xmm3, 0x1d1d1d1d);				\
	for (j = 0; j < 2; j++) {					\
		asm volatile("vpxor %xmm14, %xmm14, %xmm14");		\
		asm volatile("vpcmpgtb %xmm1, %xmm14, %xmm2");		\
		asm volatile("vpcmpgtb %xmm5, %xmm14, %xmm6");		\
		asm volatile("vpcmpgtb %xmm9, %xmm14, %xmm10");		\
		asm volatile("vpcmpgtb %xmm13, %xmm14, %xmm14");	\
		asm volatile("vpaddb %xmm1,%xmm1,%xmm1");		\
		asm volatile("vpaddb %xmm5,%xmm5,%xmm5");		\
		asm volatile("vpaddb %xmm9,%xmm9,%xmm9");		\
		asm volatile("vpaddb %xmm13,%xmm13,%xmm13");		\
		asm volatile("vpand %xmm3,%xmm2,%xmm2");		\
		asm volatile("vpand %xmm3,%xmm6,%xmm6");		\
		asm volatile("vpand %xmm3,%xmm10,%xmm10");		\
		asm volatile("vpand %xmm3,%xmm14,%xmm14");		\
		asm volatile("vpxor %xmm2,%xmm1,%xmm1");		\
		asm volatile("vpxor %xmm6,%xmm5,%xmm5");		\
		asm volatile("vpxor %xmm10,%xmm9,%xmm9");		\
		asm volatile("vpxor %xmm14,%xmm13,%xmm13");		\
	}								\
	asm volatile("vpxor %xmm0,%xmm1,%xmm1");			\
	asm volatile("vpxor %xmm4,%xmm5,%xmm5");			\
	asm volatile("vpxor %xmm8,%xmm9,%xmm9");			\
	asm volatile("vpxor %xmm12,%xmm13,%xmm13");			\
	asm volatile("vmovdqa %%xmm1,%0" : "=m" (*(r+0)));		\
	asm volatile("vmovdqa %%xmm5,%0" : "=m" (*(r+2)));		\
	asm volatile("vmovdqa %%xmm9,%0" : "=m" (*(r+4)));		\
	asm volatile("vmovdqa %%xmm13,%0" : "=m" (*(r+6)))

void
vdev_raidz_generate_parity_p_avx128(raidz_map_t *rm)
{
	uint64_t *p, *src, pcount, ccount, i;
	int c;

	pcount = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	kfpu_begin();
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		ccount = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccount == pcount);
			i = 0;
			if (ccount > 7) /* ccount is unsigned */
			for (; i < ccount-7; i += 8, src += 8, p += 8) {
				COPY8P_AVX128;
			}
			for (; i < ccount; i++, src++, p++) {
				*p = *src;
			}
		} else {
			ASSERT(ccount <= pcount);
			i = 0;
			if (ccount > 7) /* ccount is unsigned */
			for (; i < ccount-7; i += 8, src += 8, p += 8) {
				LOAD8_SRC_AVX128;
				COMPUTE8_P_AVX128;
			}
			for (; i < ccount; i++, src++, p++) {
				*p ^= *src;
			}
		}
	}
	kfpu_end();
}

void
vdev_raidz_generate_parity_pq_avx128(raidz_map_t *rm)
{
	uint64_t *p, *q, *src, pcnt, ccnt, mask, i;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	kfpu_begin();
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		q = rm->rm_col[VDEV_RAIDZ_Q].rc_data;

		ccnt = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			i = 0;
			if (ccnt > 7) /* ccnt is unsigned */
			for (; i < ccnt-7; i += 8, src += 8, p += 8, q += 8) {
				COPY8PQ_AVX128;
			}
			for (; i < ccnt; i++, src++, p++, q++) {
				*p = *src;
				*q = *src;
			}
			for (; i < pcnt; i++, src++, p++, q++) {
				*p = 0;
				*q = 0;
			}
		} else {
			ASSERT(ccnt <= pcnt);

			/*
			 * Apply the algorithm described above by multiplying
			 * the previous result and adding in the new value.
			 */
			i = 0;
			if (ccnt > 7) /* ccnt is unsigned */
			for (; i < ccnt-7; i += 8, src += 8, p += 8, q += 8) {
				LOAD8_SRC_AVX128;
				COMPUTE8_P_AVX128;
				COMPUTE8_Q_AVX128;
			}
			for (; i < ccnt; i++, src++, p++, q++) {
				*p ^= *src;

				VDEV_RAIDZ_64MUL_2(*q, mask);
				*q ^= *src;
			}

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			for (; i < pcnt; i++, q++) {
				VDEV_RAIDZ_64MUL_2(*q, mask);
			}
		}
	}
	kfpu_end();
}

void
vdev_raidz_generate_parity_pqr_avx128(raidz_map_t *rm)
{
	uint64_t *p, *q, *r, *src, pcnt, ccnt, mask, i, j;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_R].rc_size);
	kfpu_begin();
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		q = rm->rm_col[VDEV_RAIDZ_Q].rc_data;
		r = rm->rm_col[VDEV_RAIDZ_R].rc_data;

		ccnt = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			i = 0;
			if (ccnt > 7) /* ccnt is unsigned */
			for (; i < ccnt-7; i += 8, src += 8, p += 8,
							q += 8, r += 8) {
				COPY8PQR_AVX128;
			}
			for (; i < ccnt; i++, src++, p++, q++, r++) {
				*p = *src;
				*q = *src;
				*r = *src;
			}
			for (; i < pcnt; i++, src++, p++, q++, r++) {
				*p = 0;
				*q = 0;
				*r = 0;
			}
		} else {
			ASSERT(ccnt <= pcnt);

			/*
			 * Apply the algorithm described above by multiplying
			 * the previous result and adding in the new value.
			 */
			i = 0;
			if (ccnt > 7) /* ccnt is unsigned */
			for (; i < ccnt-7; i += 8, src += 8, p += 8,
							q += 8, r += 8) {
				LOAD8_SRC_AVX128;
				COMPUTE8_P_AVX128;
				COMPUTE8_Q_AVX128;
				COMPUTE8_R_AVX128;
			}
			for (; i < ccnt; i++, src++, p++, q++, r++) {
				*p ^= *src;

				VDEV_RAIDZ_64MUL_2(*q, mask);
				*q ^= *src;

				VDEV_RAIDZ_64MUL_4(*r, mask);
				*r ^= *src;
			}

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			for (; i < pcnt; i++, q++, r++) {
				VDEV_RAIDZ_64MUL_2(*q, mask);
				VDEV_RAIDZ_64MUL_4(*r, mask);
			}
		}
	}
	kfpu_end();
}
#endif
