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
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 *
 * Extended attributes (xattr) on Solaris are implemented as files
 * which exist in a hidden xattr directory.  These extended attributes
 * can be accessed using the attropen() system call which opens
 * the extended attribute.  It can then be manipulated just like
 * a standard file descriptor.  This has a couple advantages such
 * as practically no size limit on the file, and the extended
 * attributes permissions may differ from those of the parent file.
 * This interface is really quite clever, but it's also completely
 * different than what is supported on Linux.  It also comes with a
 * steep performance penalty when accessing small xattrs because they
 * are not stored with the parent file.
 *
 * Under Linux extended attributes are manipulated by the system
 * calls getxattr(2), setxattr(2), and listxattr(2).  They consider
 * extended attributes to be name/value pairs where the name is a
 * NULL terminated string.  The name must also include one of the
 * following namespace prefixes:
 *
 *   user     - No restrictions and is available to user applications.
 *   trusted  - Restricted to kernel and root (CAP_SYS_ADMIN) use.
 *   system   - Used for access control lists (system.nfs4_acl, etc).
 *   security - Used by SELinux to store a files security context.
 *
 * The value under Linux to limited to 65536 bytes of binary data.
 * In practice, individual xattrs tend to be much smaller than this
 * and are typically less than 100 bytes.  A good example of this
 * are the security.selinux xattrs which are less than 100 bytes and
 * exist for every file when xattr labeling is enabled.
 *
 * The Linux xattr implementation has been written to take advantage of
 * this typical usage.  When the dataset property 'xattr=sa' is set,
 * then xattrs will be preferentially stored as System Attributes (SA).
 * This allows tiny xattrs (~100 bytes) to be stored with the dnode and
 * up to 64k of xattrs to be stored in the spill block.  If additional
 * xattr space is required, which is unlikely under Linux, they will
 * be stored using the traditional directory approach.
 *
 * This optimization results in roughly a 3x performance improvement
 * when accessing xattrs because it avoids the need to perform a seek
 * for every xattr value.  When multiple xattrs are stored per-file
 * the performance improvements are even greater because all of the
 * xattrs stored in the spill block will be cached.
 *
 * However, by default SA based xattrs are disabled in the Linux port
 * to maximize compatibility with other implementations.  If you do
 * enable SA based xattrs then they will not be visible on platforms
 * which do not support this feature.
 *
 * NOTE: One additional consequence of the xattr directory implementation
 * is that when an extended attribute is manipulated an inode is created.
 * This inode will exist in the Linux inode cache but there will be no
 * associated entry in the dentry cache which references it.  This is
 * safe but it may result in some confusion.  Enabling SA based xattrs
 * largely avoids the issue except in the overflow case.
 */

#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/xvattr.h>
#include <sys/zap.h>
#include <sys/vfs.h>
#include <sys/zpl.h>
#include <rpc/xdr.h>
#include "nfs41acl.h"

#define	NFS41ACL_XATTR		"system.nfs4_acl_xdr"

static const struct {
	int kmask;
	int zfsperm;
} mask2zfs[] = {
	{ MAY_READ, ACE_READ_DATA },
	{ MAY_WRITE, ACE_WRITE_DATA },
	{ MAY_EXEC, ACE_EXECUTE },
#ifdef SB_NFSV4ACL
	{ MAY_DELETE, ACE_DELETE },
	{ MAY_DELETE_CHILD, ACE_DELETE_CHILD },
	{ MAY_WRITE_ATTRS, ACE_WRITE_ATTRIBUTES },
	{ MAY_WRITE_NAMED_ATTRS, ACE_WRITE_NAMED_ATTRS },
	{ MAY_WRITE_ACL, ACE_WRITE_ACL },
	{ MAY_WRITE_OWNER, ACE_WRITE_OWNER },
#endif
};

#define	GENERIC_MASK(mask) ((mask & ~(MAY_READ | MAY_WRITE | MAY_EXEC)) == 0)

enum xattr_permission {
	XAPERM_DENY,
	XAPERM_ALLOW,
	XAPERM_COMPAT,
};

typedef struct xattr_filldir {
	size_t size;
	size_t offset;
	char *buf;
	struct dentry *dentry;
} xattr_filldir_t;

static enum xattr_permission zpl_xattr_permission(xattr_filldir_t *,
    const char *, int);

/*
 * Determine is a given xattr name should be visible and if so copy it
 * in to the provided buffer (xf->buf).
 */
static int
zpl_xattr_filldir(xattr_filldir_t *xf, const char *name, int name_len)
{
	enum xattr_permission perm;

	/* Check permissions using the per-namespace list xattr handler. */
	perm = zpl_xattr_permission(xf, name, name_len);
	if (perm == XAPERM_DENY)
		return (0);

	/* Prefix the name with "user." if it does not have a namespace. */
	if (perm == XAPERM_COMPAT) {
		if (xf->buf) {
			if (xf->offset + XATTR_USER_PREFIX_LEN + 1 > xf->size)
				return (-ERANGE);

			memcpy(xf->buf + xf->offset, XATTR_USER_PREFIX,
			    XATTR_USER_PREFIX_LEN);
			xf->buf[xf->offset + XATTR_USER_PREFIX_LEN] = '\0';
		}

		xf->offset += XATTR_USER_PREFIX_LEN;
	}

	/* When xf->buf is NULL only calculate the required size. */
	if (xf->buf) {
		if (xf->offset + name_len + 1 > xf->size)
			return (-ERANGE);

		memcpy(xf->buf + xf->offset, name, name_len);
		xf->buf[xf->offset + name_len] = '\0';
	}

	xf->offset += (name_len + 1);

	return (0);
}

/*
 * Read as many directory entry names as will fit in to the provided buffer,
 * or when no buffer is provided calculate the required buffer size.
 */
static int
zpl_xattr_readdir(struct inode *dxip, xattr_filldir_t *xf)
{
	zap_cursor_t zc;
	zap_attribute_t	zap;
	int error;

	zap_cursor_init(&zc, ITOZSB(dxip)->z_os, ITOZ(dxip)->z_id);

	while ((error = -zap_cursor_retrieve(&zc, &zap)) == 0) {

		if (zap.za_integer_length != 8 || zap.za_num_integers != 1) {
			error = -ENXIO;
			break;
		}

		error = zpl_xattr_filldir(xf, zap.za_name, strlen(zap.za_name));
		if (error)
			break;

		zap_cursor_advance(&zc);
	}

	zap_cursor_fini(&zc);

	if (error == -ENOENT)
		error = 0;

	return (error);
}

static ssize_t
zpl_xattr_list_dir(xattr_filldir_t *xf, cred_t *cr)
{
	struct inode *ip = xf->dentry->d_inode;
	struct inode *dxip = NULL;
	znode_t *dxzp;
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ITOZ(ip), NULL, &dxzp, LOOKUP_XATTR,
	    cr, NULL, NULL);
	if (error) {
		if (error == -ENOENT)
			error = 0;

		return (error);
	}

	dxip = ZTOI(dxzp);
	error = zpl_xattr_readdir(dxip, xf);
	iput(dxip);

	return (error);
}

