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

#if defined(__x86_64__) && defined(_KERNEL) && defined(CONFIG_AS_AVX2)
#define	MAKE_CST32_AVX2(regx, regy, val)		\
	asm volatile("vmovd %0,%%"#regx : : "r"(val));	\
	asm volatile("vpbroadcastd %"#regx",%"#regy)

#define	COPY16P_AVX2						\
    asm volatile("vmovdqa (%[src]), %%ymm0\n" \
                 "vmovdqa 32(%[src]), %%ymm1\n" \
                 "vmovdqa 64(%[src]), %%ymm2\n" \
                 "vmovdqa 96(%[src]), %%ymm3\n" \
                 "vmovdqa %%ymm0, (%[p])\n" \
                 "vmovdqa %%ymm1, 32(%[p])\n" \
                 "vmovdqa %%ymm2, 64(%[p])\n" \
                 "vmovdqa %%ymm3, 96(%[p])\n" \
            : \
            : [src] "r" (src), [p] "r" (p) \
            : "memory");

#define	COPY16PQ_AVX2						\
    asm volatile("vmovdqa (%[src]), %%ymm0\n" \
                 "vmovdqa 32(%[src]), %%ymm1\n" \
                 "vmovdqa 64(%[src]), %%ymm2\n" \
                 "vmovdqa 96(%[src]), %%ymm3\n" \
                 "vmovdqa %%ymm0, (%[p])\n" \
                 "vmovdqa %%ymm1, 32(%[p])\n" \
                 "vmovdqa %%ymm2, 64(%[p])\n" \
                 "vmovdqa %%ymm3, 96(%[p])\n" \
                 "vmovdqa %%ymm0, (%[q])\n" \
                 "vmovdqa %%ymm1, 32(%[q])\n" \
                 "vmovdqa %%ymm2, 64(%[q])\n" \
                 "vmovdqa %%ymm3, 96(%[q])\n" \
            : \
            : [src] "r" (src), [p] "r" (p), [q] "r" (q) \
            : "memory");

#define	COPY16PQR_AVX2						\
    asm volatile("vmovdqa (%[src]), %%ymm0\n" \
                 "vmovdqa 32(%[src]), %%ymm1\n" \
                 "vmovdqa 64(%[src]), %%ymm2\n" \
                 "vmovdqa 96(%[src]), %%ymm3\n" \
                 "vmovdqa %%ymm0, (%[p])\n" \
                 "vmovdqa %%ymm1, 32(%[p])\n" \
                 "vmovdqa %%ymm2, 64(%[p])\n" \
                 "vmovdqa %%ymm3, 96(%[p])\n" \
                 "vmovdqa %%ymm0, (%[q])\n" \
                 "vmovdqa %%ymm1, 32(%[q])\n" \
                 "vmovdqa %%ymm2, 64(%[q])\n" \
                 "vmovdqa %%ymm3, 96(%[q])\n" \
                 "vmovdqa %%ymm0, (%[r])\n" \
                 "vmovdqa %%ymm1, 32(%[r])\n" \
                 "vmovdqa %%ymm2, 64(%[r])\n" \
                 "vmovdqa %%ymm3, 96(%[r])\n" \
            : \
            : [src] "r" (src), [p] "r" (p), [q] "r" (q), [r] "r" (r) \
            : "memory");

#define	LOAD16_SRC_AVX2						\
    asm volatile("vmovdqa (%[src]), %%ymm0\n" \
                 "vmovdqa 32(%[src]), %%ymm4\n" \
                 "vmovdqa 64(%[src]), %%ymm8\n" \
                 "vmovdqa 96(%[src]), %%ymm12\n" \
            : \
            : [src] "r" (src) \
            : "memory");

#define	COMPUTE16_P_AVX2					\
    asm volatile("vmovdqa (%[p]), %%ymm1\n" \
                 "vmovdqa 32(%[p]), %%ymm5\n" \
                 "vmovdqa 64(%[p]), %%ymm9\n" \
                 "vmovdqa 96(%[p]), %%ymm13\n" \
                 "vpxor %%ymm0, %%ymm1, %%ymm1\n" \
                 "vpxor %%ymm4, %%ymm5, %%ymm5\n" \
                 "vpxor %%ymm8, %%ymm9, %%ymm9\n" \
                 "vpxor %%ymm12, %%ymm13, %%ymm13\n" \
                 "vmovdqa %%ymm1, (%[p])\n" \
                 "vmovdqa %%ymm5, 32(%[p])\n" \
                 "vmovdqa %%ymm9, 64(%[p])\n" \
                 "vmovdqa %%ymm13, 96(%[p])\n" \
            : \
            : [p] "r" (p) \
            : "memory");

#define	COMPUTE16_Q_AVX2						\
    asm volatile("vmovdqa (%[q]), %%ymm1\n" \
                 "vmovdqa 32(%[q]), %%ymm5\n" \
                 "vmovdqa 64(%[q]), %%ymm9\n" \
                 "vmovdqa 96(%[q]), %%ymm13\n" \
                 "vmovd %[cast], %%xmm3\n" \
                 "vpbroadcastd %%xmm3, %%ymm3\n" \
                 "vpxor %%ymm14, %%ymm14, %%ymm14\n" \
                 "vpcmpgtb %%ymm1, %%ymm14, %%ymm2\n" \
                 "vpcmpgtb %%ymm5, %%ymm14, %%ymm6\n" \
                 "vpcmpgtb %%ymm9, %%ymm14, %%ymm10\n" \
                 "vpcmpgtb %%ymm13, %%ymm14, %%ymm14\n" \
                 "vpaddb %%ymm1, %%ymm1, %%ymm1\n" \
                 "vpaddb %%ymm5, %%ymm5, %%ymm5\n" \
                 "vpaddb %%ymm9, %%ymm9, %%ymm9\n" \
                 "vpaddb %%ymm13, %%ymm13, %%ymm13\n" \
                 "vpand %%ymm3, %%ymm2, %%ymm2\n" \
                 "vpand %%ymm3, %%ymm6, %%ymm6\n" \
                 "vpand %%ymm3, %%ymm10, %%ymm10\n" \
                 "vpand %%ymm3, %%ymm14, %%ymm14\n" \
                 "vpxor %%ymm2, %%ymm1, %%ymm1\n" \
                 "vpxor %%ymm6, %%ymm5, %%ymm5\n" \
                 "vpxor %%ymm10, %%ymm9, %%ymm9\n" \
                 "vpxor %%ymm14, %%ymm13, %%ymm13\n" \
                 "vpxor %%ymm0, %%ymm1, %%ymm1\n" \
                 "vpxor %%ymm4, %%ymm5, %%ymm5\n" \
                 "vpxor %%ymm8, %%ymm9, %%ymm9\n" \
                 "vpxor %%ymm12, %%ymm13, %%ymm13\n" \
                 "vmovdqa %%ymm1, (%[q])\n" \
                 "vmovdqa %%ymm5, 32(%[q])\n" \
                 "vmovdqa %%ymm9, 64(%[q])\n" \
                 "vmovdqa %%ymm13, 96(%[q])\n" \
            : \
            : [q] "r" (q), [cast] "r" (0x1d1d1d1d) \
            : "memory");

#define	COMPUTE16_R_AVX2						\
    asm volatile("vmovdqa (%[r]), %%ymm1\n" \
                 "vmovdqa 32(%[r]), %%ymm5\n" \
                 "vmovdqa 64(%[r]), %%ymm9\n" \
                 "vmovdqa 96(%[r]), %%ymm13\n" \
                 "vmovd %[cast], %%xmm3\n" \
                 "vpbroadcastd %%xmm3, %%ymm3\n" \
                 "vpxor %%ymm14, %%ymm14, %%ymm14\n" \
                 "vpcmpgtb %%ymm1, %%ymm14, %%ymm2\n" \
                 "vpcmpgtb %%ymm5, %%ymm14, %%ymm6\n" \
                 "vpcmpgtb %%ymm9, %%ymm14, %%ymm10\n" \
                 "vpcmpgtb %%ymm13, %%ymm14, %%ymm14\n" \
                 "vpaddb %%ymm1, %%ymm1, %%ymm1\n" \
                 "vpaddb %%ymm5, %%ymm5, %%ymm5\n" \
                 "vpaddb %%ymm9, %%ymm9, %%ymm9\n" \
                 "vpaddb %%ymm13, %%ymm13, %%ymm13\n" \
                 "vpand %%ymm3, %%ymm2, %%ymm2\n" \
                 "vpand %%ymm3, %%ymm6, %%ymm6\n" \
                 "vpand %%ymm3, %%ymm10, %%ymm10\n" \
                 "vpand %%ymm3, %%ymm14, %%ymm14\n" \
                 "vpxor %%ymm2, %%ymm1, %%ymm1\n" \
                 "vpxor %%ymm6, %%ymm5, %%ymm5\n" \
                 "vpxor %%ymm10, %%ymm9, %%ymm9\n" \
                 "vpxor %%ymm14, %%ymm13, %%ymm13\n" \
                 "vpxor %%ymm14, %%ymm14, %%ymm14\n" \
                 "vpcmpgtb %%ymm1, %%ymm14, %%ymm2\n" \
                 "vpcmpgtb %%ymm5, %%ymm14, %%ymm6\n" \
                 "vpcmpgtb %%ymm9, %%ymm14, %%ymm10\n" \
                 "vpcmpgtb %%ymm13, %%ymm14, %%ymm14\n" \
                 "vpaddb %%ymm1, %%ymm1, %%ymm1\n" \
                 "vpaddb %%ymm5, %%ymm5, %%ymm5\n" \
                 "vpaddb %%ymm9, %%ymm9, %%ymm9\n" \
                 "vpaddb %%ymm13, %%ymm13, %%ymm13\n" \
                 "vpand %%ymm3, %%ymm2, %%ymm2\n" \
                 "vpand %%ymm3, %%ymm6, %%ymm6\n" \
                 "vpand %%ymm3, %%ymm10, %%ymm10\n" \
                 "vpand %%ymm3, %%ymm14, %%ymm14\n" \
                 "vpxor %%ymm2, %%ymm1, %%ymm1\n" \
                 "vpxor %%ymm6, %%ymm5, %%ymm5\n" \
                 "vpxor %%ymm10, %%ymm9, %%ymm9\n" \
                 "vpxor %%ymm14, %%ymm13, %%ymm13\n" \
                 "vpxor %%ymm0, %%ymm1, %%ymm1\n" \
                 "vpxor %%ymm4, %%ymm5, %%ymm5\n" \
                 "vpxor %%ymm8, %%ymm9, %%ymm9\n" \
                 "vpxor %%ymm12, %%ymm13, %%ymm13\n" \
                 "vmovdqa %%ymm1, (%[r])\n" \
                 "vmovdqa %%ymm5, 32(%[r])\n" \
                 "vmovdqa %%ymm9, 64(%[r])\n" \
                 "vmovdqa %%ymm13, 96(%[r])\n" \
            : \
            : [r] "r" (r), [cast] "r" (0x1d1d1d1d) \
            : "memory");

void
vdev_raidz_generate_parity_p_avx2(raidz_map_t *rm)
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
			if (ccount > 15) /* ccount is unsigned */
			for (; i < ccount-15; i += 16, src += 16, p += 16) {
				COPY16P_AVX2;
			}
			for (; i < ccount; i++, src++, p++) {
				*p = *src;
			}
		} else {
			ASSERT(ccount <= pcount);
			i = 0;
			if (ccount > 15) /* ccount is unsigned */
			for (; i < ccount-15; i += 16, src += 16, p += 16) {
				LOAD16_SRC_AVX2;
				COMPUTE16_P_AVX2;
			}
			for (; i < ccount; i++, src++, p++) {
				*p ^= *src;
			}
		}
	}
	kfpu_end();
}

