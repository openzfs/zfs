// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_OPENSOLARIS_SYS_PRIV_H_
#define	_OPENSOLARIS_SYS_PRIV_H_
#ifdef _KERNEL
#include <sys/kauth.h>
/*
 * Every PRIV_* name collapses to KAUTH_GENERIC_ISSUSER.
 * There is no independent PRIV_VFS_CHOWN / RETAINSUGID /
 * MOUNT_NONUSER.  Delegation (ZFS_DELEG_PERM_*) is the
 * unprivileged path; secpolicy_fs_mount_clearopts then
 * forces MNT_NOSUID|MNT_NODEV and latches them so a later
 * `zfs set setuid=on` cannot undo the policy.
 */
#define	priv_check(td, priv)		(kauth_authorize_generic( \
	kauth_cred_get(), KAUTH_GENERIC_ISSUSER, NULL))
#define	priv_check_cred(cr, priv)	(kauth_authorize_generic( \
	(kauth_cred_t)(cr), KAUTH_GENERIC_ISSUSER, NULL))
#define	PRIV_VFS_ADMIN			0
#define	PRIV_VFS_LOOKUP			0
#define	PRIV_VFS_READ			0
#define	PRIV_VFS_WRITE			0
#define	PRIV_VFS_EXEC			0
#define	PRIV_VFS_BLOCKRESERVE		0
#define	PRIV_VFS_CHOWN			0
#define	PRIV_VFS_SETGID			0
#define	PRIV_VFS_SYSFLAGS		0
#define	PRIV_VFS_RETAINSUGID		0
#define	PRIV_VFS_LINK			0
#define	PRIV_VFS_MOUNT			0
#define	PRIV_VFS_UNMOUNT		0
#define	PRIV_NFS_DAEMON			0
#define	PRIV_ZFS_POOL_CONFIG		0
#define	PRIV_ZFS_INJECT			0
#define	PRIV_ZFS_JAIL			0
#define	PRIV_DRIVER			0
#endif
#endif
