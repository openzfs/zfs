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
*
* Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
*
*/

#include <spl-debug.h>
#include <sys/types.h>
#include <sys/mount.h>

int     vfs_busy(mount_t *mp, int flags)
{
	return 0;
}

void    vfs_unbusy(mount_t *mp)
{
}

int     vfs_isrdonly(mount_t *mp)
{
	return (mp->mountflags & MNT_RDONLY);
}

void *vfs_fsprivate(mount_t *mp)
{
	return mp->fsprivate;
}

void vfs_setfsprivate(mount_t *mp, void *mntdata)
{
	mp->fsprivate = mntdata;
}

void    vfs_clearflags(mount_t *mp, uint64_t flags)
{
	mp->mountflags &= ~flags;
}

void    vfs_setflags(mount_t *mp, uint64_t flags)
{
	mp->mountflags |= flags;
}

uint64_t vfs_flags(mount_t *mp)
{
	return mp->mountflags;
}

struct vfsstatfs *      vfs_statfs(mount_t *mp)
{
	return NULL;
}

void    vfs_setlocklocal(mount_t *mp)
{
}

int     vfs_typenum(mount_t *mp)
{
	return 0;
}

void    vfs_getnewfsid(struct mount *mp)
{
}

int     vfs_isunmount(mount_t *mp)
{
	return 0;
}

int
vfs_iswriteupgrade(mount_t *mp) /* ronly &&  MNTK_WANTRDWR */
{
//	return (mp->mnt_flag & MNT_RDONLY) && (mp->mnt_kern_flag & MNTK_WANTRDWR);
	return (FALSE);
}

void
vfs_setextendedsecurity(mount_t *mp)
{
}

