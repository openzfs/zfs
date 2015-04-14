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
 * The Linux xattr implemenation has been written to take advantage of
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

#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/vfs.h>
#include <sys/zpl.h>

typedef struct xattr_filldir {
	size_t size;
	size_t offset;
	char *buf;
	struct inode *inode;
} xattr_filldir_t;

static int
zpl_xattr_filldir(xattr_filldir_t *xf, const char *name, int name_len)
{
	if (strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN) == 0)
		if (!(ITOZSB(xf->inode)->z_flags & ZSB_XATTR))
			return (0);

	if (strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN) == 0)
		if (!capable(CAP_SYS_ADMIN))
			return (0);

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
int
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
	struct inode *ip = xf->inode;
	struct inode *dxip = NULL;
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ip, NULL, &dxip, LOOKUP_XATTR, cr, NULL, NULL);
	if (error) {
		if (error == -ENOENT)
			error = 0;

		return (error);
	}

	error = zpl_xattr_readdir(dxip, xf);
	iput(dxip);

	return (error);
}

static ssize_t
zpl_xattr_list_sa(xattr_filldir_t *xf)
{
	znode_t *zp = ITOZ(xf->inode);
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
	zfs_sb_t *zsb = ZTOZSB(zp);
	xattr_filldir_t xf = { buffer_size, 0, buffer, dentry->d_inode };
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error = 0;

	crhold(cr);
	cookie = spl_fstrans_mark();
	rw_enter(&zp->z_xattr_lock, RW_READER);

	if (zsb->z_use_sa && zp->z_is_sa) {
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
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static int
zpl_xattr_get_dir(struct inode *ip, const char *name, void *value,
    size_t size, cred_t *cr)
{
	struct inode *dxip = NULL;
	struct inode *xip = NULL;
	loff_t pos = 0;
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ip, NULL, &dxip, LOOKUP_XATTR, cr, NULL, NULL);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = -zfs_lookup(dxip, (char *)name, &xip, 0, cr, NULL, NULL);
	if (error)
		goto out;

	if (!size) {
		error = i_size_read(xip);
		goto out;
	}

	if (size < i_size_read(xip)) {
		error = -ERANGE;
		goto out;
	}

	error = zpl_read_common(xip, value, size, &pos, UIO_SYSSPACE, 0, cr);
out:
	if (xip)
		iput(xip);

	if (dxip)
		iput(dxip);

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

	if (!size)
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
	zfs_sb_t *zsb = ZTOZSB(zp);
	int error;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	if (zsb->z_use_sa && zp->z_is_sa) {
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

static int
zpl_xattr_get(struct inode *ip, const char *name, void *value, size_t size)
{
	znode_t *zp = ITOZ(ip);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	rw_enter(&zp->z_xattr_lock, RW_READER);
	error = __zpl_xattr_get(ip, name, value, size, cr);
	rw_exit(&zp->z_xattr_lock);
	spl_fstrans_unmark(cookie);
	crfree(cr);

	return (error);
}

static int
zpl_xattr_set_dir(struct inode *ip, const char *name, const void *value,
    size_t size, int flags, cred_t *cr)
{
	struct inode *dxip = NULL;
	struct inode *xip = NULL;
	vattr_t *vap = NULL;
	ssize_t wrote;
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

	error = -zfs_lookup(ip, NULL, &dxip, lookup_flags, cr, NULL, NULL);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = -zfs_lookup(dxip, (char *)name, &xip, 0, cr, NULL, NULL);
	if (error && (error != -ENOENT))
		goto out;

	error = 0;

	/* Remove a specific name xattr when value is set to NULL. */
	if (value == NULL) {
		if (xip)
			error = -zfs_remove(dxip, (char *)name, cr);

		goto out;
	}

	/* Lookup failed create a new xattr. */
	if (xip == NULL) {
		vap = kmem_zalloc(sizeof (vattr_t), KM_SLEEP);
		vap->va_mode = xattr_mode;
		vap->va_mask = ATTR_MODE;
		vap->va_uid = crgetfsuid(cr);
		vap->va_gid = crgetfsgid(cr);

		error = -zfs_create(dxip, (char *)name, vap, 0, 0644, &xip,
		    cr, 0, NULL);
		if (error)
			goto out;
	}

	ASSERT(xip != NULL);

	error = -zfs_freesp(ITOZ(xip), 0, 0, xattr_mode, TRUE);
	if (error)
		goto out;

	wrote = zpl_write_common(xip, value, size, &pos, UIO_SYSSPACE, 0, cr);
	if (wrote < 0)
		error = wrote;

out:
	if (vap)
		kmem_free(vap, sizeof (vattr_t));

	if (xip)
		iput(xip);

	if (dxip)
		iput(dxip);

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
	int error;

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
		if (error)
			return (error);
	}

	/* Update the SA for additions, modifications, and removals. */
	if (!error)
		error = -zfs_sa_set_xattr(zp);

	ASSERT3S(error, <=, 0);

	return (error);
}

static int
zpl_xattr_set(struct inode *ip, const char *name, const void *value,
    size_t size, int flags)
{
	znode_t *zp = ITOZ(ip);
	zfs_sb_t *zsb = ZTOZSB(zp);
	cred_t *cr = CRED();
	fstrans_cookie_t cookie;
	int error;

	crhold(cr);
	cookie = spl_fstrans_mark();
	rw_enter(&ITOZ(ip)->z_xattr_lock, RW_WRITER);

	/*
	 * Before setting the xattr check to see if it already exists.
	 * This is done to ensure the following optional flags are honored.
	 *
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REPLACE: fail if xattr does not exist
	 */
	error = __zpl_xattr_get(ip, name, NULL, 0, cr);
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
	if (zsb->z_use_sa && zsb->z_xattr_sa && zp->z_is_sa) {
		error = zpl_xattr_set_sa(ip, name, value, size, flags, cr);
		if (error == 0)
			goto out;
	}

	error = zpl_xattr_set_dir(ip, name, value, size, flags, cr);
out:
	rw_exit(&ITOZ(ip)->z_xattr_lock);
	spl_fstrans_unmark(cookie);
	crfree(cr);
	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_xattr_user_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;

	if (strcmp(name, "") == 0)
		return (-EINVAL);

	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return (-EOPNOTSUPP);

	xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	strfree(xattr_name);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_user_get);

static int
__zpl_xattr_user_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	int error;

	if (strcmp(name, "") == 0)
		return (-EINVAL);

	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return (-EOPNOTSUPP);

	xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags);
	strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_user_set);