static ssize_t
zpl_xattr_list_sa(xattr_filldir_t *xf)
{
	znode_t *zp = ITOZ(xf->dentry->d_inode);
	nvpair_t *nvp = NULL;
	int error = 0;

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);

	while ((nvp = nvlist_next_nvpair(zp->z_xattr_cached, nvp)) != NULL) {
		ASSERT3U(nvpair_type(nvp), ==, DATA_TYPE_BYTE_ARRAY);

		error = zpl_xattr_filldir(xf, nvpair_name(nvp),
		    strlen(nvpair_name(nvp)));
		if (error)
			return (error);
	}

	return (0);
}

ssize_t
zpl_xattr_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	znode_t *zp = ITOZ(dentry->d_inode);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	xattr_filldir_t xf = { buffer_size, 0, buffer, dentry };
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error = 0;

	crhold(cr);
	cookie = spl_fstrans_mark();
	ZPL_ENTER(zfsvfs);
	ZPL_VERIFY_ZP(zp);
	rw_enter(&zp->z_xattr_lock, RW_READER);

	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_list_sa(&xf);
		if (error)
			goto out;
	}

	error = zpl_xattr_list_dir(&xf, cr);
	if (error)
		goto out;

	error = xf.offset;
out:

	rw_exit(&zp->z_xattr_lock);
	ZPL_EXIT(zfsvfs);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static int
zpl_xattr_get_dir(struct inode *ip, const char *name, void *value,
    size_t size, cred_t *cr)
{
	fstrans_cookie_t cookie;
	struct inode *xip = NULL;
	znode_t *dxzp = NULL;
	znode_t *xzp = NULL;
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ITOZ(ip), NULL, &dxzp, LOOKUP_XATTR,
	    cr, NULL, NULL);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = -zfs_lookup(dxzp, (char *)name, &xzp, 0, cr, NULL, NULL);
	if (error)
		goto out;

	xip = ZTOI(xzp);
	if (!size) {
		error = i_size_read(xip);
		goto out;
	}

	if (size < i_size_read(xip)) {
		error = -ERANGE;
		goto out;
	}

	struct iovec iov;
	iov.iov_base = (void *)value;
	iov.iov_len = size;

	zfs_uio_t uio;
	zfs_uio_iovec_init(&uio, &iov, 1, 0, UIO_SYSSPACE, size, 0);

	cookie = spl_fstrans_mark();
	error = -zfs_read(ITOZ(xip), &uio, 0, cr);
	spl_fstrans_unmark(cookie);

	if (error == 0)
		error = size - zfs_uio_resid(&uio);
out:
	if (xzp)
		zrele(xzp);

	if (dxzp)
		zrele(dxzp);

	return (error);
}

static int
zpl_xattr_get_sa(struct inode *ip, const char *name, void *value, size_t size)
{
	znode_t *zp = ITOZ(ip);
	uchar_t *nv_value;
	uint_t nv_size;
	int error = 0;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);
	error = -nvlist_lookup_byte_array(zp->z_xattr_cached, name,
	    &nv_value, &nv_size);
	if (error)
		return (error);

	if (size == 0 || value == NULL)
		return (nv_size);

	if (size < nv_size)
		return (-ERANGE);

	memcpy(value, nv_value, nv_size);

	return (nv_size);
}

static int
__zpl_xattr_get(struct inode *ip, const char *name, void *value, size_t size,
    cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_get_sa(ip, name, value, size);
		if (error != -ENOENT)
			goto out;
	}

	error = zpl_xattr_get_dir(ip, name, value, size, cr);
out:
	if (error == -ENOENT)
		error = -ENODATA;

	return (error);
}

#define	XATTR_NOENT	0x0
#define	XATTR_IN_SA	0x1
#define	XATTR_IN_DIR	0x2
/* check where the xattr resides */
static int
__zpl_xattr_where(struct inode *ip, const char *name, int *where, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ASSERT(where);
	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	*where = XATTR_NOENT;
	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_get_sa(ip, name, NULL, 0);
		if (error >= 0)
			*where |= XATTR_IN_SA;
		else if (error != -ENOENT)
			return (error);
	}

	error = zpl_xattr_get_dir(ip, name, NULL, 0, cr);
	if (error >= 0)
		*where |= XATTR_IN_DIR;
	else if (error != -ENOENT)
		return (error);

	if (*where == (XATTR_IN_SA|XATTR_IN_DIR))
		cmn_err(CE_WARN, "ZFS: inode %p has xattr \"%s\""
		    " in both SA and dir", ip, name);
	if (*where == XATTR_NOENT)
		error = -ENODATA;
	else
		error = 0;
	return (error);
}

static int
zpl_xattr_get(struct inode *ip, const char *name, void *value, size_t size)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	ZPL_ENTER(zfsvfs);
	ZPL_VERIFY_ZP(zp);
	rw_enter(&zp->z_xattr_lock, RW_READER);
	error = __zpl_xattr_get(ip, name, value, size, cr);
	rw_exit(&zp->z_xattr_lock);
	ZPL_EXIT(zfsvfs);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static int
