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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef	_SYS_DSL_DESTROY_H
#define	_SYS_DSL_DESTROY_H

#ifdef	__cplusplus
extern "C" {
#endif

struct nvlist;
struct dsl_dataset;
struct dsl_pool;
struct dmu_tx;

int dsl_destroy_snapshots_nvl(struct nvlist *, boolean_t,
    struct nvlist *);
int dsl_destroy_snapshot(const char *, boolean_t);
int dsl_destroy_head(const char *);
int dsl_destroy_head_check_impl(struct dsl_dataset *, int);
void dsl_destroy_head_sync_impl(struct dsl_dataset *, struct dmu_tx *);
int dsl_destroy_inconsistent(const char *, void *);
int dsl_destroy_snapshot_check_impl(struct dsl_dataset *, boolean_t);
void dsl_destroy_snapshot_sync_impl(struct dsl_dataset *,
    boolean_t, struct dmu_tx *);
void dsl_dir_remove_clones_key(dsl_dir_t *, uint64_t, dmu_tx_t *);

typedef struct dsl_destroy_snapshot_arg {
	const char *ddsa_name;
	boolean_t ddsa_defer;
} dsl_destroy_snapshot_arg_t;

int dsl_destroy_snapshot_check(void *, dmu_tx_t *);
void dsl_destroy_snapshot_sync(void *, dmu_tx_t *);

typedef struct dsl_destroy_head_arg {
	const char *ddha_name;
} dsl_destroy_head_arg_t;

int dsl_destroy_head_check(void *, dmu_tx_t *);
void dsl_destroy_head_sync(void *, dmu_tx_t *);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_DSL_DESTROY_H */
