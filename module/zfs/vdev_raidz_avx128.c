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
#define	MAKE_CST32_AVX128			\
	asm volatile("vmovd %[cast], %%xmm7\n" \
                 "vpshufd $0, %%xmm7, %%xmm7\n" \
                : \
                : [cast] "r" (0x1d1d1d1d));

#define	COPY8P_AVX128						\
    asm volatile("vmovdqa (%[src]), %%xmm0\n" \
                 "vmovdqa 16(%[src]), %%xmm1\n" \
                 "vmovdqa 32(%[src]), %%xmm2\n" \
                 "vmovdqa 48(%[src]), %%xmm3\n" \
                 "vmovdqa %%xmm0, (%[p])\n" \
                 "vmovdqa %%xmm1, 16(%[p])\n" \
                 "vmovdqa %%xmm2, 32(%[p])\n" \
                 "vmovdqa %%xmm3, 48(%[p])\n" \
            : \
            : [src] "r" (src), [p] "r" (p) \
            : "memory");

#define	COPY8PQ_AVX128						\
    asm volatile("vmovdqa (%[src]), %%xmm0\n" \
                 "vmovdqa 16(%[src]), %%xmm1\n" \
                 "vmovdqa 32(%[src]), %%xmm2\n" \
                 "vmovdqa 48(%[src]), %%xmm3\n" \
                 "vmovdqa %%xmm0, (%[p])\n" \
                 "vmovdqa %%xmm1, 16(%[p])\n" \
                 "vmovdqa %%xmm2, 32(%[p])\n" \
                 "vmovdqa %%xmm3, 48(%[p])\n" \
                 "vmovdqa %%xmm0, (%[q])\n" \
                 "vmovdqa %%xmm1, 16(%[q])\n" \
                 "vmovdqa %%xmm2, 32(%[q])\n" \
                 "vmovdqa %%xmm3, 48(%[q])\n" \
            : \
            : [src] "r" (src), [p] "r" (p), [q] "r" (q) \
            : "memory");

#define	COPY8PQR_AVX128						\
    asm volatile("vmovdqa (%[src]), %%xmm0\n" \
                 "vmovdqa 16(%[src]), %%xmm1\n" \
                 "vmovdqa 32(%[src]), %%xmm2\n" \
                 "vmovdqa 48(%[src]), %%xmm3\n" \
                 "vmovdqa %%xmm0, (%[p])\n" \
                 "vmovdqa %%xmm1, 16(%[p])\n" \
                 "vmovdqa %%xmm2, 32(%[p])\n" \
                 "vmovdqa %%xmm3, 48(%[p])\n" \
                 "vmovdqa %%xmm0, (%[q])\n" \
                 "vmovdqa %%xmm1, 16(%[q])\n" \
                 "vmovdqa %%xmm2, 32(%[q])\n" \
                 "vmovdqa %%xmm3, 48(%[q])\n" \
                 "vmovdqa %%xmm0, (%[r])\n" \
                 "vmovdqa %%xmm1, 16(%[r])\n" \
                 "vmovdqa %%xmm2, 32(%[r])\n" \
                 "vmovdqa %%xmm3, 48(%[r])\n" \
            : \
            : [src] "r" (src), [p] "r" (p), [q] "r" (q), [r] "r" (r) \
            : "memory");

#define	LOAD8_SRC_AVX128						\
    asm volatile("vmovdqa (%[src]), %%xmm0\n" \
                 "vmovdqa 16(%[src]), %%xmm4\n" \
                 "vmovdqa 32(%[src]), %%xmm8\n" \
                 "vmovdqa 48(%[src]), %%xmm12\n" \
            : \
            : [src] "r" (src) \
            : "memory");

#define	COMPUTE8_P_AVX128					\
    asm volatile("vmovdqa (%[p]), %%xmm1\n" \
                 "vmovdqa 16(%[p]), %%xmm5\n" \
                 "vmovdqa 32(%[p]), %%xmm9\n" \
                 "vmovdqa 48(%[p]), %%xmm13\n" \
                 "vpxor %%xmm0, %%xmm1, %%xmm1\n" \
                 "vpxor %%xmm4, %%xmm5, %%xmm5\n" \
                 "vpxor %%xmm8, %%xmm9, %%xmm9\n" \
                 "vpxor %%xmm12, %%xmm13, %%xmm13\n" \
                 "vmovdqa %%xmm1, (%[p])\n" \
                 "vmovdqa %%xmm5, 16(%[p])\n" \
                 "vmovdqa %%xmm9, 32(%[p])\n" \
                 "vmovdqa %%xmm13, 48(%[p])\n" \
            : \
            : [p] "r" (p) \
            : "memory");

