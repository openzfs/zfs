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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Gunnar Beutner
 * Copyright (c) 2019, 2020 by Delphix. All rights reserved.
 */
#ifndef _LIBSPL_LIBSHARE_IMPL_H
#define	_LIBSPL_LIBSHARE_IMPL_H

typedef const struct sa_share_impl {
	const char *sa_zfsname;
	const char *sa_mountpoint;
	const char *sa_shareopts;
} *sa_share_impl_t;

typedef struct {
	int (*const enable_share)(sa_share_impl_t share);
	int (*const disable_share)(sa_share_impl_t share);
	boolean_t (*const is_shared)(sa_share_impl_t share);
	int (*const validate_shareopts)(const char *shareopts);
	int (*const commit_shares)(void);
} sa_fstype_t;

extern const sa_fstype_t libshare_nfs_type, libshare_smb_type;

#endif /* _LIBSPL_LIBSHARE_IMPL_H */
