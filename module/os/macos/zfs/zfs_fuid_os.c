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
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/avl.h>
#include <sys/zap.h>
#include <sys/zfs_refcount.h>
#include <sys/nvpair.h>
#ifdef _KERNEL
#include <sys/sid.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#endif
#include <sys/zfs_fuid.h>

uint64_t
zfs_fuid_create_cred(zfsvfs_t *zfsvfs, zfs_fuid_type_t type,
    cred_t *cr, zfs_fuid_info_t **fuidp)
{
	uid_t		id;

	VERIFY(type == ZFS_OWNER || type == ZFS_GROUP);

	id = (type == ZFS_OWNER) ? crgetuid(cr) : crgetgid(cr);

	if (IS_EPHEMERAL(id))
		return ((type == ZFS_OWNER) ? UID_NOBODY : GID_NOBODY);

	return ((uint64_t)id);
}
