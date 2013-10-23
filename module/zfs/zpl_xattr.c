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
		/* Do not allow SA xattrs in symlinks (issue #1648) */
		if (S_ISLNK(ip->i_mode))
			return (-EMLINK);

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
    size_t size, int flags,boolean_t prefer_sa_anyway)
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
	if (zsb->z_use_sa && (zsb->z_xattr_sa || prefer_sa_anyway) && zp->z_is_sa) {
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
	error = zpl_xattr_set(ip, xattr_name, value, size, flags,FALSE);
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
	error = zpl_xattr_set(ip, xattr_name, value, size, flags, FALSE);
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
	error = zpl_xattr_set(ip, xattr_name, value, size, flags, FALSE);
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


/* FUNCTIONS SIMILAR TO ONES INLINED IN KERNEL HEADERS THAT CANNOT BE USED */

static void
real_pacl_kfree(void* arg) {
    kfree(arg);
}

static void
crippled_posix_acl_release(struct posix_acl* to_be_released) {
    if(to_be_released == NULL) {
        return;
    }
    if(to_be_released == ACL_NOT_CACHED) {
        return;
    }
    if(atomic_dec_and_test(&to_be_released->a_refcount)) {
	//This cannot be free'd immediatly, should wait the next RCU
	//synchronization.  Since we cannot use RCU, wait 10 seconds and hope
	//for the best.
	//It's an ugly workaround to the fact that posix_acl_release is an
	//inline function that uses a GPL-only symbol (kfree_rcu).
        taskq_dispatch_delay(system_taskq,real_pacl_kfree,
		to_be_released,TQ_SLEEP,ddi_get_lbolt() + 120*HZ);
    }
}

static void
crippled_set_cached_acl(struct inode* inode,int type,struct posix_acl* newer) {
    struct posix_acl* older = NULL;
    spin_lock(&inode->i_lock);
    if((newer != ACL_NOT_CACHED) && (newer != NULL)) posix_acl_dup(newer);
    switch(type) {
    case ACL_TYPE_ACCESS:
        older = inode->i_acl;
        rcu_assign_pointer(inode->i_acl,newer);
        break;
    case ACL_TYPE_DEFAULT:
        older = inode->i_default_acl;
        rcu_assign_pointer(inode->i_default_acl,newer);
        break;
    }
    spin_unlock(&inode->i_lock);
    crippled_posix_acl_release(older);
}

static inline void
crippled_forget_cached_acl(struct inode* inode, int type) {
    crippled_set_cached_acl(inode,type,(struct posix_acl*)ACL_NOT_CACHED);
}



/* END SIMILAR FUNCTIONS */

/* If someone wants to change WHERE the Posix ACL "blob" is stored in ZFS (e.g.
 * storing directly as System Attribute without using xattrs) the next two
* functions are only ones impacted by the change. */

static inline int
write_pacl_to_zfs(struct inode* inode, int type,
				     void* buffer, int buflen) {
    char* name = (type==ACL_TYPE_DEFAULT)?POSIX_ACL_XATTR_DEFAULT:
    					  POSIX_ACL_XATTR_ACCESS;
    /* Prefers writing the xattr to the SA with the same logic as if xattr=sa,
     * whatever property the user has set.
     * Posix ACLs are Linux-only and if can't be seen from other platforms,
     * i'ts even better, as they are not going to understand/use them.
     * Using SAs reduces significantly the performance impact of Posix ACLs in
     * ZFS.
     */
    //When the possible SA bug is fixed/explained swap thow two lines.
    //return zpl_xattr_set(inode,name,buffer,buflen,0,TRUE);
    return zpl_xattr_set(inode,name,buffer,buflen,0,FALSE);
}
static inline int
read_pacl_from_zfs(struct inode* inode, int type, void** buffer) {
    char* name = (type==ACL_TYPE_DEFAULT)?POSIX_ACL_XATTR_DEFAULT:
    					  POSIX_ACL_XATTR_ACCESS;
    int size = 0;
    size = zpl_xattr_get(inode,name,NULL,0);
    if(size <= 0) {
        return size;
    }
    *buffer = kmalloc(size,GFP_NOFS);
    size = zpl_xattr_get(inode,name,*buffer,size);
    if(size <= 0) {
        kfree(*buffer);
    }
    return size;
}

static int
zpl_set_mode(struct inode *inode, mode_t mode)
{
    struct iattr *att;
    struct dentry *fake_de = NULL;
    int err = -ENOMEM;

    att = kzalloc(sizeof(struct iattr), GFP_KERNEL);
    if(att == NULL)
        goto out;

    fake_de = kzalloc(sizeof(struct dentry), GFP_KERNEL);
    if(fake_de == NULL)
        goto out;

    fake_de->d_inode = inode;
    att->ia_valid    = ATTR_MODE;
    att->ia_mode     = mode;
    //What is the preferred way to call this function, since it's static?
    //Remove "static" keyword in the other file?
    err = zpl_inode_operations.setattr(fake_de, att);
out:
    if (att)
        kfree(att);
    if (fake_de)
        kfree(fake_de);
    return err;

}

static int
zpl_set_posix_acl(struct inode *inode,struct posix_acl *acl, int type)
{
    int err = 0;
    size_t size=0;

    char *xattr_name = NULL;
    char * value = NULL;

    if (S_ISLNK(inode->i_mode))
        return -EOPNOTSUPP;

    switch(type) {
    case ACL_TYPE_ACCESS:
        xattr_name = POSIX_ACL_XATTR_ACCESS;
        if (acl) {
            umode_t mode = inode->i_mode;
            err = posix_acl_equiv_mode(acl, &mode);
            if (err < 0)
                return err;
            else {
                if (inode->i_mode != mode) {
                    int rc;
                    rc = zpl_set_mode(inode,mode);
                    if (rc)
                        return rc;

                }
                if (err == 0) {
                    /* extended attribute not needed*/
                    acl = NULL;
                }
            }
        }
        break;

    case ACL_TYPE_DEFAULT:
        xattr_name = POSIX_ACL_XATTR_DEFAULT;
        if (!S_ISDIR(inode->i_mode))
            return acl ? -EACCES : 0;
        break;

    default:
        return -EINVAL;
    }

    if (acl) {
        size = posix_acl_xattr_size(acl->a_count);
        value = kmalloc(size, GFP_KERNEL);
        if (IS_ERR(value))
            return (int)PTR_ERR(value);

	err = acl_to_xattr(acl,value,size);
	//This writes ACLs with uids relative to the current usernamespace, and
	//not relative to the initial namespace. init_user_ns is a GPL-only
	//symbol, I cannot do otherwise.
	if (err < 0)
            goto out;
    }

    err = write_pacl_to_zfs(inode, type, value, size);

    if(err==-ENOENT && !acl) {
	err=0;   //returns -ENOENT if asked to remove a non-existent xattr,
		 //which is fine in this case.
    }
    if (!err) {
        if(acl) {
            crippled_set_cached_acl(inode, type, acl);
        }
        else {
	    //if this was a removal request, forget the cached acl!!
            crippled_forget_cached_acl(inode,type);
        }
    }

out:
    if (value)
        kfree(value);
    return err;

}


struct posix_acl *
zpl_xattr_acl_get_acl(struct inode *inode, int type)
{
    struct posix_acl *acl = NULL;
    int size;
    void *value = NULL;

    acl = get_cached_acl(inode, type);
    if (acl != ACL_NOT_CACHED)
        return acl;

    size = read_pacl_from_zfs(inode, type, &value);

    if (size > 0) {
        acl=acl_from_xattr(value,size);
	kfree(value);

    }
    else if (size == -ENODATA || size == -ENOSYS)
        acl = NULL;
    else
        acl = ERR_PTR(size);

    if (acl != NULL && !IS_ERR(acl))
        crippled_set_cached_acl(inode, type, acl);

    return acl;
}

int
zpl_xattr_acl_init(struct inode *inode, struct inode *dir)
{
    struct posix_acl *acl = NULL;
    int error = 0;

    if (!S_ISLNK(inode->i_mode)) {
        acl = zpl_xattr_acl_get_acl(dir, ACL_TYPE_DEFAULT);
        if (IS_ERR(acl))
            return PTR_ERR(acl);
        if (!acl) {
            inode->i_mode &= ~current_umask();
            error = zpl_set_mode(inode,inode->i_mode);
            if (error)
                goto cleanup;
        }
    }

    if (acl) {
        umode_t mode;

        if (S_ISDIR(inode->i_mode)) {
            error = zpl_set_posix_acl(inode, acl, ACL_TYPE_DEFAULT);
            if (error < 0)
                goto cleanup;
        }
        mode = inode->i_mode;
        error = posix_acl_create(&acl,GFP_NOFS, &mode);
        if (error >= 0) {
            int err;
            inode->i_mode = mode;
            err = zpl_set_mode(inode, mode);
            if (error > 0) {
                /* This is an extended ACL */
                error = zpl_set_posix_acl(inode, acl, ACL_TYPE_ACCESS);

            }
            error |= err;
        }
    }
cleanup:
    crippled_posix_acl_release(acl);
    return error;
}

int
zpl_xattr_acl_chmod(struct inode *inode)
{
    struct posix_acl *acl;
    int error;

    if (S_ISLNK(inode->i_mode))
        return -EOPNOTSUPP;

    acl = zpl_xattr_acl_get_acl(inode, ACL_TYPE_ACCESS);
    if (IS_ERR(acl) || !acl)
        return PTR_ERR(acl);
    error = posix_acl_chmod(&acl,GFP_NOFS, inode->i_mode);
    if (!error)
        error = zpl_set_posix_acl(inode, acl, ACL_TYPE_ACCESS);
    crippled_posix_acl_release(acl);
    return error;

}

static inline size_t
zpl_xattr_acl_list_access(struct dentry *dentry, char *list,
                          size_t list_size,const char *name, size_t name_len,int type)
{
    const size_t total_len = sizeof(POSIX_ACL_XATTR_ACCESS);

    if(ITOZSB(dentry->d_inode)->z_acltype != ZFS_ACLTYPE_POSIXACL) return 0;

    if (list && total_len <= list_size) {
        memcpy(list, POSIX_ACL_XATTR_ACCESS, total_len);
        return total_len;
    }
    else
        return -EINVAL;

}

static inline size_t
zpl_xattr_acl_list_default(struct dentry *dentry, char *list, size_t list_size,
                           const char *name, size_t name_len,int type)
{
    const size_t total_len = sizeof(POSIX_ACL_XATTR_DEFAULT);

    if(ITOZSB(dentry->d_inode)->z_acltype != ZFS_ACLTYPE_POSIXACL) return 0;

    if (list && total_len <= list_size) {
        memcpy(list, POSIX_ACL_XATTR_DEFAULT,total_len);
        return total_len;
    }
    else
        return -EINVAL;
}


static int
zpl_xattr_acl_get(struct dentry *dentry, const char *name, void *buffer,
                  size_t size, int type)
{
    struct posix_acl *acl;
    int error;
    
    if (strcmp(name, "") != 0)
        return -EINVAL;

    if(ITOZSB(dentry->d_inode)->z_acltype != ZFS_ACLTYPE_POSIXACL) return -EOPNOTSUPP;

    acl = zpl_xattr_acl_get_acl(dentry->d_inode, type);
    if (acl == NULL)
        return -ENODATA;
    if (IS_ERR(acl))
        return PTR_ERR(acl);

    error = acl_to_xattr(acl, buffer, size); 
    //This writes ACLs with uids relative to the current usernamespace, and not
    //relative to the initial namespace.  init_user_ns is a GPL-only symbol, I
    //cannot do otherwise.
    crippled_posix_acl_release(acl);
    return error;
}

static int
zpl_xattr_acl_set(struct dentry *dentry, const char *name, const void *value,
                  size_t size, int flags, int type)
{
    struct posix_acl * acl = NULL;
    int err = 0;

    if ((strcmp(name, "") != 0) &&
            ((type != ACL_TYPE_ACCESS) && (type != ACL_TYPE_DEFAULT)))
        return -EINVAL;
    
    if(ITOZSB(dentry->d_inode)->z_acltype != ZFS_ACLTYPE_POSIXACL) return -EOPNOTSUPP;

    if (!inode_owner_or_capable(dentry->d_inode))
        return -EPERM;

    if (value) {
        acl = acl_from_xattr(value, size);
	if (IS_ERR(acl))
            return PTR_ERR(acl);
        else if (acl) {
            err = posix_acl_valid(acl);
            if (err)
                goto exit;
        }
    }

    err = zpl_set_posix_acl(dentry->d_inode, acl, type);
exit:
    crippled_posix_acl_release(acl);
    return err;
}

struct xattr_handler zpl_xattr_acl_access_handler =
{
    .prefix = POSIX_ACL_XATTR_ACCESS,
    .list   = zpl_xattr_acl_list_access,
    .get    = zpl_xattr_acl_get,
    .set    = zpl_xattr_acl_set,
    .flags  = ACL_TYPE_ACCESS,
};


struct xattr_handler zpl_xattr_acl_default_handler =
{
    .prefix = POSIX_ACL_XATTR_DEFAULT,
    .list   = zpl_xattr_acl_list_default,
    .get    = zpl_xattr_acl_get,
    .set    = zpl_xattr_acl_set,
    .flags  = ACL_TYPE_DEFAULT,
};

xattr_handler_t *zpl_xattr_handlers[] = {
	&zpl_xattr_security_handler,
	&zpl_xattr_trusted_handler,
	&zpl_xattr_user_handler,
	&zpl_xattr_acl_access_handler,
	&zpl_xattr_acl_default_handler,
	NULL
};
