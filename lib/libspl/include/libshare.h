/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
#ifndef _LIBSPL_LIBSHARE_H
#define	_LIBSPL_LIBSHARE_H

typedef void *sa_handle_t;	/* opaque handle to access core functions */
typedef void *sa_group_t;
typedef void *sa_share_t;

/* API Initialization */
#define	SA_INIT_SHARE_API	0x0001	/* init share specific interface */
#define	SA_INIT_CONTROL_API	0x0002	/* init control specific interface */

/*
 * defined error values
 */

#define	SA_OK			0
#define	SA_NO_SUCH_PATH		1	/* provided path doesn't exist */
#define	SA_NO_MEMORY		2	/* no memory for data structures */
#define	SA_DUPLICATE_NAME	3	/* object name is already in use */
#define	SA_BAD_PATH		4	/* not a full path */
#define	SA_NO_SUCH_GROUP	5	/* group is not defined */
#define	SA_CONFIG_ERR		6	/* system configuration error */
#define	SA_SYSTEM_ERR		7	/* system error, use errno */
#define	SA_SYNTAX_ERR		8	/* syntax error on command line */
#define	SA_NO_PERMISSION	9	/* no permission for operation */
#define	SA_BUSY			10	/* resource is busy */
#define	SA_NO_SUCH_PROP		11	/* property doesn't exist */
#define	SA_INVALID_NAME		12	/* name of object is invalid */
#define	SA_INVALID_PROTOCOL	13	/* specified protocol not valid */
#define	SA_NOT_ALLOWED		14	/* operation not allowed */
#define	SA_BAD_VALUE		15	/* bad value for property */
#define	SA_INVALID_SECURITY	16	/* invalid security type */
#define	SA_NO_SUCH_SECURITY	17	/* security set not found */
#define	SA_VALUE_CONFLICT	18	/* property value conflict */
#define	SA_NOT_IMPLEMENTED	19	/* plugin interface not implemented */
#define	SA_INVALID_PATH		20	/* path is sub-dir of existing share */
#define	SA_NOT_SUPPORTED	21	/* operation not supported for proto */
#define	SA_PROP_SHARE_ONLY	22	/* property valid on share only */
#define	SA_NOT_SHARED		23	/* path is not shared */
#define	SA_NO_SUCH_RESOURCE	24	/* resource not found */
#define	SA_RESOURCE_REQUIRED	25	/* resource name is required  */
#define	SA_MULTIPLE_ERROR	26	/* multiple protocols reported error */
#define	SA_PATH_IS_SUBDIR	27	/* check_path found path is subdir */
#define	SA_PATH_IS_PARENTDIR	28	/* check_path found path is parent */
#define	SA_NO_SECTION		29	/* protocol requires section info */
#define	SA_NO_SUCH_SECTION	30	/* no section found */
#define	SA_NO_PROPERTIES	31	/* no properties found */
#define	SA_PASSWORD_ENC		32	/* passwords must be encrypted */
#define	SA_SHARE_EXISTS		33	/* path or file is already shared */

/* initialization */
extern sa_handle_t sa_init(int);
extern void sa_fini(sa_handle_t);
extern char *sa_errorstr(int);

/* share control */
extern sa_share_t sa_find_share(sa_handle_t, char *);
extern int sa_enable_share(sa_group_t, char *);
extern int sa_disable_share(sa_share_t, char *);

/* protocol specific interfaces */
extern int sa_parse_legacy_options(sa_group_t, char *, char *);

/* ZFS functions */
extern boolean_t sa_needs_refresh(sa_handle_t handle);
libzfs_handle_t *sa_get_zfs_handle(sa_handle_t handle);
extern int sa_zfs_process_share(sa_handle_t handle, sa_group_t group,
    sa_share_t share, char *mountpoint, char *proto, zprop_source_t source,
    char *shareopts, char *sourcestr, char *dataset);

#endif /* _LIBSPL_LIBSHARE_H */