zpl_xattr_set_dir(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, cred_t *cr)
{
	znode_t *dxzp = NULL;
	znode_t *xzp = NULL;
	vattr_t *vap = NULL;
	int lookup_flags, error;
	const int xattr_mode = S_IFREG | 0644;
	loff_t pos = 0;

	/*
	 * Lookup the xattr directory.  When we're adding an entry pass
	 * CREATE_XATTR_DIR to ensure the xattr directory is created.
	 * When removing an entry this flag is not passed to avoid
	 * unnecessarily creating a new xattr directory.
	 */
	lookup_flags = LOOKUP_XATTR;
	if (value != NULL)
		lookup_flags |= CREATE_XATTR_DIR;

	error = -zfs_lookup(ITOZ(ip), NULL, &dxzp, lookup_flags,
	    cr, NULL, NULL);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = -zfs_lookup(dxzp, (char *)name, &xzp, 0, cr, NULL, NULL);
	if (error && (error != -ENOENT))
		goto out;

	error = 0;

	/* Remove a specific name xattr when value is set to NULL. */
	if (value == NULL) {
		if (xzp)
			error = -zfs_remove(dxzp, (char *)name, cr, 0);

		goto out;
	}

	/* Lookup failed create a new xattr. */
	if (xzp == NULL) {
		vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
		vap->va_mode = xattr_mode;
		vap->va_mask = ATTR_MODE;
		vap->va_uid = crgetfsuid(cr);
		vap->va_gid = crgetfsgid(cr);

		error = -zfs_create(dxzp, (char *)name, vap, 0, 0644, &xzp,
		    cr, 0, NULL);
		if (error)
			goto out;
	}

	ASSERT(xzp != NULL);

	error = -zfs_freesp(xzp, 0, 0, xattr_mode, TRUE);
	if (error)
		goto out;

	error = -zfs_write_simple(xzp, value, size, pos, NULL);
out:
	if (error == 0) {
		ip->i_ctime = current_time(ip);
		zfs_mark_inode_dirty(ip);
	}

	if (vap)
		kmem_free(vap, sizeof (vattr_t));

	if (xzp)
		zrele(xzp);

	if (dxzp)
		zrele(dxzp);

	if (error == -ENOENT)
		error = -ENODATA;

	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_xattr_set_sa(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	nvlist_t *nvl;
	size_t sa_size;
	int error = 0;

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = -zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);
	nvl = zp->z_xattr_cached;

	if (value == NULL) {
		error = -nvlist_remove(nvl, name, DATA_TYPE_BYTE_ARRAY);
		if (error == -ENOENT)
			error = zpl_xattr_set_dir(ip, name, NULL, 0, flags, cr);
	} else {
		/* Limited to 32k to keep nvpair memory allocations small */
		if (size > DXATTR_MAX_ENTRY_SIZE)
			return (-EFBIG);

		/* Prevent the DXATTR SA from consuming the entire SA region */
		error = -nvlist_size(nvl, &sa_size, NV_ENCODE_XDR);
		if (error)
			return (error);

		if (sa_size > DXATTR_MAX_SA_SIZE)
			return (-EFBIG);

		error = -nvlist_add_byte_array(nvl, name,
		    (uchar_t *)value, size);
	}

	/*
	 * Update the SA for additions, modifications, and removals. On
	 * error drop the inconsistent cached version of the nvlist, it
	 * will be reconstructed from the ARC when next accessed.
	 */
	if (error == 0)
		error = -zfs_sa_set_xattr(zp);

	if (error) {
		nvlist_free(nvl);
		zp->z_xattr_cached = NULL;
	}

	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_xattr_set(struct inode *ip, const char *name, const void *value,
    size_t size, int flags)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int where;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	ZPL_ENTER(zfsvfs);
	ZPL_VERIFY_ZP(zp);
	rw_enter(&ITOZ(ip)->z_xattr_lock, RW_WRITER);

	/*
	 * Before setting the xattr check to see if it already exists.
	 * This is done to ensure the following optional flags are honored.
	 *
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REPLACE: fail if xattr does not exist
	 *
	 * We also want to know if it resides in sa or dir, so we can make
	 * sure we don't end up with duplicate in both places.
	 */
	error = __zpl_xattr_where(ip, name, &where, cr);
	if (error < 0) {
		if (error != -ENODATA)
			goto out;
		if (flags & XATTR_REPLACE)
			goto out;

		/* The xattr to be removed already doesn't exist */
		error = 0;
		if (value == NULL)
			goto out;
	} else {
		error = -EEXIST;
		if (flags & XATTR_CREATE)
			goto out;
	}

	/* Preferentially store the xattr as a SA for better performance */
	if (zfsvfs->z_use_sa && zp->z_is_sa &&
	    (zfsvfs->z_xattr_sa || (value == NULL && where & XATTR_IN_SA))) {
		error = zpl_xattr_set_sa(ip, name, value, size, flags, cr);
		if (error == 0) {
			/*
			 * Successfully put into SA, we need to clear the one
			 * in dir.
			 */
			if (where & XATTR_IN_DIR)
				zpl_xattr_set_dir(ip, name, NULL, 0, 0, cr);
			goto out;
		}
	}

	error = zpl_xattr_set_dir(ip, name, value, size, flags, cr);
	/*
	 * Successfully put into dir, we need to clear the one in SA.
	 */
	if (error == 0 && (where & XATTR_IN_SA))
		zpl_xattr_set_sa(ip, name, NULL, 0, 0, cr);
out:
	rw_exit(&ITOZ(ip)->z_xattr_lock);
	ZPL_EXIT(zfsvfs);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

/*
 * Extended user attributes
 *
 * "Extended user attributes may be assigned to files and directories for
 * storing arbitrary additional information such as the mime type,
 * character set or encoding of a file.  The access permissions for user
 * attributes are defined by the file permission bits: read permission
 * is required to retrieve the attribute value, and writer permission is
 * required to change it.
 *
 * The file permission bits of regular files and directories are
 * interpreted differently from the file permission bits of special
 * files and symbolic links.  For regular files and directories the file
 * permission bits define access to the file's contents, while for
 * device special files they define access to the device described by
 * the special file.  The file permissions of symbolic links are not
 * used in access checks.  These differences would allow users to
 * consume filesystem resources in a way not controllable by disk quotas
 * for group or world writable special files and directories.
 *
 * For this reason, extended user attributes are allowed only for
 * regular files and directories, and access to extended user attributes
 * is restricted to the owner and to users with appropriate capabilities
 * for directories with the sticky bit set (see the chmod(1) manual page
 * for an explanation of the sticky bit)." - xattr(7)
 *
 * ZFS allows extended user attributes to be disabled administratively
 * by setting the 'xattr=off' property on the dataset.
 */
static int
__zpl_xattr_user_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return (ITOZSB(ip)->z_flags & ZSB_XATTR);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_user_list);

static int
__zpl_xattr_user_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	boolean_t compat = !!(ITOZSB(ip)->z_flags & ZSB_XATTR_COMPAT);
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") == 0)
		return (-EINVAL);
#endif
	if (ZFS_XA_NS_PREFIX_FORBIDDEN(name))
		return (-EINVAL);
	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return (-EOPNOTSUPP);

	/*
	 * Try to look up the name without the namespace prefix first for
	 * compatibility with xattrs from other platforms.  If that fails,
	 * try again with the namespace prefix.
	 */
	error = zpl_xattr_get(ip, name, value, size);
	if (!compat || error == -ENODATA) {
		char *xattr_name;
		xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
		error = zpl_xattr_get(ip, xattr_name, value, size);
		kmem_strfree(xattr_name);
	}

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_user_get);

static int
__zpl_xattr_user_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	boolean_t compat = !!(ITOZSB(ip)->z_flags & ZSB_XATTR_COMPAT);
	int error = 0;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") == 0)
		return (-EINVAL);
#endif
	if (ZFS_XA_NS_PREFIX_FORBIDDEN(name))
		return (-EINVAL);
	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return (-EOPNOTSUPP);

	/*
	 * Remove any namespaced version of the xattr so we only set the
	 * version compatible with other platforms.
	 *
	 * The following flags must be handled correctly:
	 *
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REPLACE: fail if xattr does not exist
	 */
	xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	if (compat)
		error = zpl_xattr_set(ip, xattr_name, NULL, 0, flags);
	if (!compat)
		error = zpl_xattr_set(ip, xattr_name, value, size, flags);
	kmem_strfree(xattr_name);

	if (!compat || error == -EEXIST)
		return (error);
	if (error == 0 && (flags & XATTR_REPLACE))
		flags &= ~XATTR_REPLACE;
	error = zpl_xattr_set(ip, name, value, size, flags);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_user_set);

xattr_handler_t zpl_xattr_user_handler =
{
	.prefix	= XATTR_USER_PREFIX,
	.list	= zpl_xattr_user_list,
	.get	= zpl_xattr_user_get,
	.set	= zpl_xattr_user_set,
};