xattr_handler_t zpl_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.get	= zpl_xattr_user_get,
	.set	= zpl_xattr_user_set,
};

static int
__zpl_xattr_trusted_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return (-EACCES);

	if (strcmp(name, "") == 0)
		return (-EINVAL);

	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	strfree(xattr_name);

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

	if (strcmp(name, "") == 0)
		return (-EINVAL);

	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags);
	strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_trusted_set);

xattr_handler_t zpl_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.get	= zpl_xattr_trusted_get,
	.set	= zpl_xattr_trusted_set,
};

static int
__zpl_xattr_security_get(struct inode *ip, const char *name,
    void *value, size_t size)
{
	char *xattr_name;
	int error;

	if (strcmp(name, "") == 0)
		return (-EINVAL);

	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, value, size);
	strfree(xattr_name);

	return (error);
}
ZPL_XATTR_GET_WRAPPER(zpl_xattr_security_get);

static int
__zpl_xattr_security_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	char *xattr_name;
	int error;

	if (strcmp(name, "") == 0)
		return (-EINVAL);

	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_set(ip, xattr_name, value, size, flags);
	strfree(xattr_name);

	return (error);
}
ZPL_XATTR_SET_WRAPPER(zpl_xattr_security_set);

#ifdef HAVE_CALLBACK_SECURITY_INODE_INIT_SECURITY
static int
__zpl_xattr_security_init(struct inode *ip, const struct xattr *xattrs,
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
	    &__zpl_xattr_security_init, NULL);
}

