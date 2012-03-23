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

#ifndef	_SYS_POLICY_H
#define	_SYS_POLICY_H

#include <sys/types.h>
#include <sys/xvattr.h>
#include <sys/zfs_znode.h>
#include <sys/cred.h>
#include <sys/vnode.h>
#include <linux/kmod.h>
#include <linux/security.h>

/*
 * The passed credentials cannot be used because the Linux kernel does
 * not expose an interface to check arbitrary credentials.  The available
 * capable() interface assumes the current user.  Luckily, the credentials
 * passed by zfs are almost always obtained be CRED() which is defined to
 * be the current user credentials.  So the credentials should be the same.
 *
 * The major exception to this is that zfs will occasionally pass kcred
 * for certain operations such as zil replay.  Now as long as zfs uses
 * kcred only in the right contexts this should also be OK.  The worst
 * case here is that we mistakenly deny an operation.
 */
static inline int
spl_capable(cred_t *c, int capability)
{
	return capable(capability) ? 0 : EACCES;
}

static inline int
secpolicy_fs_unmount(cred_t *c, struct vfsmount *mnt)
{
	return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int
secpolicy_sys_config(cred_t *c, int checkonly)
{
	return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int
secpolicy_nfs(cred_t *c)
{
	return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int
secpolicy_zfs(cred_t *c)
{
	return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int
secpolicy_zinject(cred_t *c)
{
	return spl_capable(c, CAP_SYS_ADMIN);
}

static inline int
secpolicy_vnode_setids_setgids(cred_t *c, gid_t gid)
{
	if (groupmember(gid, c))
		return 0;

	return spl_capable(c, CAP_FSETID);
}

static inline int
secpolicy_vnode_setid_retain(cred_t *c, int is_setuid_root)
{
	return spl_capable(c, CAP_FSETID);
}

static inline int
secpolicy_setid_setsticky_clear(struct inode *ip, vattr_t *attr,
				vattr_t *oldattr, cred_t *c)
{
	boolean_t requires_extrapriv = B_FALSE;

	if ((attr->va_mode & S_ISGID) && !groupmember(oldattr->va_gid, c))
		requires_extrapriv = B_TRUE;

	if ((attr->va_mode & S_ISUID) && !(oldattr->va_uid == crgetuid(c)))
		requires_extrapriv = B_TRUE;

	if (requires_extrapriv == B_FALSE)
		return 0;

	return spl_capable(c, CAP_FSETID);
}

static inline int
secpolicy_setid_clear(vattr_t *v, cred_t *c)
{
	int err;

	err = spl_capable(c, CAP_FSETID);
	if (err)
		return err;

	if (v->va_mode & (S_ISUID | S_ISGID)) {
		v->va_mask |= AT_MODE;
		v->va_mode &= ~(S_ISUID | S_ISGID);
	}

	return err;
}

static inline int
secpolicy_vnode_any_access(cred_t *c , struct inode *ip, uid_t owner)
{
	if (crgetuid(c) == owner)
		return 0;

	if (spl_capable(c, CAP_DAC_OVERRIDE) == 0)
		return 0;

	if (spl_capable(c, CAP_DAC_READ_SEARCH) == 0)
		return 0;

	if (spl_capable(c, CAP_FOWNER) == 0)
		return 0;

	return EACCES;
}

static inline int
secpolicy_vnode_access2(cred_t *c, struct inode *ip, uid_t owner,
			mode_t curmode, mode_t wantedmode)
{
	mode_t missing = ~curmode & wantedmode;

	if (missing == 0)
		return 0;

	if ((missing & ~(S_IRUSR | S_IXUSR)) == 0) {
		if (spl_capable(c, CAP_DAC_READ_SEARCH) == 0)
			return 0;
	}

	return spl_capable(c, CAP_DAC_OVERRIDE);
}

static inline int
secpolicy_vnode_chown(cred_t *c, uid_t owner)
{
	if (crgetuid(c) == owner)
		return 0;

	return spl_capable(c, CAP_FOWNER);
}

static inline int
secpolicy_vnode_setdac(cred_t *c, uid_t owner)
{
	if (crgetuid(c) == owner)
		return 0;

	return spl_capable(c, CAP_DAC_OVERRIDE);
}

static inline int
secpolicy_vnode_remove(cred_t *c)
{
	return spl_capable(c, CAP_FOWNER);
}

static inline int
secpolicy_vnode_setattr(cred_t *c, struct inode *ip, vattr_t *vap,
			vattr_t *oldvap, int flags,
			int (*zaccess)(void *, int, cred_t *), znode_t *znode)
{
	int mask = vap->va_mask;
	int err = 0;

	if (mask & AT_MODE) {
		err = secpolicy_vnode_setdac(c, oldvap->va_uid);
		if (err)
			return err;

		err = secpolicy_setid_setsticky_clear(ip, vap, oldvap, c);
		if (err)
			return err;
	} else {
		vap->va_mode = oldvap->va_mode;
	}

	if (mask & AT_SIZE) {
		if (S_ISDIR(ip->i_mode))
			return (EISDIR);

		err = zaccess(znode, S_IWUSR, c);
		if (err)
			return err;
	}

	if (mask & (AT_UID | AT_GID)) {
		if (((mask & AT_UID) && vap->va_uid != oldvap->va_uid) ||
		    ((mask & AT_GID) && vap->va_gid != oldvap->va_gid )) {
			secpolicy_setid_clear(vap, c);
			err = secpolicy_vnode_setdac(c, oldvap->va_uid);
			if (err)
				return err;
		}
	}

	if (mask & (AT_ATIME | AT_MTIME)) {
		/*
		 * If the caller is the owner or has appropriate capabilities,
		 * allow them to set every time.  The Linux-specific constants
		 * mean the user has requested to write specific times instead
		 * of the current time.
		 */
		if ((secpolicy_vnode_setdac(c, oldvap->va_uid) != 0) &&
		    (mask & (ATTR_ATIME_SET | ATTR_MTIME_SET))) {
			err = zaccess(znode, S_IWUSR, c);
			if (err)
				return err;
		}
	}

	return err;
}

/* Unused */
static inline int
secpolicy_vnode_stky_modify(cred_t *c)
{
	return EACCES;
}

static inline int
secpolicy_basic_link(cred_t *c)
{
	return spl_capable(c, CAP_FOWNER);
}

/* Only used with ksid */
static inline int
secpolicy_vnode_create_gid(cred_t *c)
{
	return EACCES;
}

static inline int
secpolicy_xvattr(xvattr_t *xv, uid_t owner, cred_t *c, mode_t mode)
{
	return secpolicy_vnode_chown(c, owner);
}

#endif /* SYS_POLICY_H */