/*
 * Trusted extended attributes
 *
 * "Trusted extended attributes are visible and accessible only to
 * processes that have the CAP_SYS_ADMIN capability.  Attributes in this
 * class are used to implement mechanisms in user space (i.e., outside
 * the kernel) which keep information in extended attributes to which
 * ordinary processes should not have access." - xattr(7)
 */
static int
__zpl_xattr_trusted_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return (capable(CAP_SYS_ADMIN));
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_trusted_list);

static int
__zpl_xattr_trusted_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return (-EACCES);
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") == 0)
		return (-EINVAL);
#endif
	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_trusted_get);

static int
__zpl_xattr_trusted_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return (-EACCES);
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") == 0)
		return (-EINVAL);
#endif
	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_trusted_set);

xattr_handler_t zpl_xattr_trusted_handler =
{
	.prefix	= XATTR_TRUSTED_PREFIX,
	.list	= zpl_xattr_trusted_list,
	.get	= zpl_xattr_trusted_get,
	.set	= zpl_xattr_trusted_set,
};

/*
 * Extended security attributes
 *
 * "The security attribute namespace is used by kernel security modules,
 * such as Security Enhanced Linux, and also to implement file
 * capabilities (see capabilities(7)).  Read and write access
 * permissions to security attributes depend on the policy implemented
 * for each security attribute by the security module.  When no security
 * module is loaded, all processes have read access to extended security
 * attributes, and write access is limited to processes that have the
 * CAP_SYS_ADMIN capability." - xattr(7)
 */
static int
__zpl_xattr_security_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return (1);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_security_list);

static int
__zpl_xattr_security_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") == 0)
		return (-EINVAL);
#endif
	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_security_get);

static int
__zpl_xattr_security_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") == 0)
		return (-EINVAL);
#endif
	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags);
	kmem_strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_security_set);

static int
zpl_xattr_security_init_impl(struct inode *ip, const struct xattr *xattrs,
    void *fs_info)
{
	const struct xattr *xattr;
	int error = 0;

	for (xattr = xattrs; xattr->name != NULL; xattr++) {
		error = __zpl_xattr_security_set(ip,
		    xattr->name, xattr->value, xattr->value_len, 0);

		if (error < 0)
			break;
	}

	return (error);
}

int
zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr)
{
	return security_inode_init_security(ip, dip, qstr,
	    &zpl_xattr_security_init_impl, NULL);
}

/*
 * Security xattr namespace handlers.
 */
xattr_handler_t zpl_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.list	= zpl_xattr_security_list,
	.get	= zpl_xattr_security_get,
	.set	= zpl_xattr_security_set,
};

/*
 * Extended system attributes
 *
 * "Extended system attributes are used by the kernel to store system
 * objects such as Access Control Lists.  Read and write access permissions
 * to system attributes depend on the policy implemented for each system
 * attribute implemented by filesystems in the kernel." - xattr(7)
 */
#ifdef CONFIG_FS_POSIX_ACL
static int
zpl_set_acl_impl(struct inode *ip, struct posix_acl *acl, int type)
{
	char *name, *value = NULL;
	int error = 0;
	size_t size = 0;

	if (S_ISLNK(ip->i_mode))
		return (-EOPNOTSUPP);

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		if (acl) {
			umode_t mode = ip->i_mode;
			error = posix_acl_equiv_mode(acl, &mode);
			if (error < 0) {
				return (error);
			} else {
				/*
				 * The mode bits will have been set by
				 * ->zfs_setattr()->zfs_acl_chmod_setattr()
				 * using the ZFS ACL conversion.  If they
				 * differ from the Posix ACL conversion dirty
				 * the inode to write the Posix mode bits.
				 */
				if (ip->i_mode != mode) {
					ip->i_mode = mode;
					ip->i_ctime = current_time(ip);
					zfs_mark_inode_dirty(ip);
				}

				if (error == 0)
					acl = NULL;
			}
		}
		break;

	case ACL_TYPE_DEFAULT:
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		if (!S_ISDIR(ip->i_mode))
			return (acl ? -EACCES : 0);
		break;

	default:
		return (-EINVAL);
	}

	if (acl) {
		size = posix_acl_xattr_size(acl->a_count);
		value = kmem_alloc(size, KM_SLEEP);

		error = zpl_acl_to_xattr(acl, value, size);
		if (error < 0) {
			kmem_free(value, size);
			return (error);
		}
	}

	error = zpl_xattr_set(ip, name, value, size, 0);
	if (value)
		kmem_free(value, size);

	if (!error) {
		if (acl)
			zpl_set_cached_acl(ip, type, acl);
		else
			zpl_forget_cached_acl(ip, type);
	}

	return (error);
}

#ifdef HAVE_SET_ACL
int
#ifdef HAVE_SET_ACL_USERNS
zpl_set_acl(struct user_namespace *userns, struct inode *ip,
    struct posix_acl *acl, int type)
#else
zpl_set_acl(struct inode *ip, struct posix_acl *acl, int type)
#endif /* HAVE_SET_ACL_USERNS */
{
	return (zpl_set_acl_impl(ip, acl, type));
}
#endif /* HAVE_SET_ACL */

static struct posix_acl *
zpl_get_acl_impl(struct inode *ip, int type)
{
	struct posix_acl *acl;
	void *value = NULL;
	char *name;

	/*
	 * As of Linux 3.14, the kernel get_acl will check this for us.
	 * Also as of Linux 4.7, comparing against ACL_NOT_CACHED is wrong
	 * as the kernel get_acl will set it to temporary sentinel value.
	 */
#ifndef HAVE_KERNEL_GET_ACL_HANDLE_CACHE
	acl = get_cached_acl(ip, type);
	if (acl != ACL_NOT_CACHED)
		return (acl);
#endif

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = XATTR_NAME_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = XATTR_NAME_POSIX_ACL_DEFAULT;
		break;
	default:
		return (ERR_PTR(-EINVAL));
	}

	int size = zpl_xattr_get(ip, name, NULL, 0);
	if (size > 0) {
		value = kmem_alloc(size, KM_SLEEP);
		size = zpl_xattr_get(ip, name, value, size);
	}

	if (size > 0) {
		acl = zpl_acl_from_xattr(value, size);
	} else if (size == -ENODATA || size == -ENOSYS) {
		acl = NULL;
	} else {
		acl = ERR_PTR(-EIO);
	}

	if (size > 0)
		kmem_free(value, size);

	/* As of Linux 4.7, the kernel get_acl will set this for us */
#ifndef HAVE_KERNEL_GET_ACL_HANDLE_CACHE
	if (!IS_ERR(acl))
		zpl_set_cached_acl(ip, type, acl);
#endif

	return (acl);
}

#if defined(HAVE_GET_ACL_RCU)
struct posix_acl *
zpl_get_acl(struct inode *ip, int type, bool rcu)
{
	if (rcu)
		return (ERR_PTR(-ECHILD));

	return (zpl_get_acl_impl(ip, type));
}
#elif defined(HAVE_GET_ACL)
struct posix_acl *
zpl_get_acl(struct inode *ip, int type)
{
	return (zpl_get_acl_impl(ip, type));
}
#else
#error "Unsupported iops->get_acl() implementation"
#endif /* HAVE_GET_ACL_RCU */

