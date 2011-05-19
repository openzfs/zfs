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
 * different than what is supported on Linux.
 *
 * Under Linux extended attributes are manipulated by the system
 * calls getxattr(2), setxattr(2), and listxattr(2).  They consider
 * extended attributes to be name/value pairs where the name is a
 * NULL terminated string.  The name must also include one of the
 * following name space prefixes:
 *
 *   user     - No restrictions and is available to user applications.
 *   trusted  - Restricted to kernel and root (CAP_SYS_ADMIN) use.
 *   system   - Used for access control lists (system.nfs4_acl, etc).
 *   security - Used by SELinux to store a files security context.
 *
 * This Linux interface is implemented internally using the more
 * flexible Solaris style extended attributes.  Every extended
 * attribute is store as a file in a hidden directory associated
 * with the parent file.  This ensures on disk compatibility with
 * zfs implementations on other platforms (Solaris, FreeBSD, MacOS).
 *
 * One consequence of this implementation is that when an extended
 * attribute is manipulated an inode is created.  This inode will
 * exist in the Linux inode cache but there will be no associated
 * entry in the dentry cache which references it.  This is safe
 * but it may result in some confusion.
 *
 * Longer term I would like to see the 'security.selinux' extended
 * attribute moved to a SA.  This should significantly improve
 * performance on a SELinux enabled system by minimizing the
 * number of seeks required to access a file.  However, for now
 * this xattr is still stored in a file because I'm pretty sure
 * adding a new SA will break on-disk compatibility.
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

ssize_t
zpl_xattr_list(struct dentry *dentry, char *buffer, size_t buffer_size)
{
	struct inode *ip = dentry->d_inode;
	struct inode *dxip = NULL;
	loff_t pos = 3;  /* skip '.', '..', and '.zfs' entries. */
	cred_t *cr = CRED();
	int error;
	xattr_filldir_t xf = { buffer_size, 0, buffer, ip };

	crhold(cr);

	/* Lookup the xattr directory */
	error = -zfs_lookup(ip, NULL, &dxip, LOOKUP_XATTR, cr, NULL, NULL);
	if (error) {
		if (error == -ENOENT)
			error = 0;

		goto out;
	}

	/* Fill provided buffer via zpl_zattr_filldir helper */
	error = -zfs_readdir(dxip, (void *)&xf, zpl_xattr_filldir, &pos, cr);
	if (error)
		goto out;

	error = xf.offset;
out:
	if (dxip)
		iput(dxip);

	crfree(cr);

	return (error);
}

static int
zpl_xattr_get(struct inode *ip, const char *name, void *buf, size_t size)
{
	struct inode *dxip = NULL;
	struct inode *xip = NULL;
	cred_t *cr = CRED();
	int error;

	crhold(cr);

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

	error = zpl_read_common(xip, buf, size, 0, UIO_SYSSPACE, 0, cr);
out:
	if (xip)
		iput(xip);

	if (dxip)
		iput(dxip);

	crfree(cr);

	if (error == -ENOENT)
		error = -ENODATA;

	return (error);
}

static int
zpl_xattr_set(struct inode *ip, const char *name, const void *value,
    size_t size, int flags)
{
	struct inode *dxip = NULL;
	struct inode *xip = NULL;
	vattr_t *vap = NULL;
	cred_t *cr = CRED();
	ssize_t wrote;
	int error;
	const int xattr_mode = S_IFREG | 0644;

	crhold(cr);

	/* Lookup the xattr directory and create it if required. */
	error = -zfs_lookup(ip, NULL, &dxip, LOOKUP_XATTR | CREATE_XATTR_DIR,
	    cr, NULL, NULL);
	if (error)
		goto out;

	/*
	 * Lookup a specific xattr name in the directory, two failure modes:
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REMOVE: fail if xattr does not exist
	 */
	error = -zfs_lookup(dxip, (char *)name, &xip, 0, cr, NULL, NULL);
	if (error) {
		if (error != -ENOENT)
			goto out;

		if ((error == -ENOENT) && (flags & XATTR_REPLACE))
			goto out;
	} else {
		error = -EEXIST;
		if (flags & XATTR_CREATE)
			goto out;
	}
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

	crfree(cr);
	if (error == -ENOENT)
		error = -ENODATA;

	ASSERT3S(error, <=, 0);

	return (error);
}

static int
__zpl_xattr_user_get(struct inode *ip, const char *name,
    void *buffer, size_t size)
{
	char *xattr_name;
	int error;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	if (!(ITOZSB(ip)->z_flags & ZSB_XATTR))
		return -EOPNOTSUPP;

	xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, buffer, size);
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
    void *buffer, size_t size)
{
	char *xattr_name;
	int error;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	xattr_name = kmem_asprintf("%s%s", XATTR_TRUSTED_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, buffer, size);
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
    void *buffer, size_t size)
{
	char *xattr_name;
	int error;

	if (strcmp(name, "") == 0)
		return -EINVAL;

	xattr_name = kmem_asprintf("%s%s", XATTR_SECURITY_PREFIX, name);
	error = zpl_xattr_get(ip, xattr_name, buffer, size);
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
};
