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
 */
/*
 * Copyright (c) 2020, 2021, 2022 by Pawel Jakub Dawidek
 */

#ifndef _SYS_BRT_H
#define	_SYS_BRT_H

#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/dmu.h>

#ifdef	__cplusplus
extern "C" {
#endif

extern boolean_t brt_entry_decref(spa_t *spa, const blkptr_t *bp);
extern uint64_t brt_entry_get_refcount(spa_t *spa, const blkptr_t *bp);

extern uint64_t brt_get_dspace(spa_t *spa);
extern uint64_t brt_get_used(spa_t *spa);
extern uint64_t brt_get_saved(spa_t *spa);
extern uint64_t brt_get_ratio(spa_t *spa);

extern boolean_t brt_maybe_exists(spa_t *spa, const blkptr_t *bp);
extern void brt_init(void);
extern void brt_fini(void);

extern void brt_pending_add(spa_t *spa, const blkptr_t *bp, dmu_tx_t *tx);
extern void brt_pending_remove(spa_t *spa, const blkptr_t *bp, dmu_tx_t *tx);
extern void brt_pending_apply(spa_t *spa, uint64_t txg);

extern void brt_create(spa_t *spa);
extern int brt_load(spa_t *spa);
extern void brt_unload(spa_t *spa);
extern void brt_sync(spa_t *spa, uint64_t txg);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_BRT_H */