int
zpl_init_acl(struct inode *ip, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	int error = 0;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (!S_ISLNK(ip->i_mode)) {
		acl = zpl_get_acl_impl(dir, ACL_TYPE_DEFAULT);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		if (!acl) {
			ip->i_mode &= ~current_umask();
			ip->i_ctime = current_time(ip);
			zfs_mark_inode_dirty(ip);
			return (0);
		}
	}

	if (acl) {
		umode_t mode;

		if (S_ISDIR(ip->i_mode)) {
			error = zpl_set_acl_impl(ip, acl, ACL_TYPE_DEFAULT);
			if (error)
				goto out;
		}

		mode = ip->i_mode;
		error = __posix_acl_create(&acl, GFP_KERNEL, &mode);
		if (error >= 0) {
			ip->i_mode = mode;
			zfs_mark_inode_dirty(ip);
			if (error > 0) {
				error = zpl_set_acl_impl(ip, acl,
				    ACL_TYPE_ACCESS);
			}
		}
	}
out:
	zpl_posix_acl_release(acl);

	return (error);
}

int
zpl_chmod_acl(struct inode *ip)
{
	struct posix_acl *acl;
	int error;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (S_ISLNK(ip->i_mode))
		return (-EOPNOTSUPP);

	acl = zpl_get_acl_impl(ip, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return (PTR_ERR(acl));

	error = __posix_acl_chmod(&acl, GFP_KERNEL, ip->i_mode);
	if (!error)
		error = zpl_set_acl_impl(ip, acl, ACL_TYPE_ACCESS);

	zpl_posix_acl_release(acl);

	return (error);
}

static int
__zpl_xattr_acl_list_access(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	char *xattr_name = XATTR_NAME_POSIX_ACL_ACCESS;
	size_t xattr_size = sizeof (XATTR_NAME_POSIX_ACL_ACCESS);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (list && xattr_size <= list_size)
		memcpy(list, xattr_name, xattr_size);

	return (xattr_size);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_acl_list_access);

static int
__zpl_xattr_acl_list_default(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	char *xattr_name = XATTR_NAME_POSIX_ACL_DEFAULT;
	size_t xattr_size = sizeof (XATTR_NAME_POSIX_ACL_DEFAULT);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (0);

	if (list && xattr_size <= list_size)
		memcpy(list, xattr_name, xattr_size);

	return (xattr_size);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_acl_list_default);

static int
__zpl_xattr_acl_get_access(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	struct posix_acl *acl;
	int type = ACL_TYPE_ACCESS;
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") != 0)
		return (-EINVAL);
#endif
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

	acl = zpl_get_acl_impl(ip, type);
	if (IS_ERR(acl))
		return (PTR_ERR(acl));
	if (acl == NULL)
		return (-ENODATA);

	error = zpl_acl_to_xattr(acl, buffer, size);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_acl_get_access);

static int
__zpl_xattr_acl_get_default(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	struct posix_acl *acl;
	int type = ACL_TYPE_DEFAULT;
	int error;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") != 0)
		return (-EINVAL);
#endif
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

	acl = zpl_get_acl_impl(ip, type);
	if (IS_ERR(acl))
		return (PTR_ERR(acl));
	if (acl == NULL)
		return (-ENODATA);

	error = zpl_acl_to_xattr(acl, buffer, size);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_acl_get_default);

static int
__zpl_xattr_acl_set_access(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	struct posix_acl *acl;
	int type = ACL_TYPE_ACCESS;
	int error = 0;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") != 0)
		return (-EINVAL);
#endif
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

	if (!zpl_inode_owner_or_capable(kcred->user_ns, ip))
		return (-EPERM);

	if (value) {
		acl = zpl_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		else if (acl) {
			error = zpl_posix_acl_valid(ip, acl);
			if (error) {
				zpl_posix_acl_release(acl);
				return (error);
			}
		}
	} else {
		acl = NULL;
	}
	error = zpl_set_acl_impl(ip, acl, type);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_acl_set_access);

static int
__zpl_xattr_acl_set_default(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	struct posix_acl *acl;
	int type = ACL_TYPE_DEFAULT;
	int error = 0;
	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") != 0)
		return (-EINVAL);
#endif
	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIX)
		return (-EOPNOTSUPP);

	if (!zpl_inode_owner_or_capable(kcred->user_ns, ip))
		return (-EPERM);

	if (value) {
		acl = zpl_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		else if (acl) {
			error = zpl_posix_acl_valid(ip, acl);
			if (error) {
				zpl_posix_acl_release(acl);
				return (error);
			}
		}
	} else {
		acl = NULL;
	}

	error = zpl_set_acl_impl(ip, acl, type);
	zpl_posix_acl_release(acl);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_acl_set_default);

/*
 * ACL access xattr namespace handlers.
 *
 * Use .name instead of .prefix when available. xattr_resolve_name will match
 * whole name and reject anything that has .name only as prefix.
 */
xattr_handler_t zpl_xattr_acl_access_handler =
{
#ifdef HAVE_XATTR_HANDLER_NAME
	.name	= XATTR_NAME_POSIX_ACL_ACCESS,
#else
	.prefix	= XATTR_NAME_POSIX_ACL_ACCESS,
#endif
	.list	= zpl_xattr_acl_list_access,
	.get	= zpl_xattr_acl_get_access,
	.set	= zpl_xattr_acl_set_access,
#if defined(HAVE_XATTR_LIST_SIMPLE) || \
    defined(HAVE_XATTR_LIST_DENTRY) || \
    defined(HAVE_XATTR_LIST_HANDLER)
	.flags	= ACL_TYPE_ACCESS,
#endif
};

/*
 * ACL default xattr namespace handlers.
 *
 * Use .name instead of .prefix when available. xattr_resolve_name will match
 * whole name and reject anything that has .name only as prefix.
 */
xattr_handler_t zpl_xattr_acl_default_handler =
{
#ifdef HAVE_XATTR_HANDLER_NAME
	.name	= XATTR_NAME_POSIX_ACL_DEFAULT,
#else
	.prefix	= XATTR_NAME_POSIX_ACL_DEFAULT,
#endif
	.list	= zpl_xattr_acl_list_default,
	.get	= zpl_xattr_acl_get_default,
	.set	= zpl_xattr_acl_set_default,
#if defined(HAVE_XATTR_LIST_SIMPLE) || \
    defined(HAVE_XATTR_LIST_DENTRY) || \
    defined(HAVE_XATTR_LIST_HANDLER)
	.flags	= ACL_TYPE_DEFAULT,
#endif
};

#endif /* CONFIG_FS_POSIX_ACL */

