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

#ifndef	_SYS_QAT_COMPRESS_H
#define	_SYS_QAT_COMPRESS_H

#ifdef	_KERNEL
#include "cpa.h"
#include "dc/cpa_dc.h"

extern int qat_init_done;
extern int qat_init(void);
extern void qat_fini(void);
extern int qat_compress(int dir, char *src, int src_len,
			    char *dst, int dst_len, size_t *c_len);
#else
#define	qat_init()
#define	qat_fini()
#define	qat_init_done	0
#define	qat_compress(dir, s, sl, d, dl, cl)	0
#define	CPA_STATUS_SUCCESS	0
#endif

#endif
