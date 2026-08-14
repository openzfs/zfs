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
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _ZFS_ACL_IMPL_H
#define	_ZFS_ACL_IMPL_H

#include <sys/zfs_acl.h>

/*
 * These functions are only called by FreeBSD platform-specific code. They have
 * similar names and prototypes to other platforms because of their shared
 * history, but are otherwise unrelated.
 */

int zfs_acl_ids_create(struct znode *, int, vattr_t *,
    cred_t *, vsecattr_t *, zfs_acl_ids_t *);
void zfs_acl_ids_free(zfs_acl_ids_t *);
boolean_t zfs_acl_ids_overquota(struct zfsvfs *, zfs_acl_ids_t *, uint64_t);
int zfs_fastaccesschk_execute(struct znode *, cred_t *);
int zfs_zaccess_unix(void *, int, cred_t *);
int zfs_acl_chmod_setattr(struct znode *, zfs_acl_t **, uint64_t);
int zfs_zaccess_delete(struct znode *, struct znode *, cred_t *);
int zfs_zaccess_rename(struct znode *, struct znode *,
    struct znode *, struct znode *, cred_t *cr);
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
