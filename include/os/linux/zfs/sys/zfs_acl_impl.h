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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _ZFS_ACL_IMPL_H
#define	_ZFS_ACL_IMPL_H

#include <sys/zfs_acl.h>

/*
 * These functions are only called by Linux platform-specific code. They have
 * similar names and prototypes to other platforms because of their shared
 * history, but are otherwise unrelated.
 */

int zfs_acl_ids_create(struct znode *, int, vattr_t *,
    cred_t *, vsecattr_t *, zfs_acl_ids_t *, zidmap_t *);
void zfs_acl_ids_free(zfs_acl_ids_t *);
boolean_t zfs_acl_ids_overquota(struct zfsvfs *, zfs_acl_ids_t *, uint64_t);
int zfs_fastaccesschk_execute(struct znode *, cred_t *);
int zfs_zaccess_unix(void *, int, cred_t *);
int zfs_acl_chmod_setattr(struct znode *, zfs_acl_t **, uint64_t);
int zfs_zaccess_delete(struct znode *, struct znode *, cred_t *, zidmap_t *);
int zfs_zaccess_rename(struct znode *, struct znode *,
    struct znode *, struct znode *, cred_t *cr, zidmap_t *mnt_ns);
void zfs_acl_free(zfs_acl_t *);
int zfs_vsec_2_aclp(struct zfsvfs *, umode_t, vsecattr_t *, cred_t *,
    struct zfs_fuid_info **, zfs_acl_t **);
int zfs_aclset_common(struct znode *, zfs_acl_t *, cred_t *, dmu_tx_t *);
int zfs_znode_acl_version(struct znode *);
zfs_acl_t *zfs_acl_alloc(int);
zfs_acl_node_t *zfs_acl_node_alloc(size_t);
uint64_t zfs_mode_compute(uint64_t, zfs_acl_t *,
    uint64_t *, uint64_t, uint64_t);
int zfs_acl_chown_setattr(struct znode *);

#endif