#else
int
zpl_xattr_security_init(struct inode *ip, struct inode *dip,
    const struct qstr *qstr)
{
	int error;
	size_t len;
	void *value;
	char *name;

	error = zpl_security_inode_init_security(ip, dip, qstr,
	    &name, &value, &len);
	if (error) {
		if (error == -EOPNOTSUPP)
			return (0);

		return (error);
	}

	error = __zpl_xattr_security_set(ip, name, value, len, 0);

	kfree(name);
	kfree(value);

	return (error);
}
#endif /* HAVE_CALLBACK_SECURITY_INODE_INIT_SECURITY */

xattr_handler_t zpl_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.get	= zpl_xattr_security_get,
	.set	= zpl_xattr_security_set,
};

#ifdef CONFIG_FS_POSIX_ACL

int
zpl_set_acl(struct inode *ip, int type, struct posix_acl *acl)
{
	struct super_block *sb = ITOZSB(ip)->z_sb;
	char *name, *value = NULL;
	int error = 0;
	size_t size = 0;

	if (S_ISLNK(ip->i_mode))
		return (-EOPNOTSUPP);

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		if (acl) {
			zpl_equivmode_t mode = ip->i_mode;
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
					ip->i_ctime = current_fs_time(sb);
					zfs_mark_inode_dirty(ip);
				}

				if (error == 0)
					acl = NULL;
			}
		}
		break;

	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
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

struct posix_acl *
zpl_get_acl(struct inode *ip, int type)
{
	struct posix_acl *acl;
	void *value = NULL;
	char *name;
	int size;

#ifdef HAVE_POSIX_ACL_CACHING
	acl = get_cached_acl(ip, type);
	if (acl != ACL_NOT_CACHED)
		return (acl);
#endif /* HAVE_POSIX_ACL_CACHING */

	switch (type) {
	case ACL_TYPE_ACCESS:
		name = POSIX_ACL_XATTR_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		name = POSIX_ACL_XATTR_DEFAULT;
		break;
	default:
		return (ERR_PTR(-EINVAL));
	}

	size = zpl_xattr_get(ip, name, NULL, 0);
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

	if (!IS_ERR(acl))
		zpl_set_cached_acl(ip, type, acl);

	return (acl);
}

#if !defined(HAVE_GET_ACL)
static int
__zpl_check_acl(struct inode *ip, int mask)
{
	struct posix_acl *acl;
	int error;

	acl = zpl_get_acl(ip, ACL_TYPE_ACCESS);
	if (IS_ERR(acl))
		return (PTR_ERR(acl));

	if (acl) {
		error = posix_acl_permission(ip, acl, mask);
		zpl_posix_acl_release(acl);
		return (error);
	}

	return (-EAGAIN);
}

#if defined(HAVE_CHECK_ACL_WITH_FLAGS)
int
zpl_check_acl(struct inode *ip, int mask, unsigned int flags)
{
	return (__zpl_check_acl(ip, mask));
}
#elif defined(HAVE_CHECK_ACL)
int
zpl_check_acl(struct inode *ip, int mask)
{
	return (__zpl_check_acl(ip, mask));
}
#elif defined(HAVE_PERMISSION_WITH_NAMEIDATA)
int
zpl_permission(struct inode *ip, int mask, struct nameidata *nd)
{
	return (generic_permission(ip, mask, __zpl_check_acl));
}
#elif defined(HAVE_PERMISSION)
int
zpl_permission(struct inode *ip, int mask)
{
	return (generic_permission(ip, mask, __zpl_check_acl));
}
#endif /* HAVE_CHECK_ACL | HAVE_PERMISSION */
#endif /* !HAVE_GET_ACL */

