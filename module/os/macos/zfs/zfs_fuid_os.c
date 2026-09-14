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
