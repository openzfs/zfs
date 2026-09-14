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

#ifndef ZFS_CONTEXT_MACOS_H_
#define	ZFS_CONTEXT_MACOS_H_

#include <sys/stat.h>
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

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#if !defined(MAC_OS_VERSION_12_0) || \
	(MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_VERSION_12_0)
#define	kIOMainPortDefault kIOMasterPortDefault
#define	IOMainPort IOMasterPort
#endif /* MAC_OS */

#endif