int
zpl_permission(struct inode *ip, int mask)
{
	int to_check = 0, i, ret;
	cred_t *cr = NULL;

	/*
	 * If NFSv4 ACLs are not being used, go back to
	 * generic_permission(). If ACL is trivial and the
	 * mask is representable by POSIX permissions, then
	 * also go back to generic_permission().
	 */
	if ((ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_NFSV4) ||
	    ((ITOZ(ip)->z_pflags & ZFS_ACL_TRIVIAL && GENERIC_MASK(mask)))) {
		return (generic_permission(ip, mask));
	}

	for (i = 0; i < ARRAY_SIZE(mask2zfs); i++) {
		if (mask & mask2zfs[i].kmask) {
			to_check |= mask2zfs[i].zfsperm;
		}
	}

	/*
	 * We're being asked to check something that doesn't contain an
	 * NFSv4 ACE. Pass back to default kernel permissions check.
	 */
	if (to_check == 0) {
		return (generic_permission(ip, mask));
	}

	/*
	 *  Avoid potentially blocking in RCU walk.
	 */
	if (mask & MAY_NOT_BLOCK) {
		return (-ECHILD);
	}

	cr = CRED();
	crhold(cr);
	ret = -zfs_access(ITOZ(ip), to_check, V_ACE_MASK, cr);
	if (ret != -EPERM && ret != -EACCES) {
		crfree(cr);
		return (ret);
	}

	/*
	 * There are some situations in which capabilities
	 * may allow overriding the DACL.
	 */
	if (S_ISDIR(ip->i_mode)) {
#ifdef SB_NFSV4ACL
		if (!(mask & (MAY_WRITE | NFS41ACL_WRITE_ALL))) {
#else
		if (!(mask & MAY_WRITE)) {
#endif
			if (capable(CAP_DAC_READ_SEARCH)) {
				crfree(cr);
				return (0);
			}
		}
		if (capable(CAP_DAC_OVERRIDE)) {
			crfree(cr);
			return (0);
		}
		crfree(cr);
		return (ret);
	}

	if (to_check == ACE_READ_DATA) {
		if (capable(CAP_DAC_READ_SEARCH)) {
			crfree(cr);
			return (0);
		}
	}

	if (!(mask & MAY_EXEC) ||
	    (zfs_fastaccesschk_execute(ITOZ(ip), cr) == 0)) {
		if (capable(CAP_DAC_OVERRIDE)) {
			crfree(cr);
			return (0);
		}
	}

	crfree(cr);
	return (ret);
}

#define	NFS41ACL_MAX_ACES	128
#define	NFS41_FLAGS		(ACE_DIRECTORY_INHERIT_ACE| \
				ACE_FILE_INHERIT_ACE| \
				ACE_NO_PROPAGATE_INHERIT_ACE| \
				ACE_INHERIT_ONLY_ACE| \
				ACE_INHERITED_ACE| \
				ACE_IDENTIFIER_GROUP)

/*
 * Macros for sanity checks related to XDR and ACL buffer sizes
 */
#define	ACE4SIZE		(sizeof (nfsace4i))
#define	ACLBASE			(sizeof (nfsacl41i))
#define	XDRBASE			(2 * sizeof (uint_t))

#define	ACES_TO_SIZE(x, y)	(x + (y * ACE4SIZE))
#define	SIZE_TO_ACES(x, y)	((y - x) / ACE4SIZE)
#define	SIZE_IS_VALID(x, y)	((x >= ACES_TO_SIZE(y, 0)) && \
				(((x - y) % ACE4SIZE) == 0))

#define	ACES_TO_ACLSIZE(x)	(ACES_TO_SIZE(ACLBASE, x))
#define	ACES_TO_XDRSIZE(x)	(ACES_TO_SIZE(XDRBASE, x))

#define	ACLSIZE_TO_ACES(x)	(SIZE_TO_ACES(ACLBASE, x))
#define	XDRSIZE_TO_ACES(x)	(SIZE_TO_ACES(XDRBASE, x))

#define	ACLSIZE_IS_VALID(x)	(SIZE_IS_VALID(x, ACLBASE))
#define	XDRSIZE_IS_VALID(x)	(SIZE_IS_VALID(x, XDRBASE))

static int
__zpl_xattr_nfs41acl_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	char *xattr_name = NFS41ACL_XATTR;
	size_t xattr_size = sizeof (NFS41ACL_XATTR);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_NFSV4)
		return (0);

	if (list && xattr_size <= list_size)
		memcpy(list, xattr_name, xattr_size);

	return (xattr_size);
}
ZPL_XATTR_LIST_WRAPPER(zpl_xattr_nfs41acl_list);

static int
acep_to_nfsace4i(const ace_t *acep, nfsace4i *nacep)
{
	nacep->type = acep->a_type;
	nacep->flag = acep->a_flags & NFS41_FLAGS;
	nacep->access_mask = acep->a_access_mask;

	switch (acep->a_flags & ACE_TYPE_FLAGS) {
	case ACE_OWNER:
		nacep->iflag |= ACEI4_SPECIAL_WHO;
		nacep->who = ACE4_SPECIAL_OWNER;
		break;

	case ACE_GROUP|ACE_IDENTIFIER_GROUP:
		nacep->iflag |= ACEI4_SPECIAL_WHO;
		nacep->who = ACE4_SPECIAL_GROUP;
		break;

	case ACE_EVERYONE:
		nacep->iflag |= ACEI4_SPECIAL_WHO;
		nacep->who = ACE4_SPECIAL_EVERYONE;
		break;

	case ACE_IDENTIFIER_GROUP:
	case 0:
		nacep->who = acep->a_who;
		break;

	default:
		dprintf("Unknown ACE_TYPE_FLAG 0x%08x\n",
		    acep->a_flags & ACE_TYPE_FLAGS);
		return (-EINVAL);
	}

	return (0);
}

static int
zfsacl_to_nfsacl41i(const vsecattr_t vsecp, nfsacl41i **_nacl, size_t *_size)
{
	nfsacl41i *nacl = NULL;
	nfsace4i *nacep = NULL;
	ace_t *acep = NULL;

	int i, error;
	size_t acl_size;

	acl_size = ACES_TO_ACLSIZE(vsecp.vsa_aclcnt);

	nacl = kmem_alloc(acl_size, KM_SLEEP);
	nacl->na41_aces.na41_aces_len = vsecp.vsa_aclcnt;
	nacl->na41_flag = vsecp.vsa_aclflags;
	nacep = (nfsace4i *)((char *)nacl + sizeof (nfsacl41i));
	nacl->na41_aces.na41_aces_val = nacep;

	for (i = 0; i < nacl->na41_aces.na41_aces_len; i++) {
		nacep = &nacl->na41_aces.na41_aces_val[i];
		acep = vsecp.vsa_aclentp + (i * sizeof (ace_t));
		error = acep_to_nfsace4i(acep, nacep);
		if (error) {
			kmem_free(nacl, acl_size);
			return (error);
		}
	}
	*_size = acl_size;
	*_nacl = nacl;
	return (0);
}

