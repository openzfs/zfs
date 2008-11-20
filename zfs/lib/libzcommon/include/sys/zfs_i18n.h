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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_ZFS_I18N_H
#define	_SYS_ZFS_I18N_H



#include <sys/sunddi.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * z_case behaviors
 *	The first two describe the extent of case insensitivity.
 *	The third describes matching behavior when mixed sensitivity
 *	is allowed.
 */
#define	ZFS_CI_ONLY	0x01		/* all lookups case-insensitive */
#define	ZFS_CI_MIXD	0x02		/* some lookups case-insensitive */

/*
 * ZFS_UTF8_ONLY
 *	If set, the file system should reject non-utf8 characters in names.
 */
#define	ZFS_UTF8_ONLY	0x04

enum zfs_case {
	ZFS_CASE_SENSITIVE,
	ZFS_CASE_INSENSITIVE,
	ZFS_CASE_MIXED
};

enum zfs_normal {
	ZFS_NORMALIZE_NONE,
	ZFS_NORMALIZE_D,
	ZFS_NORMALIZE_KC,
	ZFS_NORMALIZE_C,
	ZFS_NORMALIZE_KD
};

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_ZFS_I18N_H */
