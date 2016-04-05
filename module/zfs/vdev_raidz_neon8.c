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

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/vdev_raidz.h>

#if defined(__aarch64__)

#ifdef __KERNEL__
#include <asm/neon.h>
#else
#define kernel_neon_begin()
#define kernel_neon_end()
#endif
#endif

#if defined(__aarch64__)

#define MAKE_CST32_NEON8(reg, val)	\
	asm volatile("movi "#reg".16b, #"#val)

#define COPY8P_NEON8	\
	asm volatile("ldp q0, q1, %0" : : "Q"(*(src+0)));	\
	asm volatile("ldp q2, q3, %0" : : "Q"(*(src+4)));	\
	asm volatile("stp q0, q1, %0" : "=Q"(*(p+0)));		\
	asm volatile("stp q2, q3, %0" : "=Q"(*(p+4)))

#define COPY8PQ_NEON8	\
	asm volatile("ldp q0, q1, %0" : : "Q"(*(src+0)));	\
	asm volatile("ldp q2, q3, %0" : : "Q"(*(src+4)));	\
	asm volatile("stp q0, q1, %0" : "=Q"(*(p+0)));		\
	asm volatile("stp q2, q3, %0" : "=Q"(*(p+4)));		\
	asm volatile("stp q0, q1, %0" : "=Q"(*(q+0)));		\
	asm volatile("stp q2, q3, %0" : "=Q"(*(q+4)))

#define COPY8PQR_NEON8	\
	asm volatile("ldp q0, q1, %0" : : "Q"(*(src+0)));	\
	asm volatile("ldp q2, q3, %0" : : "Q"(*(src+4)));	\
	asm volatile("stp q0, q1, %0" : "=Q"(*(p+0)));		\
	asm volatile("stp q2, q3, %0" : "=Q"(*(p+4)));		\
	asm volatile("stp q0, q1, %0" : "=Q"(*(q+0)));		\
	asm volatile("stp q2, q3, %0" : "=Q"(*(q+4)));		\
	asm volatile("stp q0, q1, %0" : "=Q"(*(r+0)));		\
	asm volatile("stp q2, q3, %0" : "=Q"(*(r+4)))

#define LOAD8_SRC_NEON8	\
	asm volatile("ldp q0, q4, %0" : : "Q"(*(src+0)));	\
	asm volatile("ldp q8, q12, %0" : : "Q"(*(src+4)))

#define COMPUTE8_P_NEON8	\
	asm volatile("ldp q1, q5, %0" : : "Q"(*(p+0)));		\
	asm volatile("ldp q9, q13, %0" : : "Q"(*(p+4)));	\
	asm volatile("eor v1.16b, v1.16b, v0.16b");		\
	asm volatile("eor v5.16b, v5.16b, v4.16b");		\
	asm volatile("eor v9.16b, v9.16b, v8.16b");		\
	asm volatile("eor v13.16b, v13.16b, v12.16b");		\
	asm volatile("stp q1, q5, %0" : "=Q"(*(p+0)));		\
	asm volatile("stp q9, q13, %0" : "=Q"(*(p+4)))

#define COMPUTE8_Q_NEON8	\
	asm volatile("ldp q1, q5, %0" : : "Q"(*(q+0)));		\
	asm volatile("ldp q9, q13, %0" : : "Q"(*(q+4)));	\
	MAKE_CST32_NEON8(v3, 0x1d);				\
	asm volatile("eor v2.16b, v2.16b, v2.16b");		\
	asm volatile("eor v6.16b, v6.16b, v6.16b");		\
	asm volatile("eor v10.16b, v10.16b, v10.16b");		\
	asm volatile("eor v14.16b, v14.16b, v14.16b");		\
	asm volatile("cmgt v2.16b, v2.16b, v1.16b");		\
	asm volatile("cmgt v6.16b, v6.16b, v5.16b");		\
	asm volatile("cmgt v10.16b, v10.16b, v9.16b");		\
	asm volatile("cmgt v14.16b, v14.16b, v13.16b");		\
	asm volatile("shl v1.16b, v1.16b, #1");			\
	asm volatile("shl v5.16b, v5.16b, #1");			\
	asm volatile("shl v9.16b, v9.16b, #1");			\
	asm volatile("shl v13.16b, v13.16b, #1");		\
	asm volatile("and v2.16b, v2.16b, v3.16b");		\
	asm volatile("and v6.16b, v6.16b, v3.16b");		\
	asm volatile("and v10.16b, v10.16b, v3.16b");		\
	asm volatile("and v14.16b, v14.16b, v3.16b");		\
	asm volatile("eor v1.16b, v1.16b, v2.16b");		\
	asm volatile("eor v5.16b, v5.16b, v6.16b");		\
	asm volatile("eor v9.16b, v9.16b, v10.16b");		\
	asm volatile("eor v13.16b, v13.16b, v14.16b");		\
	asm volatile("eor v1.16b, v1.16b, v0.16b");		\
	asm volatile("eor v5.16b, v5.16b, v4.16b");		\
	asm volatile("eor v9.16b, v9.16b, v8.16b");		\
	asm volatile("eor v13.16b, v13.16b, v12.16b");		\
	asm volatile("stp q1, q5, %0" : "=Q"(*(q+0)));		\
	asm volatile("stp q9, q13, %0" : "=Q"(*(q+4)))

#define COMPUTE8_R_NEON8	\
	asm volatile("ldp q1, q5, %0" : : "Q"(*(r+0)));		\
	asm volatile("ldp q9, q13, %0" : : "Q"(*(r+4)));	\
	MAKE_CST32_NEON8(v3, 0x1d);				\
	for ( j = 0; j < 2; j++ ) {				\
		asm volatile("eor v2.16b, v2.16b, v2.16b");	\
		asm volatile("eor v6.16b, v6.16b, v6.16b");	\
		asm volatile("eor v10.16b, v10.16b, v10.16b");	\
		asm volatile("eor v14.16b, v14.16b, v14.16b");	\
		asm volatile("cmgt v2.16b, v2.16b, v1.16b");	\
		asm volatile("cmgt v6.16b, v6.16b, v5.16b");	\
		asm volatile("cmgt v10.16b, v10.16b, v9.16b");	\
		asm volatile("cmgt v14.16b, v14.16b, v13.16b");	\
		asm volatile("shl v1.16b, v1.16b, #1");		\
		asm volatile("shl v5.16b, v5.16b, #1");		\
		asm volatile("shl v9.16b, v9.16b, #1");		\
		asm volatile("shl v13.16b, v13.16b, #1");	\
		asm volatile("and v2.16b, v2.16b, v3.16b");	\
		asm volatile("and v6.16b, v6.16b, v3.16b");	\
		asm volatile("and v10.16b, v10.16b, v3.16b");	\
		asm volatile("and v14.16b, v14.16b, v3.16b");	\
		asm volatile("eor v1.16b, v1.16b, v2.16b");	\
		asm volatile("eor v5.16b, v5.16b, v6.16b");	\
		asm volatile("eor v9.16b, v9.16b, v10.16b");	\
		asm volatile("eor v13.16b, v13.16b, v14.16b");	\
	}							\
	asm volatile("eor v1.16b, v1.16b, v0.16b");		\
	asm volatile("eor v5.16b, v5.16b, v4.16b");		\
	asm volatile("eor v9.16b, v9.16b, v8.16b");		\
	asm volatile("eor v13.16b, v13.16b, v12.16b");		\
	asm volatile("stp q1, q5, %0" : "=Q"(*(r+0)));		\
	asm volatile("stp q9, q13, %0" : "=Q"(*(r+4)))


void
vdev_raidz_generate_parity_p_neon8(raidz_map_t *rm)
{
	uint64_t *p, *src, pcount, ccount, i;
	int c;

	pcount = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	kernel_neon_begin();
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		ccount = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccount == pcount);
			i = 0;
			if (ccount > 7) /* ccount is unsigned */
			{
				for (; i < ccount-7; i += 8, src += 8, p += 8) {
					COPY8P_NEON8;
				}
			}
			for (; i < ccount; i++, src++, p++) {
				*p = *src;
			}
		} else {
			ASSERT(ccount <= pcount);
			i = 0;
			if (ccount > 7) /* ccount is unsigned */
			{
				for (; i < ccount-7; i += 8, src += 8, p += 8) {
					LOAD8_SRC_NEON8;
					COMPUTE8_P_NEON8;
				}
			}
			for (; i < ccount; i++, src++, p++) {
				*p ^= *src;
			}
		}
	}
	kernel_neon_end();
}

