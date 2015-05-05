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

#if defined(__x86_64__)
#define	MAKE_CST32_SSE(reg, val)			\
	asm volatile("movd %0,%%"#reg : : "r"(val));	\
	asm volatile("pshufd $0,%"#reg",%"#reg)

#define	COPY8P_SSE						\
	asm volatile("movdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("movdqa %0,%%xmm1" : : "m" (*(src+2)));	\
	asm volatile("movdqa %0,%%xmm2" : : "m" (*(src+4)));	\
	asm volatile("movdqa %0,%%xmm3" : : "m" (*(src+6)));	\
	asm volatile("movdqa %%xmm0, %0" : "=m" (*(p+0)));	\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(p+2)));	\
	asm volatile("movdqa %%xmm2, %0" : "=m" (*(p+4)));	\
	asm volatile("movdqa %%xmm3, %0" : "=m" (*(p+6)))

#define	COPY8PQ_SSE						\
	asm volatile("movdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("movdqa %0,%%xmm1" : : "m" (*(src+2)));	\
	asm volatile("movdqa %0,%%xmm2" : : "m" (*(src+4)));	\
	asm volatile("movdqa %0,%%xmm3" : : "m" (*(src+6)));	\
	asm volatile("movdqa %%xmm0, %0" : "=m" (*(p+0)));	\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(p+2)));	\
	asm volatile("movdqa %%xmm2, %0" : "=m" (*(p+4)));	\
	asm volatile("movdqa %%xmm3, %0" : "=m" (*(p+6)));	\
	asm volatile("movdqa %%xmm0, %0" : "=m" (*(q+0)));	\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(q+2)));	\
	asm volatile("movdqa %%xmm2, %0" : "=m" (*(q+4)));	\
	asm volatile("movdqa %%xmm3, %0" : "=m" (*(q+6)))

#define	COPY8PQR_SSE						\
	asm volatile("movdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("movdqa %0,%%xmm1" : : "m" (*(src+2)));	\
	asm volatile("movdqa %0,%%xmm2" : : "m" (*(src+4)));	\
	asm volatile("movdqa %0,%%xmm3" : : "m" (*(src+6)));	\
	asm volatile("movdqa %%xmm0, %0" : "=m" (*(p+0)));	\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(p+2)));	\
	asm volatile("movdqa %%xmm2, %0" : "=m" (*(p+4)));	\
	asm volatile("movdqa %%xmm3, %0" : "=m" (*(p+6)));	\
	asm volatile("movdqa %%xmm0, %0" : "=m" (*(q+0)));	\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(q+2)));	\
	asm volatile("movdqa %%xmm2, %0" : "=m" (*(q+4)));	\
	asm volatile("movdqa %%xmm3, %0" : "=m" (*(q+6)));	\
	asm volatile("movdqa %%xmm0, %0" : "=m" (*(r+0)));	\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(r+2)));	\
	asm volatile("movdqa %%xmm2, %0" : "=m" (*(r+4)));	\
	asm volatile("movdqa %%xmm3, %0" : "=m" (*(r+6)))

#define	LOAD8_SRC_SSE						\
	asm volatile("movdqa %0,%%xmm0" : : "m" (*(src+0)));	\
	asm volatile("movdqa %0,%%xmm4" : : "m" (*(src+2)));	\
	asm volatile("movdqa %0,%%xmm8" : : "m" (*(src+4)));	\
	asm volatile("movdqa %0,%%xmm12" : : "m" (*(src+6)))

#define	COMPUTE8_P_SSE						\
	asm volatile("movdqa %0,%%xmm1" : : "m" (*(p+0)));	\
	asm volatile("movdqa %0,%%xmm5" : : "m" (*(p+2)));	\
	asm volatile("movdqa %0,%%xmm9" : : "m" (*(p+4)));	\
	asm volatile("movdqa %0,%%xmm13" : : "m" (*(p+6)));	\
	asm volatile("pxor %xmm0,%xmm1");			\
	asm volatile("pxor %xmm4,%xmm5");			\
	asm volatile("pxor %xmm8,%xmm9");			\
	asm volatile("pxor %xmm12,%xmm13");			\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(p+0)));	\
	asm volatile("movdqa %%xmm5, %0" : "=m" (*(p+2)));	\
	asm volatile("movdqa %%xmm9, %0" : "=m" (*(p+4)));	\
	asm volatile("movdqa %%xmm13, %0" : "=m" (*(p+6)))

#define	COMPUTE8_Q_SSE							\
	asm volatile("movdqa %0,%%xmm1" : : "m" (*(q+0)));		\
	asm volatile("movdqa %0,%%xmm5" : : "m" (*(q+2)));		\
	asm volatile("movdqa %0,%%xmm9" : : "m" (*(q+4)));		\
	asm volatile("movdqa %0,%%xmm13" : : "m" (*(q+6)));		\
	MAKE_CST32_SSE(xmm3, 0x1d1d1d1d);				\
	asm volatile("pxor %xmm2, %xmm2");				\
	asm volatile("pxor %xmm6, %xmm6");				\
	asm volatile("pxor %xmm10, %xmm10");				\
	asm volatile("pxor %xmm14, %xmm14");				\
	asm volatile("pcmpgtb %xmm1, %xmm2");				\
	asm volatile("pcmpgtb %xmm5, %xmm6");				\
	asm volatile("pcmpgtb %xmm9, %xmm10");				\
	asm volatile("pcmpgtb %xmm13, %xmm14");				\
	asm volatile("paddb %xmm1,%xmm1");	      			\
	asm volatile("paddb %xmm5,%xmm5");				\
	asm volatile("paddb %xmm9,%xmm9");				\
	asm volatile("paddb %xmm13,%xmm13");				\
	asm volatile("pand %xmm3,%xmm2");			       	\
	asm volatile("pand %xmm3,%xmm6");				\
	asm volatile("pand %xmm3,%xmm10");				\
	asm volatile("pand %xmm3,%xmm14");				\
	asm volatile("pxor %xmm2, %xmm1");				\
	asm volatile("pxor %xmm6, %xmm5");				\
	asm volatile("pxor %xmm10, %xmm9");				\
	asm volatile("pxor %xmm14, %xmm13");				\
	asm volatile("pxor %xmm0, %xmm1");			 	\
	asm volatile("pxor %xmm4, %xmm5");				\
	asm volatile("pxor %xmm8, %xmm9");				\
	asm volatile("pxor %xmm12, %xmm13");				\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(q+0)));		\
	asm volatile("movdqa %%xmm5, %0" : "=m" (*(q+2)));		\
	asm volatile("movdqa %%xmm9, %0" : "=m" (*(q+4)));		\
	asm volatile("movdqa %%xmm13, %0" : "=m" (*(q+6)))

#define	COMPUTE8_R_SSE							\
	asm volatile("movdqa %0,%%xmm1" : : "m" (*(r+0)));		\
	asm volatile("movdqa %0,%%xmm5" : : "m" (*(r+2)));		\
	asm volatile("movdqa %0,%%xmm9" : : "m" (*(r+4)));		\
	asm volatile("movdqa %0,%%xmm13" : : "m" (*(r+6)));		\
	MAKE_CST32_SSE(xmm3, 0x1d1d1d1d);				\
	for (j = 0; j < 2; j++) {					\
		asm volatile("pxor %xmm2, %xmm2");		       	\
		asm volatile("pxor %xmm6, %xmm6");			\
		asm volatile("pxor %xmm10, %xmm10");			\
		asm volatile("pxor %xmm14, %xmm14");			\
		asm volatile("pcmpgtb %xmm1, %xmm2");			\
		asm volatile("pcmpgtb %xmm5, %xmm6");			\
		asm volatile("pcmpgtb %xmm9, %xmm10");			\
		asm volatile("pcmpgtb %xmm13, %xmm14");			\
		asm volatile("paddb %xmm1,%xmm1");			\
		asm volatile("paddb %xmm5,%xmm5");			\
		asm volatile("paddb %xmm9,%xmm9");			\
		asm volatile("paddb %xmm13,%xmm13");			\
		asm volatile("pand %xmm3,%xmm2");		       	\
		asm volatile("pand %xmm3,%xmm6");			\
		asm volatile("pand %xmm3,%xmm10");			\
		asm volatile("pand %xmm3,%xmm14");			\
		asm volatile("pxor %xmm2, %xmm1");			\
		asm volatile("pxor %xmm6, %xmm5");			\
		asm volatile("pxor %xmm10, %xmm9");			\
		asm volatile("pxor %xmm14, %xmm13");			\
	}								\
	asm volatile("pxor %xmm0, %xmm1");				\
	asm volatile("pxor %xmm4, %xmm5");				\
	asm volatile("pxor %xmm8, %xmm9");				\
	asm volatile("pxor %xmm12, %xmm13");				\
	asm volatile("movdqa %%xmm1, %0" : "=m" (*(r+0)));		\
	asm volatile("movdqa %%xmm5, %0" : "=m" (*(r+2)));		\
	asm volatile("movdqa %%xmm9, %0" : "=m" (*(r+4)));		\
	asm volatile("movdqa %%xmm13, %0" : "=m" (*(r+6)))

void
vdev_raidz_generate_parity_p_sse(raidz_map_t *rm)
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
				COPY8P_SSE;
			}
			for (; i < ccount; i++, src++, p++) {
				*p = *src;
			}
		} else {
			ASSERT(ccount <= pcount);
			i = 0;
			if (ccount > 7) /* ccount is unsigned */
			for (; i < ccount-7; i += 8, src += 8, p += 8) {
				LOAD8_SRC_SSE;
				COMPUTE8_P_SSE;
			}
			for (; i < ccount; i++, src++, p++) {
				*p ^= *src;
			}
		}
	}
	kfpu_end();
}

void
vdev_raidz_generate_parity_pq_sse(raidz_map_t *rm)
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
				COPY8PQ_SSE;
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
				LOAD8_SRC_SSE;
				COMPUTE8_P_SSE;
				COMPUTE8_Q_SSE;
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
vdev_raidz_generate_parity_pqr_sse(raidz_map_t *rm)
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
				COPY8PQR_SSE;
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
				LOAD8_SRC_SSE;
				COMPUTE8_P_SSE;
				COMPUTE8_Q_SSE;
				COMPUTE8_R_SSE;
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
