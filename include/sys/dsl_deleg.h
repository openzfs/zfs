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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 */

#ifndef	_SYS_DSL_DELEG_H
#define	_SYS_DSL_DELEG_H

#include <sys/dmu.h>
#include <sys/dsl_pool.h>
#include <sys/zfs_context.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_DELEG_PERM_NONE		""
#define	ZFS_DELEG_PERM_CREATE		"create"
#define	ZFS_DELEG_PERM_DESTROY		"destroy"
#define	ZFS_DELEG_PERM_SNAPSHOT		"snapshot"
#define	ZFS_DELEG_PERM_ROLLBACK		"rollback"
#define	ZFS_DELEG_PERM_CLONE		"clone"
#define	ZFS_DELEG_PERM_PROMOTE		"promote"
#define	ZFS_DELEG_PERM_RENAME		"rename"
#define	ZFS_DELEG_PERM_MOUNT		"mount"
#define	ZFS_DELEG_PERM_SHARE		"share"
#define	ZFS_DELEG_PERM_SEND		"send"
#define	ZFS_DELEG_PERM_SEND_RAW		"send:raw"
#define	ZFS_DELEG_PERM_SEND_ENCRYPTED	"send:encrypted"
#define	ZFS_DELEG_PERM_RECEIVE		"receive"
#define	ZFS_DELEG_PERM_RECEIVE_APPEND	"receive:append"
#define	ZFS_DELEG_PERM_ALLOW		"allow"
#define	ZFS_DELEG_PERM_USERPROP		"userprop"
#define	ZFS_DELEG_PERM_VSCAN		"vscan"
#define	ZFS_DELEG_PERM_USERQUOTA	"userquota"
#define	ZFS_DELEG_PERM_GROUPQUOTA	"groupquota"
#define	ZFS_DELEG_PERM_USEROBJQUOTA	"userobjquota"
#define	ZFS_DELEG_PERM_GROUPOBJQUOTA	"groupobjquota"
#define	ZFS_DELEG_PERM_USERUSED		"userused"
#define	ZFS_DELEG_PERM_GROUPUSED	"groupused"
#define	ZFS_DELEG_PERM_USEROBJUSED	"userobjused"
#define	ZFS_DELEG_PERM_GROUPOBJUSED	"groupobjused"
#define	ZFS_DELEG_PERM_HOLD		"hold"
#define	ZFS_DELEG_PERM_RELEASE		"release"
#define	ZFS_DELEG_PERM_DIFF		"diff"
#define	ZFS_DELEG_PERM_BOOKMARK		"bookmark"
#define	ZFS_DELEG_PERM_LOAD_KEY		"load-key"
#define	ZFS_DELEG_PERM_CHANGE_KEY	"change-key"
#define	ZFS_DELEG_PERM_PROJECTUSED	"projectused"
#define	ZFS_DELEG_PERM_PROJECTQUOTA	"projectquota"
#define	ZFS_DELEG_PERM_PROJECTOBJUSED	"projectobjused"
#define	ZFS_DELEG_PERM_PROJECTOBJQUOTA	"projectobjquota"

/*
 * Note: the names of properties that are marked delegatable are also
 * valid delegated permissions
 */

int dsl_deleg_get(const char *ddname, nvlist_t **nvp);
int dsl_deleg_set(const char *ddname, nvlist_t *nvp, boolean_t unset);
int dsl_deleg_access(const char *ddname, const char *perm, cred_t *cr);
int dsl_deleg_access_impl(struct dsl_dataset *ds, const char *perm, cred_t *cr);
void dsl_deleg_set_create_perms(dsl_dir_t *dd, dmu_tx_t *tx, cred_t *cr);
int dsl_deleg_can_allow(char *ddname, nvlist_t *nvp, cred_t *cr);
int dsl_deleg_can_unallow(char *ddname, nvlist_t *nvp, cred_t *cr);
int dsl_deleg_destroy(objset_t *os, uint64_t zapobj, dmu_tx_t *tx);
boolean_t dsl_delegation_on(objset_t *os);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_DSL_DELEG_H */