void
vdev_raidz_generate_parity_pq_neon8(raidz_map_t *rm)
{
	uint64_t *p, *q, *src, pcnt, ccnt, mask, i;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	kernel_neon_begin();
	for (c = rm->rm_firstdatacol; c < rm->rm_cols; c++) {
		src = rm->rm_col[c].rc_data;
		p = rm->rm_col[VDEV_RAIDZ_P].rc_data;
		q = rm->rm_col[VDEV_RAIDZ_Q].rc_data;

		ccnt = rm->rm_col[c].rc_size / sizeof (src[0]);

		if (c == rm->rm_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			i = 0;
			if (ccnt > 7) /* ccnt is unsigned */
			{
				for (; i < ccnt-7; i += 8, src += 8, p += 8, q += 8) {
					COPY8PQ_NEON8;
				}
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
			{
				for (; i < ccnt-7; i += 8, src += 8, p += 8, q += 8) {
					LOAD8_SRC_NEON8;
					COMPUTE8_P_NEON8;
					COMPUTE8_Q_NEON8;
				}
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
	kernel_neon_end();
}

void
vdev_raidz_generate_parity_pqr_neon8(raidz_map_t *rm)
{
	uint64_t *p, *q, *r, *src, pcnt, ccnt, mask, i, j;
	int c;

	pcnt = rm->rm_col[VDEV_RAIDZ_P].rc_size / sizeof (src[0]);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rm->rm_col[VDEV_RAIDZ_P].rc_size ==
	    rm->rm_col[VDEV_RAIDZ_R].rc_size);
	kernel_neon_begin();
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
			{
				for (; i < ccnt-7; i += 8, src += 8, p += 8,
						q += 8, r += 8) {
					COPY8PQR_NEON8;
				}
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
			{
				for (; i < ccnt-7; i += 8, src += 8, p += 8,
						q += 8, r += 8) {
					LOAD8_SRC_NEON8;
					COMPUTE8_P_NEON8;
					COMPUTE8_Q_NEON8;
					COMPUTE8_R_NEON8;
				}
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
	kernel_neon_end();
}
#endif
