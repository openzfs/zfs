// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2013 by Delphix. All rights reserved.
 */

#ifndef _SYS_ZFEATURE_H
#define	_SYS_ZFEATURE_H

#include <sys/nvpair.h>
#include <sys/txg.h>
#include "zfeature_common.h"

#ifdef	__cplusplus
extern "C" {
#endif

#define	VALID_FEATURE_FID(fid)	((fid) >= 0 && (fid) < SPA_FEATURES)
#define	VALID_FEATURE_OR_NONE(fid)	((fid) == SPA_FEATURE_NONE ||	\
    VALID_FEATURE_FID(fid))

struct spa;
struct dmu_tx;
struct objset;

extern void spa_feature_create_zap_objects(struct spa *, struct dmu_tx *);
extern void spa_feature_enable(struct spa *, spa_feature_t,
    struct dmu_tx *);
extern void spa_feature_incr(struct spa *, spa_feature_t, struct dmu_tx *);
extern void spa_feature_decr(struct spa *, spa_feature_t, struct dmu_tx *);
extern boolean_t spa_feature_is_enabled(struct spa *, spa_feature_t);
extern boolean_t spa_feature_is_active(struct spa *, spa_feature_t);
extern boolean_t spa_feature_enabled_txg(spa_t *spa, spa_feature_t fid,
    uint64_t *txg);
extern uint64_t spa_feature_refcount(spa_t *, spa_feature_t, uint64_t);
extern boolean_t spa_features_check(spa_t *, boolean_t, nvlist_t *, nvlist_t *);

/*
 * These functions are only exported for zhack and zdb; normal callers should
 * use the above interfaces.
 */
extern int feature_get_refcount(struct spa *, zfeature_info_t *, uint64_t *);
extern int feature_get_refcount_from_disk(spa_t *spa, zfeature_info_t *feature,
    uint64_t *res);
extern void feature_enable_sync(struct spa *, zfeature_info_t *,
    struct dmu_tx *);
extern void feature_sync(struct spa *, zfeature_info_t *, uint64_t,
    struct dmu_tx *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ZFEATURE_H */
