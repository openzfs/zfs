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
 * Copyright 2010 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 */

#ifndef	_ZFS_DELEG_H
#define	_ZFS_DELEG_H extern __attribute__((visibility("default")))

#include <sys/fs/zfs.h>

#ifdef	__cplusplus
extern "C" {
#endif

#define	ZFS_DELEG_SET_NAME_CHR		'@'		/* set name lead char */
#define	ZFS_DELEG_FIELD_SEP_CHR		'$'		/* field separator */

/*
 * Max name length for a delegation attribute
 */
#define	ZFS_MAX_DELEG_NAME	128

#define	ZFS_DELEG_LOCAL		'l'
#define	ZFS_DELEG_DESCENDENT	'd'
#define	ZFS_DELEG_NA		'-'

typedef enum {
	ZFS_DELEG_NOTE_CREATE,
	ZFS_DELEG_NOTE_DESTROY,
	ZFS_DELEG_NOTE_SNAPSHOT,
	ZFS_DELEG_NOTE_ROLLBACK,
	ZFS_DELEG_NOTE_CLONE,
	ZFS_DELEG_NOTE_PROMOTE,
	ZFS_DELEG_NOTE_RENAME,
	ZFS_DELEG_NOTE_SEND,
	ZFS_DELEG_NOTE_SEND_RAW,
	ZFS_DELEG_NOTE_SEND_ENCRYPTED,
	ZFS_DELEG_NOTE_RECEIVE,
	ZFS_DELEG_NOTE_ALLOW,
	ZFS_DELEG_NOTE_USERPROP,
	ZFS_DELEG_NOTE_MOUNT,
	ZFS_DELEG_NOTE_SHARE,
	ZFS_DELEG_NOTE_USERQUOTA,
	ZFS_DELEG_NOTE_GROUPQUOTA,
	ZFS_DELEG_NOTE_USERUSED,
	ZFS_DELEG_NOTE_GROUPUSED,
	ZFS_DELEG_NOTE_USEROBJQUOTA,
	ZFS_DELEG_NOTE_GROUPOBJQUOTA,
	ZFS_DELEG_NOTE_USEROBJUSED,
	ZFS_DELEG_NOTE_GROUPOBJUSED,
	ZFS_DELEG_NOTE_HOLD,
	ZFS_DELEG_NOTE_RELEASE,
	ZFS_DELEG_NOTE_DIFF,
	ZFS_DELEG_NOTE_BOOKMARK,
	ZFS_DELEG_NOTE_LOAD_KEY,
	ZFS_DELEG_NOTE_CHANGE_KEY,
	ZFS_DELEG_NOTE_PROJECTUSED,
	ZFS_DELEG_NOTE_PROJECTQUOTA,
	ZFS_DELEG_NOTE_PROJECTOBJUSED,
	ZFS_DELEG_NOTE_PROJECTOBJQUOTA,
	ZFS_DELEG_NOTE_NONE
} zfs_deleg_note_t;

typedef struct zfs_deleg_perm_tab {
	const char *z_perm;
	zfs_deleg_note_t z_note;
} zfs_deleg_perm_tab_t;

_ZFS_DELEG_H const zfs_deleg_perm_tab_t zfs_deleg_perm_tab[];

_ZFS_DELEG_H int zfs_deleg_verify_nvlist(nvlist_t *nvlist);
_ZFS_DELEG_H void zfs_deleg_whokey(char *attr, zfs_deleg_who_type_t type,
    char checkflag, void *data);
_ZFS_DELEG_H const char *zfs_deleg_canonicalize_perm(const char *perm);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_DELEG_H */