#define	COMPUTE8_Q_AVX128						\
    asm volatile("vmovdqa (%[q]), %%xmm1\n" \
                 "vmovdqa 16(%[q]), %%xmm5\n" \
                 "vmovdqa 32(%[q]), %%xmm9\n" \
                 "vmovdqa 48(%[q]), %%xmm13\n" \
                 "vpxor %%xmm14, %%xmm14, %%xmm14\n" \
                 "vpcmpgtb %%xmm1, %%xmm14, %%xmm2\n" \
                 "vpcmpgtb %%xmm5, %%xmm14, %%xmm6\n" \
                 "vpcmpgtb %%xmm9, %%xmm14, %%xmm10\n" \
                 "vpcmpgtb %%xmm13, %%xmm14, %%xmm14\n" \
                 "vpaddb %%xmm1, %%xmm1, %%xmm1\n" \
                 "vpaddb %%xmm5, %%xmm5, %%xmm5\n" \
                 "vpaddb %%xmm9, %%xmm9, %%xmm9\n" \
                 "vpaddb %%xmm13, %%xmm13, %%xmm13\n" \
                 "vpand %%xmm7, %%xmm2, %%xmm2\n" \
                 "vpand %%xmm7, %%xmm6, %%xmm6\n" \
                 "vpand %%xmm7, %%xmm10, %%xmm10\n" \
                 "vpand %%xmm7, %%xmm14, %%xmm14\n" \
                 "vpxor %%xmm2, %%xmm1, %%xmm1\n" \
                 "vpxor %%xmm6, %%xmm5, %%xmm5\n" \
                 "vpxor %%xmm10, %%xmm9, %%xmm9\n" \
                 "vpxor %%xmm14, %%xmm13, %%xmm13\n" \
                 "vpxor %%xmm0, %%xmm1, %%xmm1\n" \
                 "vpxor %%xmm4, %%xmm5, %%xmm5\n" \
                 "vpxor %%xmm8, %%xmm9, %%xmm9\n" \
                 "vpxor %%xmm12, %%xmm13, %%xmm13\n" \
                 "vmovdqa %%xmm1, (%[q])\n" \
                 "vmovdqa %%xmm5, 16(%[q])\n" \
                 "vmovdqa %%xmm9, 32(%[q])\n" \
                 "vmovdqa %%xmm13, 48(%[q])\n" \
            : \
            : [q] "r" (q) \
            : "memory");

#define	COMPUTE8_R_AVX128						\
    asm volatile("vmovdqa (%[r]), %%xmm1\n" \
                 "vmovdqa 16(%[r]), %%xmm5\n" \
                 "vmovdqa 32(%[r]), %%xmm9\n" \
                 "vmovdqa 48(%[r]), %%xmm13\n" \
                 "vpxor %%xmm14, %%xmm14, %%xmm14\n" \
                 "vpcmpgtb %%xmm1, %%xmm14, %%xmm2\n" \
                 "vpcmpgtb %%xmm5, %%xmm14, %%xmm6\n" \
                 "vpcmpgtb %%xmm9, %%xmm14, %%xmm10\n" \
                 "vpcmpgtb %%xmm13, %%xmm14, %%xmm14\n" \
                 "vpaddb %%xmm1, %%xmm1, %%xmm1\n" \
                 "vpaddb %%xmm5, %%xmm5, %%xmm5\n" \
                 "vpaddb %%xmm9, %%xmm9, %%xmm9\n" \
                 "vpaddb %%xmm13, %%xmm13, %%xmm13\n" \
                 "vpand %%xmm7, %%xmm2, %%xmm2\n" \
                 "vpand %%xmm7, %%xmm6, %%xmm6\n" \
                 "vpand %%xmm7, %%xmm10, %%xmm10\n" \
                 "vpand %%xmm7, %%xmm14, %%xmm14\n" \
                 "vpxor %%xmm2, %%xmm1, %%xmm1\n" \
                 "vpxor %%xmm6, %%xmm5, %%xmm5\n" \
                 "vpxor %%xmm10, %%xmm9, %%xmm9\n" \
                 "vpxor %%xmm14, %%xmm13, %%xmm13\n" \
                 "vpxor %%xmm14, %%xmm14, %%xmm14\n" \
                 "vpcmpgtb %%xmm1, %%xmm14, %%xmm2\n" \
                 "vpcmpgtb %%xmm5, %%xmm14, %%xmm6\n" \
                 "vpcmpgtb %%xmm9, %%xmm14, %%xmm10\n" \
                 "vpcmpgtb %%xmm13, %%xmm14, %%xmm14\n" \
                 "vpaddb %%xmm1, %%xmm1, %%xmm1\n" \
                 "vpaddb %%xmm5, %%xmm5, %%xmm5\n" \
                 "vpaddb %%xmm9, %%xmm9, %%xmm9\n" \
                 "vpaddb %%xmm13, %%xmm13, %%xmm13\n" \
                 "vpand %%xmm7, %%xmm2, %%xmm2\n" \
                 "vpand %%xmm7, %%xmm6, %%xmm6\n" \
                 "vpand %%xmm7, %%xmm10, %%xmm10\n" \
                 "vpand %%xmm7, %%xmm14, %%xmm14\n" \
                 "vpxor %%xmm2, %%xmm1, %%xmm1\n" \
                 "vpxor %%xmm6, %%xmm5, %%xmm5\n" \
                 "vpxor %%xmm10, %%xmm9, %%xmm9\n" \
                 "vpxor %%xmm14, %%xmm13, %%xmm13\n" \
                 "vpxor %%xmm0, %%xmm1, %%xmm1\n" \
                 "vpxor %%xmm4, %%xmm5, %%xmm5\n" \
                 "vpxor %%xmm8, %%xmm9, %%xmm9\n" \
                 "vpxor %%xmm12, %%xmm13, %%xmm13\n" \
                 "vmovdqa %%xmm1, (%[r])\n" \
                 "vmovdqa %%xmm5, 16(%[r])\n" \
                 "vmovdqa %%xmm9, 32(%[r])\n" \
                 "vmovdqa %%xmm13, 48(%[r])\n" \
            : \
            : [r] "r" (r) \
            : "memory");

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
    MAKE_CST32_AVX128;
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
	uint64_t *p, *q, *r, *src, pcnt, ccnt, mask, i;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_R].rc_size);
	kfpu_begin();
    MAKE_CST32_AVX128;
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