static int
nfsace4i_to_acep(const nfsace4i *nacep, ace_t *acep)
{
	acep->a_type = nacep->type;
	acep->a_flags = nacep->flag & NFS41_FLAGS;
	acep->a_access_mask = nacep->access_mask;
	if (nacep->iflag & ACEI4_SPECIAL_WHO) {
		switch (nacep->who) {
		case ACE4_SPECIAL_OWNER:
			acep->a_flags |= ACE_OWNER;
			acep->a_who = -1;
			break;

		case ACE4_SPECIAL_GROUP:
			acep->a_flags |= (ACE_GROUP | ACE_IDENTIFIER_GROUP);
			acep->a_who = -1;
			break;

		case ACE4_SPECIAL_EVERYONE:
			acep->a_flags |= ACE_EVERYONE;
			acep->a_who = -1;
			break;

		default:
			dprintf("Unknown id 0x%08x\n", nacep->who);
			return (-EINVAL);
		}
	} else {
		acep->a_who = nacep->who;
	}

	return (0);
}

static int
nfsacl41i_to_zfsacl(const nfsacl41i *nacl, vsecattr_t *_vsecp)
{
	int i, error = 0;
	vsecattr_t vsecp;

	vsecp.vsa_aclcnt = nacl->na41_aces.na41_aces_len;
	vsecp.vsa_aclflags = nacl->na41_flag;
	vsecp.vsa_aclentsz = vsecp.vsa_aclcnt * sizeof (ace_t);
	vsecp.vsa_mask = (VSA_ACE | VSA_ACE_ACLFLAGS);
	vsecp.vsa_aclentp = kmem_alloc(vsecp.vsa_aclentsz, KM_SLEEP);

	for (i = 0; i < vsecp.vsa_aclcnt; i++) {
		ace_t *acep = vsecp.vsa_aclentp + (i * sizeof (ace_t));
		nfsace4i *nacep = &nacl->na41_aces.na41_aces_val[i];
		error = nfsace4i_to_acep(nacep, acep);
		if (error) {
			return (error);
		}
	}
	*_vsecp = vsecp;
	return (error);
}

static int
__zpl_xattr_nfs41acl_get(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	vsecattr_t vsecp;
	cred_t *cr = CRED();
	int ret, fl;
	size_t acl_size = 0, xdr_size = 0;
	XDR xdr = {0};
	boolean_t ok;
	nfsacl41i *nacl = NULL;

	/* xattr_resolve_name will do this for us if this is defined */
#ifndef HAVE_XATTR_HANDLER_NAME
	if (strcmp(name, "") != 0)
		return (-EINVAL);
#endif

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_NFSV4)
		return (-EOPNOTSUPP);

	if (size == 0) {
		/*
		 * API user may send 0 size so that we
		 * return size of buffer needed for ACL.
		 */
		crhold(cr);
		vsecp.vsa_mask = VSA_ACECNT;
		ret = -zfs_getsecattr(ITOZ(ip), &vsecp, ATTR_NOACLCHECK, cr);
		if (ret) {
			return (ret);
		}
		crfree(cr);
		ret = ACES_TO_XDRSIZE(vsecp.vsa_aclcnt);
		return (ret);
	}

	if (size < ACES_TO_XDRSIZE(1)) {
		return (-EINVAL);
	}

	vsecp.vsa_mask = VSA_ACE_ALLTYPES | VSA_ACECNT | VSA_ACE |
	    VSA_ACE_ACLFLAGS;

	crhold(cr);
	fl = capable(CAP_DAC_OVERRIDE) ? ATTR_NOACLCHECK : 0;
	ret = -zfs_getsecattr(ITOZ(ip), &vsecp, fl, cr);
	crfree(cr);

	if (ret) {
		return (ret);
	}

	if (vsecp.vsa_aclcnt == 0) {
		ret = -ENODATA;
		goto nfs4acl_get_out;
	}

	xdr_size = ACES_TO_XDRSIZE(vsecp.vsa_aclcnt);
	if (xdr_size > size) {
		ret = -ERANGE;
		goto nfs4acl_get_out;
	}

	ret = zfsacl_to_nfsacl41i(vsecp, &nacl, &acl_size);
	if (ret) {
		ret = -ENOMEM;
		goto nfs4acl_get_out;
	}

	xdrmem_create(&xdr, (char *)buffer, xdr_size, XDR_ENCODE);
	ok = xdr_nfsacl41i(&xdr, nacl);
	if (!ok) {
		ret = -ENOMEM;
		kmem_free(nacl, acl_size);
		goto nfs4acl_get_out;
	}

	kmem_free(nacl, acl_size);
	ret = xdr_size;

nfs4acl_get_out:
	kmem_free(vsecp.vsa_aclentp, vsecp.vsa_aclentsz);

	return (ret);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_nfs41acl_get);

static int
__zpl_xattr_nfs41acl_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	cred_t *cr = CRED();
	vsecattr_t vsecp;
	char *bufp = NULL;
	nfsacl41i *nacl = NULL;
	boolean_t ok;
	XDR xdr = {0};
	size_t acl_size = 0;
	int error, fl, naces;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_NFSV4)
		return (-EOPNOTSUPP);

	/*
	 * TODO: we may receive NULL value and size 0
	 * when rmxattr() on our special xattr is called.
	 * A function to "strip" the ACL needs to be added
	 * to avoid POLA violation.
	 */

	/* xdr data is 4-byte aligned */
	if (((ulong_t)value % 4) != 0) {
		return (-EINVAL);
	}

	naces = XDRSIZE_TO_ACES(size);
	if (naces > NFS41ACL_MAX_ACES) {
		return (-E2BIG);
	}

	if (!XDRSIZE_IS_VALID(size)) {
		return (-EINVAL);
	}
	bufp = (char *)value;
	acl_size = ACES_TO_ACLSIZE(naces);
	nacl = kmem_alloc(sizeof (nfsacl41i), KM_SLEEP);
	/*
	 * NULL may still be returned with KM_SLEEP set.
	 * In principal, checks for SIZE of xattr are
	 * sufficient to protect, but check for NULL anyway.
	 */
	if (nacl == NULL) {
		return (-ENOMEM);
	}

	xdrmem_create(&xdr, bufp, acl_size, XDR_DECODE);
	ok = xdr_nfsacl41i(&xdr, nacl);
	if (!ok) {
		kmem_free(nacl, sizeof (nfsacl41i));
		return (-ENOMEM);
	}
	error = nfsacl41i_to_zfsacl(nacl, &vsecp);
	if (error) {
		kmem_free(nacl, sizeof (nfsacl41i));
		return (error);
	}

	/* XDR_DECODE allocates memory for the array of aces */
	kmem_free(nacl->na41_aces.na41_aces_val,
	    (nacl->na41_aces.na41_aces_len * ACE4SIZE));
	kmem_free(nacl, sizeof (nfsacl41i));

	crhold(cr);
	fl = capable(CAP_DAC_OVERRIDE) ? ATTR_NOACLCHECK : 0;
	error = -zfs_setsecattr(ITOZ(ip), &vsecp, fl, cr);
	crfree(cr);

	kmem_free(vsecp.vsa_aclentp, vsecp.vsa_aclentsz);
	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_nfs41acl_set);

/*
 * ACL access xattr namespace handlers.
 *
 * Use .name instead of .prefix when available. xattr_resolve_name will match
 * whole name and reject anything that has .name only as prefix.
 */