int
zpl_init_acl(struct inode *ip, struct inode *dir)
{
	struct posix_acl *acl = NULL;
	int error = 0;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIXACL)
		return (0);

	if (!S_ISLNK(ip->i_mode)) {
		if (ITOZSB(ip)->z_acl_type == ZFS_ACLTYPE_POSIXACL) {
			acl = zpl_get_acl(dir, ACL_TYPE_DEFAULT);
			if (IS_ERR(acl))
				return (PTR_ERR(acl));
		}

		if (!acl) {
			ip->i_mode &= ~current_umask();
			ip->i_ctime = current_fs_time(ITOZSB(ip)->z_sb);
			zfs_mark_inode_dirty(ip);
			return (0);
		}
	}

	if ((ITOZSB(ip)->z_acl_type == ZFS_ACLTYPE_POSIXACL) && acl) {
		umode_t mode;

		if (S_ISDIR(ip->i_mode)) {
			error = zpl_set_acl(ip, ACL_TYPE_DEFAULT, acl);
			if (error)
				goto out;
		}

		mode = ip->i_mode;
		error = __posix_acl_create(&acl, GFP_KERNEL, &mode);
		if (error >= 0) {
			ip->i_mode = mode;
			zfs_mark_inode_dirty(ip);
			if (error > 0)
				error = zpl_set_acl(ip, ACL_TYPE_ACCESS, acl);
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

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIXACL)
		return (0);

	if (S_ISLNK(ip->i_mode))
		return (-EOPNOTSUPP);

	acl = zpl_get_acl(ip, ACL_TYPE_ACCESS);
	if (IS_ERR(acl) || !acl)
		return (PTR_ERR(acl));

	error = __posix_acl_chmod(&acl, GFP_KERNEL, ip->i_mode);
	if (!error)
		error = zpl_set_acl(ip, ACL_TYPE_ACCESS, acl);

	zpl_posix_acl_release(acl);

	return (error);
}

static size_t
zpl_xattr_acl_list(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len, int type)
{
	char *xattr_name;
	size_t xattr_size;

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIXACL)
		return (0);

	switch (type) {
	case ACL_TYPE_ACCESS:
		xattr_name = POSIX_ACL_XATTR_ACCESS;
		xattr_size = sizeof (xattr_name);
		break;
	case ACL_TYPE_DEFAULT:
		xattr_name = POSIX_ACL_XATTR_DEFAULT;
		xattr_size = sizeof (xattr_name);
		break;
	default:
		return (0);
	}

	if (list && xattr_size <= list_size)
		memcpy(list, xattr_name, xattr_size);

	return (xattr_size);
}

#ifdef HAVE_DENTRY_XATTR_LIST
static size_t
zpl_xattr_acl_list_access(struct dentry *dentry, char *list,
    size_t list_size, const char *name, size_t name_len, int type)
{
	ASSERT3S(type, ==, ACL_TYPE_ACCESS);
	return zpl_xattr_acl_list(dentry->d_inode,
	    list, list_size, name, name_len, type);
}

static size_t
zpl_xattr_acl_list_default(struct dentry *dentry, char *list,
    size_t list_size, const char *name, size_t name_len, int type)
{
	ASSERT3S(type, ==, ACL_TYPE_DEFAULT);
	return zpl_xattr_acl_list(dentry->d_inode,
	    list, list_size, name, name_len, type);
}

#else

static size_t
zpl_xattr_acl_list_access(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return zpl_xattr_acl_list(ip,
	    list, list_size, name, name_len, ACL_TYPE_ACCESS);
}

static size_t
zpl_xattr_acl_list_default(struct inode *ip, char *list, size_t list_size,
    const char *name, size_t name_len)
{
	return zpl_xattr_acl_list(ip,
	    list, list_size, name, name_len, ACL_TYPE_DEFAULT);
}
#endif /* HAVE_DENTRY_XATTR_LIST */

static int
zpl_xattr_acl_get(struct inode *ip, const char *name,
    void *buffer, size_t size, int type)
{
	struct posix_acl *acl;
	int error;

	if (strcmp(name, "") != 0)
		return (-EINVAL);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIXACL)
		return (-EOPNOTSUPP);

	acl = zpl_get_acl(ip, type);
	if (IS_ERR(acl))
		return (PTR_ERR(acl));
	if (acl == NULL)
		return (-ENODATA);

	error = zpl_acl_to_xattr(acl, buffer, size);
	zpl_posix_acl_release(acl);

	return (error);
}

#ifdef HAVE_DENTRY_XATTR_GET
static int
zpl_xattr_acl_get_access(struct dentry *dentry, const char *name,
    void *buffer, size_t size, int type)
{
	ASSERT3S(type, ==, ACL_TYPE_ACCESS);
	return (zpl_xattr_acl_get(dentry->d_inode, name, buffer, size, type));
}

