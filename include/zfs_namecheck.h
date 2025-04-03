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