xattr_handler_t zpl_xattr_nfs41acl_handler =
{
#ifdef HAVE_XATTR_HANDLER_NAME
	.name	= NFS41ACL_XATTR,
#else
	.prefix	= NFS41ACL_XATTR,
#endif
	.list	= zpl_xattr_nfs41acl_list,
	.get	= zpl_xattr_nfs41acl_get,
	.set	= zpl_xattr_nfs41acl_set,
};

xattr_handler_t *zpl_xattr_handlers[] = {
	&zpl_xattr_security_handler,
	&zpl_xattr_trusted_handler,
	&zpl_xattr_user_handler,
#ifdef CONFIG_FS_POSIX_ACL
	&zpl_xattr_acl_access_handler,
	&zpl_xattr_acl_default_handler,
#endif /* CONFIG_FS_POSIX_ACL */
	&zpl_xattr_nfs41acl_handler,
	NULL
};

static const struct xattr_handler *
zpl_xattr_handler(const char *name)
{
	if (strncmp(name, XATTR_USER_PREFIX,
	    XATTR_USER_PREFIX_LEN) == 0)
		return (&zpl_xattr_user_handler);

	if (strncmp(name, XATTR_TRUSTED_PREFIX,
	    XATTR_TRUSTED_PREFIX_LEN) == 0)
		return (&zpl_xattr_trusted_handler);

	if (strncmp(name, XATTR_SECURITY_PREFIX,
	    XATTR_SECURITY_PREFIX_LEN) == 0)
		return (&zpl_xattr_security_handler);

#ifdef CONFIG_FS_POSIX_ACL
	if (strncmp(name, XATTR_NAME_POSIX_ACL_ACCESS,
	    sizeof (XATTR_NAME_POSIX_ACL_ACCESS)) == 0)
		return (&zpl_xattr_acl_access_handler);

	if (strncmp(name, XATTR_NAME_POSIX_ACL_DEFAULT,
	    sizeof (XATTR_NAME_POSIX_ACL_DEFAULT)) == 0)
		return (&zpl_xattr_acl_default_handler);
#endif /* CONFIG_FS_POSIX_ACL */

	if (strncmp(name, NFS41ACL_XATTR,
	    sizeof (NFS41ACL_XATTR)) == 0)
		return (&zpl_xattr_nfs41acl_handler);

	return (NULL);
}

static enum xattr_permission
zpl_xattr_permission(xattr_filldir_t *xf, const char *name, int name_len)
{
	const struct xattr_handler *handler;
	struct dentry *d = xf->dentry;
	boolean_t compat = !!(ITOZSB(d->d_inode)->z_flags & ZSB_XATTR_COMPAT);
	enum xattr_permission perm = XAPERM_ALLOW;

	handler = zpl_xattr_handler(name);
	if (handler == NULL) {
		if (!compat)
			return (XAPERM_DENY);
		/* Do not expose FreeBSD system namespace xattrs. */
		if (ZFS_XA_NS_PREFIX_MATCH(FREEBSD, name))
			return (XAPERM_DENY);
		/*
		 * Anything that doesn't match a known namespace gets put in the
		 * user namespace for compatibility with other platforms.
		 */
		perm = XAPERM_COMPAT;
		handler = &zpl_xattr_user_handler;
	}

	if (handler->list) {
#if defined(HAVE_XATTR_LIST_SIMPLE)
		if (!handler->list(d))
			return (XAPERM_DENY);
#elif defined(HAVE_XATTR_LIST_DENTRY)
		if (!handler->list(d, NULL, 0, name, name_len, 0))
			return (XAPERM_DENY);
#elif defined(HAVE_XATTR_LIST_HANDLER)
		if (!handler->list(handler, d, NULL, 0, name, name_len))
			return (XAPERM_DENY);
#endif
	}

	return (perm);
}

#if !defined(HAVE_POSIX_ACL_RELEASE) || defined(HAVE_POSIX_ACL_RELEASE_GPL_ONLY)
struct acl_rel_struct {
	struct acl_rel_struct *next;
	struct posix_acl *acl;
	clock_t time;
};

#define	ACL_REL_GRACE	(60*HZ)
#define	ACL_REL_WINDOW	(1*HZ)
#define	ACL_REL_SCHED	(ACL_REL_GRACE+ACL_REL_WINDOW)

/*
 * Lockless multi-producer single-consumer fifo list.
 * Nodes are added to tail and removed from head. Tail pointer is our
 * synchronization point. It always points to the next pointer of the last
 * node, or head if list is empty.
 */
static struct acl_rel_struct *acl_rel_head = NULL;
static struct acl_rel_struct **acl_rel_tail = &acl_rel_head;

static void
zpl_posix_acl_free(void *arg)
{
	struct acl_rel_struct *freelist = NULL;
	struct acl_rel_struct *a;
	clock_t new_time;
	boolean_t refire = B_FALSE;

	ASSERT3P(acl_rel_head, !=, NULL);
	while (acl_rel_head) {
		a = acl_rel_head;
		if (ddi_get_lbolt() - a->time >= ACL_REL_GRACE) {
			/*
			 * If a is the last node we need to reset tail, but we
			 * need to use cmpxchg to make sure it is still the
			 * last node.
			 */
			if (acl_rel_tail == &a->next) {
				acl_rel_head = NULL;
				if (cmpxchg(&acl_rel_tail, &a->next,
				    &acl_rel_head) == &a->next) {
					ASSERT3P(a->next, ==, NULL);
					a->next = freelist;
					freelist = a;
					break;
				}
			}
			/*
			 * a is not last node, make sure next pointer is set
			 * by the adder and advance the head.
			 */
			while (READ_ONCE(a->next) == NULL)
				cpu_relax();
			acl_rel_head = a->next;
			a->next = freelist;
			freelist = a;
		} else {
			/*
			 * a is still in grace period. We are responsible to
			 * reschedule the free task, since adder will only do
			 * so if list is empty.
			 */
			new_time = a->time + ACL_REL_SCHED;
			refire = B_TRUE;
			break;
		}
	}

	if (refire)
		taskq_dispatch_delay(system_delay_taskq, zpl_posix_acl_free,
		    NULL, TQ_SLEEP, new_time);

	while (freelist) {
		a = freelist;
		freelist = a->next;
		kfree(a->acl);
		kmem_free(a, sizeof (struct acl_rel_struct));
	}
}

void
zpl_posix_acl_release_impl(struct posix_acl *acl)
{
	struct acl_rel_struct *a, **prev;

	a = kmem_alloc(sizeof (struct acl_rel_struct), KM_SLEEP);
	a->next = NULL;
	a->acl = acl;
	a->time = ddi_get_lbolt();
	/* atomically points tail to us and get the previous tail */
	prev = xchg(&acl_rel_tail, &a->next);
	ASSERT3P(*prev, ==, NULL);
	*prev = a;
	/* if it was empty before, schedule the free task */
	if (prev == &acl_rel_head)
		taskq_dispatch_delay(system_delay_taskq, zpl_posix_acl_free,
		    NULL, TQ_SLEEP, ddi_get_lbolt() + ACL_REL_SCHED);
}
#endif
