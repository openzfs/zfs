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
#include <sys/vfs.h>
#include <sys/zpl.h>

typedef struct xattr_filldir {
	size_t size;
	size_t offset;
	char *buf;
	struct inode *inode;
} xattr_filldir_t;

static int
zpl_xattr_filldir(void *arg, const char *name, int name_len,
    loff_t offset, uint64_t objnum, unsigned int d_type)
{
	xattr_filldir_t *xf = arg;

	if (!strncmp(name, XATTR_USER_PREFIX, XATTR_USER_PREFIX_LEN))
		if (!(ITOZSB(xf->inode)->z_flags & ZSB_XATTR))
			return (0);

	if (!strncmp(name, XATTR_TRUSTED_PREFIX, XATTR_TRUSTED_PREFIX_LEN))
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

static ssize_t
zpl_xattr_list_dir(xattr_filldir_t *xf, cred_t *cr)
{
	struct inode *ip = xf->inode;
	struct inode *dxip = NULL;
	loff_t pos = 3;  /* skip '.', '..', and '.zfs' entries. */
	int error;

	/* Lookup the xattr directory */
	error = -zfs_lookup(ip, NULL, &dxip, LOOKUP_XATTR, cr, NULL, NULL);
	if (error) {
		if (error == -ENOENT)
			error = 0;

		return (error);
	}

	/* Fill provided buffer via zpl_zattr_filldir helper */
	error = -zfs_readdir(dxip, (void *)xf, zpl_xattr_filldir, &pos, cr);
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

		error = zpl_xattr_filldir((void *)xf, nvpair_name(nvp),
		     strlen(nvpair_name(nvp)), 0, 0, 0);
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
	int error = 0;

	crhold(cr);
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
	crfree(cr);

	return (error);
}

static int
zpl_xattr_get_dir(struct inode *ip, const char *name, void *value,
    size_t size, cred_t *cr)
{
	struct inode *dxip = NULL;
	struct inode *xip = NULL;
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

	error = zpl_read_common(xip, value, size, 0, UIO_SYSSPACE, 0, cr);
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
		if (error >= 0)
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
	int error;

	crhold(cr);
	rw_enter(&zp->z_xattr_lock, RW_READER);
	error = __zpl_xattr_get(ip, name, value, size, cr);
	rw_exit(&zp->z_xattr_lock);
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
	int error;
	const int xattr_mode = S_IFREG | 0644;

	/* Lookup the xattr directory and create it if required. */
	error = -zfs_lookup(ip, NULL, &dxip, LOOKUP_XATTR | CREATE_XATTR_DIR,
	    cr, NULL, NULL);
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
		vap = kmem_zalloc(sizeof(vattr_t), KM_SLEEP);
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

	wrote = zpl_write_common(xip, value, size, 0, UIO_SYSSPACE, 0, cr);
	if (wrote < 0)
		error = wrote;

out:
	if (vap)
		kmem_free(vap, sizeof(vattr_t));

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
	int error;

	crhold(cr);
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

		if ((error == -ENODATA) && (flags & XATTR_REPLACE))
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
		return -EINVAL;

	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return -EOPNOTSUPP;

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
		return -EINVAL;

	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return -EOPNOTSUPP;

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
		return -EACCES;

	if (strcmp(name, "") == 0)
		return -EINVAL;

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
		return -EACCES;

	if (strcmp(name, "") == 0)
		return -EINVAL;

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
		return -EINVAL;

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
		return -EINVAL;

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
			return 0;
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

xattr_handler_t *zpl_xattr_handlers[] = {
	&zpl_xattr_security_handler,
	&zpl_xattr_trusted_handler,
	&zpl_xattr_user_handler,
#ifdef HAVE_POSIX_ACLS
	&zpl_xattr_acl_access_handler,
	&zpl_xattr_acl_default_handler,
#endif /* HAVE_POSIX_ACLS */
	NULL
};
