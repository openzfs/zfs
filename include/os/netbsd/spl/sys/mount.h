// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_MOUNT_H_
#define	_OPENSOLARIS_SYS_MOUNT_H_

#include <sys/param.h>
#include_next <sys/mount.h>
#ifdef BUILDING_ZFS
#include <sys/vfs.h>
#endif
#define	MS_FORCE	MNT_FORCE
#define	MS_REMOUNT	MNT_UPDATE
#define	MS_RDONLY	MNT_RDONLY
#define	MS_OVERLAY	0
#define	MS_OPTIONSTR	0
#define	MS_NOMNTTAB	0

typedef	struct fid		fid_t;

#ifndef MOUNT_ZFS
#define	MOUNT_ZFS	"zfs"
#endif

/*
 * NetBSD: _VFS_NAMELEN is 32 (fstype). Mount paths are _VFS_MNAMELEN (1024).
 * Using MNAMELEN/_VFS_NAMELEN here truncated mountpoints to 31 bytes and
 * broke zfs unmount/destroy (ENOENT on the clipped path).
 */
#ifndef	ZFS_MNAMELEN
#ifdef	_VFS_MNAMELEN
#define	ZFS_MNAMELEN	_VFS_MNAMELEN
#else
#define	ZFS_MNAMELEN	1024
#endif
#endif

struct zfs_args {
	char	fspec[MAXPATHLEN];
	int	mflag;
	char	optptr[MAXPATHLEN];
	int	optlen;
	char	mntonname[ZFS_MNAMELEN];
};
typedef struct zfs_args zfs_args_t;

#define	vfs_fstype	mnt_op->vfs_name

#endif	/* !_OPENSOLARIS_SYS_MOUNT_H_ */
