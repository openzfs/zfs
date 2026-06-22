// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 *
 * Copyright 2026, tiehexue <tiehexue@hotmail.com>. All rights reserved.
 *
 */

#ifndef	_SYS_DMU_OS_H
#define	_SYS_DMU_OS_H

#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Async Direct I/O completion callback type (shared by read and write).
 */
typedef void (dmu_abd_done_func_t)(void *arg, int error);

/*
 * Async Direct I/O read.  Submits reads via the ZIO pipeline and returns
 * immediately.  The completion callback fires from ZIO taskq context when
 * all reads finish.  Caller retains ownership of 'data' until callback.
 */
int dmu_read_abd_async(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, dmu_flags_t flags,
    dmu_abd_done_func_t *done, void *done_arg);

/*
 * Async Direct I/O write.  Submits writes via the ZIO pipeline and returns
 * immediately.  The completion callback fires from ZIO taskq context when
 * all writes finish.  Caller retains ownership of 'data' until callback
 * and must commit the transaction (tx) from the callback.
 */
int dmu_write_abd_async(dnode_t *dn, uint64_t offset, uint64_t size,
    abd_t *data, dmu_flags_t flags, dmu_tx_t *tx,
    dmu_abd_done_func_t *done, void *done_arg);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DMU_OS_H */
