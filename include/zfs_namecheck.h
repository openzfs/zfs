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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */

#ifndef	_ZFS_NAMECHECK_H
#define	_ZFS_NAMECHECK_H extern __attribute__((visibility("default")))

#ifdef	__cplusplus
extern "C" {
#endif

typedef enum {
	NAME_ERR_LEADING_SLASH,		/* name begins with leading slash */
	NAME_ERR_EMPTY_COMPONENT,	/* name contains an empty component */
	NAME_ERR_TRAILING_SLASH,	/* name ends with a slash */
	NAME_ERR_INVALCHAR,		/* invalid character found */
	NAME_ERR_MULTIPLE_DELIMITERS,	/* multiple '@'/'#' delimiters found */
	NAME_ERR_NOLETTER,		/* pool doesn't begin with a letter */
	NAME_ERR_RESERVED,		/* entire name is reserved */
	NAME_ERR_DISKLIKE,		/* reserved disk name (c[0-9].*) */
	NAME_ERR_TOOLONG,		/* name is too long */
	NAME_ERR_SELF_REF,		/* reserved self path name ('.') */
	NAME_ERR_PARENT_REF,		/* reserved parent path name ('..') */
	NAME_ERR_NO_AT,			/* permission set is missing '@' */
	NAME_ERR_NO_POUND, 		/* permission set is missing '#' */
} namecheck_err_t;

#define	ZFS_PERMSET_MAXLEN	64

_ZFS_NAMECHECK_H int zfs_max_dataset_nesting;

_ZFS_NAMECHECK_H int get_dataset_depth(const char *);
_ZFS_NAMECHECK_H int pool_namecheck(const char *, namecheck_err_t *, char *);
_ZFS_NAMECHECK_H int entity_namecheck(const char *, namecheck_err_t *, char *);
_ZFS_NAMECHECK_H int dataset_namecheck(const char *, namecheck_err_t *, char *);
_ZFS_NAMECHECK_H int snapshot_namecheck(const char *, namecheck_err_t *,
    char *);
_ZFS_NAMECHECK_H int bookmark_namecheck(const char *, namecheck_err_t *,
    char *);
_ZFS_NAMECHECK_H int dataset_nestcheck(const char *);
_ZFS_NAMECHECK_H int mountpoint_namecheck(const char *, namecheck_err_t *);
_ZFS_NAMECHECK_H int zfs_component_namecheck(const char *, namecheck_err_t *,
    char *);
_ZFS_NAMECHECK_H int permset_namecheck(const char *, namecheck_err_t *,
    char *);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZFS_NAMECHECK_H */