void
vdev_raidz_generate_parity_pq_avx2(raidz_map_t *rm)
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
			if (ccnt > 15) /* ccnt is unsigned */
			for (; i < ccnt-15; i += 16, src += 16,
							p += 16, q += 16) {
				COPY16PQ_AVX2;
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
			if (ccnt > 15) /* ccnt is unsigned */
			for (; i < ccnt-15; i += 16, src += 16,
							p += 16, q += 16) {
				LOAD16_SRC_AVX2;
				COMPUTE16_P_AVX2;
				COMPUTE16_Q_AVX2;
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
vdev_raidz_generate_parity_pqr_avx2(raidz_map_t *rm)
{
	uint64_t *p, *q, *r, *src, pcnt, ccnt, mask, i;
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
			if (ccnt > 15) /* ccnt is unsigned */
			for (; i < ccnt-15; i += 16, src += 16,
					p += 16, q += 16, r += 16) {
				COPY16PQR_AVX2;
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
			if (ccnt > 15) /* ccnt is unsigned */
			for (; i < ccnt-15; i += 16, src += 16,
					p += 16, q += 16, r += 16) {
				LOAD16_SRC_AVX2;
				COMPUTE16_P_AVX2;
				COMPUTE16_Q_AVX2;
				COMPUTE16_R_AVX2;
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
