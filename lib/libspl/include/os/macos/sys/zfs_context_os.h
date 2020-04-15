/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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

#ifndef ZFS_CONTEXT_OS_H_
#define	ZFS_CONTEXT_OS_H_

#include <sys/ioctl.h>

#define	ZFS_EXPORTS_PATH	"/etc/exports"
#define	MNTTYPE_ZFS_SUBTYPE ('Z'<<24|'F'<<16|'S'<<8)

/*
 * XNU reserves fileID 1-15, so we remap them high.
 * 2 is root-of-the-mount.
 * If ID is same as root, return 2. Otherwise, if it is 0-15, return
 * adjusted, otherwise, return as-is.
 * See hfs_format.h: kHFSRootFolderID, kHFSExtentsFileID, ...
 */
#define	INO_ROOT 		2ULL
#define	INO_RESERVED		16ULL	/* [0-15] reserved. */
#define	INO_ISRESERVED(ID)	((ID) < (INO_RESERVED))
/*				0xFFFFFFFFFFFFFFF0 */
#define	INO_MAP			((uint64_t)-INO_RESERVED) /* -16, -15, .., -1 */

#define	INO_ZFSTOXNU(ID, ROOT)	\
	((ID) == (ROOT)?INO_ROOT:(INO_ISRESERVED(ID)?INO_MAP+(ID):(ID)))

/*
 * This macro relies on *unsigned*.
 * If asking for 2, return rootID. If in special range, adjust to
 * normal, otherwise, return as-is.
 */
#define	INO_XNUTOZFS(ID, ROOT)	\
	((ID) == INO_ROOT)?(ROOT): \
	(INO_ISRESERVED((ID)-INO_MAP))?((ID)-INO_MAP):(ID)

struct spa_iokit;
typedef struct spa_iokit spa_iokit_t;

#define	zc_fd_offset zc_zoneid

struct zfs_handle;

extern void zfs_rollback_os(struct zfs_handle *zhp);
extern void libzfs_macos_wrapfd(int *srcfd, boolean_t send);
extern void libzfs_macos_wrapclose(void);
extern int  libzfs_macos_pipefd(int *read_fd, int *write_fd);

#endif