static int
zpl_xattr_acl_get_default(struct dentry *dentry, const char *name,
    void *buffer, size_t size, int type)
{
	ASSERT3S(type, ==, ACL_TYPE_DEFAULT);
	return (zpl_xattr_acl_get(dentry->d_inode, name, buffer, size, type));
}

#else

static int
zpl_xattr_acl_get_access(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	return (zpl_xattr_acl_get(ip, name, buffer, size, ACL_TYPE_ACCESS));
}

static int
zpl_xattr_acl_get_default(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	return (zpl_xattr_acl_get(ip, name, buffer, size, ACL_TYPE_DEFAULT));
}
#endif /* HAVE_DENTRY_XATTR_GET */

static int
zpl_xattr_acl_set(struct inode *ip, const char *name,
    const void *value, size_t size, int flags, int type)
{
	struct posix_acl *acl;
	int error = 0;

	if (strcmp(name, "") != 0)
		return (-EINVAL);

	if (ITOZSB(ip)->z_acl_type != ZFS_ACLTYPE_POSIXACL)
		return (-EOPNOTSUPP);

	if (!zpl_inode_owner_or_capable(ip))
		return (-EPERM);

	if (value) {
		acl = zpl_acl_from_xattr(value, size);
		if (IS_ERR(acl))
			return (PTR_ERR(acl));
		else if (acl) {
			error = posix_acl_valid(acl);
			if (error) {
				zpl_posix_acl_release(acl);
				return (error);
			}
		}
	} else {
		acl = NULL;
	}

	error = zpl_set_acl(ip, type, acl);
	zpl_posix_acl_release(acl);

	return (error);
}

#ifdef HAVE_DENTRY_XATTR_SET
static int
zpl_xattr_acl_set_access(struct dentry *dentry, const char *name,
    const void *value, size_t size, int flags, int type)
{
	ASSERT3S(type, ==, ACL_TYPE_ACCESS);
	return (zpl_xattr_acl_set(dentry->d_inode,
	    name, value, size, flags, type));
}

static int
zpl_xattr_acl_set_default(struct dentry *dentry, const char *name,
    const void *value, size_t size, int flags, int type)
{
	ASSERT3S(type, ==, ACL_TYPE_DEFAULT);
	return zpl_xattr_acl_set(dentry->d_inode,
	    name, value, size, flags, type);
}

#else

static int
zpl_xattr_acl_set_access(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	return zpl_xattr_acl_set(ip,
	    name, value, size, flags, ACL_TYPE_ACCESS);
}

static int
zpl_xattr_acl_set_default(struct inode *ip, const char *name,
    const void *value, size_t size, int flags)
{
	return zpl_xattr_acl_set(ip,
	    name, value, size, flags, ACL_TYPE_DEFAULT);
}
#endif /* HAVE_DENTRY_XATTR_SET */

struct xattr_handler zpl_xattr_acl_access_handler =
{
	.prefix	= POSIX_ACL_XATTR_ACCESS,
	.list	= zpl_xattr_acl_list_access,
	.get	= zpl_xattr_acl_get_access,
	.set	= zpl_xattr_acl_set_access,
#ifdef HAVE_DENTRY_XATTR_LIST
	.flags	= ACL_TYPE_ACCESS,
#endif /* HAVE_DENTRY_XATTR_LIST */
};

struct xattr_handler zpl_xattr_acl_default_handler =
{
	.prefix	= POSIX_ACL_XATTR_DEFAULT,
	.list	= zpl_xattr_acl_list_default,
	.get	= zpl_xattr_acl_get_default,
	.set	= zpl_xattr_acl_set_default,
#ifdef HAVE_DENTRY_XATTR_LIST
	.flags	= ACL_TYPE_DEFAULT,
#endif /* HAVE_DENTRY_XATTR_LIST */
};

#endif /* CONFIG_FS_POSIX_ACL */

xattr_handler_t *zpl_xattr_handlers[] = {
	&zpl_xattr_security_handler,
	&zpl_xattr_trusted_handler,
	&zpl_xattr_user_handler,
#ifdef CONFIG_FS_POSIX_ACL
	&zpl_xattr_acl_access_handler,
	&zpl_xattr_acl_default_handler,
#endif /* CONFIG_FS_POSIX_ACL */
	NULL
};
