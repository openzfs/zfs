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

#ifndef _SYS_ADAPTIVE_COMPRESS_H
#define	_SYS_ADAPTIVE_COMPRESS_H

#define	COMPRESS_ADAPTIVE_LEVELS 10

#include <sys/spa.h>
#include <sys/zio.h>

size_t compress_adaptive(zio_t *zio, abd_t *src, void *dst,
	size_t s_len, enum zio_compress *c);

void compress_calc_avg_without_zero(uint64_t act, uint64_t *res, int n);

uint64_t compress_calc_Bps(uint64_t byte, hrtime_t delay);


#endif /* _SYS_ADAPTIVE_COMPRESS_H */
