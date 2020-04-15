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
/*
 * Large Block Coment about differences on XNU goes here, etc.
 */

#include <sys/types.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/zap.h>
#include <sys/vfs.h>
#include <sys/zpl.h>
#include <sys/xattr.h>

#define	XATTR_USER_PREFIX		"macos:"
#define	XATTR_USER_PREFIX_LEN	strlen("macos:")

enum xattr_permission {
	XAPERM_DENY,
	XAPERM_ALLOW,
	XAPERM_COMPAT,
};

static unsigned int zfs_xattr_compat = 0;

static enum xattr_permission
zpl_xattr_permission(struct vnode *dvp, zfs_uio_t *uio, const char *name,
    int name_len)
{
	if (xattr_protected(name))
		return (XAPERM_DENY);
	return (zfs_xattr_compat ? XAPERM_COMPAT : XAPERM_ALLOW);
}

/*
 * Determine is a given xattr name should be visible and if so copy it
 * in to the provided buffer (xf->buf).
 */
static int
zpl_xattr_filldir(struct vnode *dvp, zfs_uio_t *uio, const char *name,
    int name_len)
{
	enum xattr_permission perm;

	/* Check permissions using the per-namespace list xattr handler. */
	perm = zpl_xattr_permission(dvp, uio, name, name_len);
	if (perm == XAPERM_DENY)
		return (0);

	/* If it starts with "macos:", skip past it. */
	if (perm != XAPERM_COMPAT) {
		if (name_len >= XATTR_USER_PREFIX_LEN &&
		    strncmp(XATTR_USER_PREFIX, name,
		    XATTR_USER_PREFIX_LEN) == 0) {
			name += XATTR_USER_PREFIX_LEN;
			name_len -= XATTR_USER_PREFIX_LEN;
		}
	}

	/* When resid is 0 only calculate the required size. */
	if (uio == NULL || zfs_uio_resid(uio) == 0) {
		zfs_uio_setoffset(uio, zfs_uio_offset(uio) +
		    name_len + 1);
		return (0);
	}

	if (name_len + 1 > zfs_uio_resid(uio))
		return (ERANGE);

	zfs_uiomove((void *)name, name_len + 1, UIO_READ, uio);

	return (0);
}

/*
 * Read as many directory entry names as will fit in to the provided buffer,
 * or when no buffer is provided calculate the required buffer size.
 */
static int
zpl_xattr_readdir(struct vnode *dxip, struct vnode *dvp, zfs_uio_t *uio)
{
	zap_cursor_t zc;
	zap_attribute_t	zap;
	int error;

	zap_cursor_init(&zc, ITOZSB(dxip)->z_os, ITOZ(dxip)->z_id);

	while ((error = zap_cursor_retrieve(&zc, &zap)) == 0) {

		if (zap.za_integer_length != 8 || zap.za_num_integers != 1) {
			error = ENXIO;
			break;
		}

		error = zpl_xattr_filldir(dvp, uio,
		    zap.za_name, strlen(zap.za_name));

		if (error)
			break;

		zap_cursor_advance(&zc);
	}

	zap_cursor_fini(&zc);

	if (error == ENOENT)
		error = 0;

	return (error);
}

static ssize_t
zpl_xattr_list_dir(struct vnode *dvp, zfs_uio_t *uio, cred_t *cr)
{
	struct vnode *dxip = NULL;
	znode_t *dxzp;
	int error;

	/* Lookup the xattr directory */
	error = zfs_lookup(ITOZ(dvp), NULL, &dxzp, LOOKUP_XATTR,
	    cr, NULL, NULL);
	if (error) {
		if (error == ENOENT)
			error = 0;
		return (error);
	}

	dxip = ZTOI(dxzp);
	error = zpl_xattr_readdir(dxip, dvp, uio);
	VN_RELE(dxip);

	return (error);
}

static ssize_t
zpl_xattr_list_sa(struct vnode *dvp, zfs_uio_t *uio)
{
	znode_t *zp = ITOZ(dvp);
	nvpair_t *nvp = NULL;
	int error = 0;

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);

	while ((nvp = nvlist_next_nvpair(zp->z_xattr_cached, nvp)) != NULL) {
		ASSERT3U(nvpair_type(nvp), ==, DATA_TYPE_BYTE_ARRAY);

		error = zpl_xattr_filldir(dvp, uio, nvpair_name(nvp),
		    strlen(nvpair_name(nvp)));
		if (error)
			return (error);
	}

	return (0);
}

int
zpl_xattr_list(struct vnode *dvp, zfs_uio_t *uio, ssize_t *size, cred_t *cr)
{
	znode_t *zp = ITOZ(dvp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error = 0;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);
	rw_enter(&zp->z_xattr_lock, RW_READER);

	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_list_sa(dvp, uio);
		if (error)
			goto out;
	}

	error = zpl_xattr_list_dir(dvp, uio, cr);
	if (error)
		goto out;

	if (size)
		*size = zfs_uio_offset(uio);

out:

	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);

	return (error);
}

static int
zpl_xattr_get_dir(struct vnode *ip, const char *name, zfs_uio_t *uio,
    ssize_t *size, cred_t *cr)
{
	znode_t *dxzp = NULL;
	znode_t *xzp = NULL;
	int error;

	/* Lookup the xattr directory */
	error = zfs_lookup(ITOZ(ip), NULL, &dxzp, LOOKUP_XATTR,
	    cr, NULL, NULL);
	if (error)
		goto out;

	if (size)
		*size = 0; // NA

	/* Lookup a specific xattr name in the directory */
	error = zfs_lookup(dxzp, (char *)name, &xzp, 0, cr, NULL, NULL);
	if (error)
		goto out;

	if (size)
		*size = xzp->z_size;

	if (uio == NULL || zfs_uio_resid(uio) == 0)
		goto out;

	if (zfs_uio_resid(uio) < xzp->z_size) {
		error = ERANGE;
		goto out;
	}

	error = zfs_read(xzp, uio, 0, cr);

	if (size)
		*size = xzp->z_size - zfs_uio_resid(uio);

out:
	if (xzp)
		zrele(xzp);

	if (dxzp)
		zrele(dxzp);

	return (error);
}

static int
zpl_xattr_get_sa(struct vnode *ip, const char *name, zfs_uio_t *uio,
    ssize_t *size)
{
	znode_t *zp = ITOZ(ip);
	uchar_t *nv_value;
	uint_t nv_size;
	int error = 0;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	if (size)
		*size = 0; // NA

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	ASSERT(zp->z_xattr_cached);
	error = nvlist_lookup_byte_array(zp->z_xattr_cached, name,
	    &nv_value, &nv_size);
	if (error)
		return (error);

	if (size)
		*size = nv_size;

	if (uio == NULL || zfs_uio_resid(uio) == 0)
		return (0);

	if (zfs_uio_resid(uio) < nv_size)
		return (ERANGE);

	zfs_uiomove(nv_value, nv_size, UIO_READ, uio);

	if (size)
		*size = nv_size - zfs_uio_resid(uio);

	return (0);
}

static int
__zpl_xattr_get(struct vnode *ip, const char *name, zfs_uio_t *uio,
    ssize_t *retsize, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_get_sa(ip, name, uio, retsize);
		if (error != ENOENT)
			goto out;
	}

	error = zpl_xattr_get_dir(ip, name, uio, retsize, cr);

out:
	if (error == ENOENT)
		error = ENOATTR;

	return (error);
}

#define	XATTR_NOENT	0x0
#define	XATTR_IN_SA	0x1
#define	XATTR_IN_DIR	0x2
/* check where the xattr resides */
static int
__zpl_xattr_where(struct vnode *ip, const char *name, int *where, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;
	ssize_t retsize;

	ASSERT(where);
	ASSERT(RW_LOCK_HELD(&zp->z_xattr_lock));

	*where = XATTR_NOENT;
	if (zfsvfs->z_use_sa && zp->z_is_sa) {
		error = zpl_xattr_get_sa(ip, name, NULL, &retsize);
		if (error == 0)
			*where |= XATTR_IN_SA;
		else if (error != ENOENT)
			return (error);
	}

	error = zpl_xattr_get_dir(ip, name, NULL, &retsize, cr);
	if (error == 0)
		*where |= XATTR_IN_DIR;
	else if (error != ENOENT)
		return (error);

	if (*where == (XATTR_IN_SA|XATTR_IN_DIR))
		cmn_err(CE_WARN, "ZFS: inode %p has xattr \"%s\""
		    " in both SA and dir", ip, name);
	if (*where == XATTR_NOENT)
		error = ENOATTR;
	else
		error = 0;

	return (error);
}

int
zpl_xattr_get(struct vnode *ip, const char *name, zfs_uio_t *uio,
    ssize_t *retsize, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	rw_enter(&zp->z_xattr_lock, RW_READER);
	/*
	 * Try to look up the name with the namespace prefix first for
	 * compatibility with xattrs from this platform.  If that fails,
	 * try again without the namespace prefix for compatibility with
	 * other platforms.
	 */
	char *xattr_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	error = __zpl_xattr_get(ip, xattr_name, uio, retsize, cr);
	kmem_strfree(xattr_name);

	if (error == ENOATTR)
		error = __zpl_xattr_get(ip, name, uio, retsize, cr);

	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);

	return (error);
}

static int
zpl_xattr_set_dir(struct vnode *ip, const char *name, zfs_uio_t *uio,
    int flags, cred_t *cr)
{
	znode_t *dxzp = NULL;
	znode_t *xzp = NULL;
	int lookup_flags, error;
	const int xattr_mode = S_IFREG | 0644;

	/*
	 * Lookup the xattr directory.  When we're adding an entry pass
	 * CREATE_XATTR_DIR to ensure the xattr directory is created.
	 * When removing an entry this flag is not passed to avoid
	 * unnecessarily creating a new xattr directory.
	 */
	lookup_flags = LOOKUP_XATTR;
	if (uio != NULL && zfs_uio_resid(uio) != 0)
		lookup_flags |= CREATE_XATTR_DIR;

	error = zfs_lookup(ITOZ(ip), NULL, &dxzp, lookup_flags,
	    cr, NULL, NULL);
	if (error)
		goto out;

	/* Lookup a specific xattr name in the directory */
	error = zfs_lookup(dxzp, (char *)name, &xzp, 0, cr, NULL, NULL);

	if (error && (error != ENOENT))
		goto out;

	error = 0;

	/* Remove a specific name xattr when value is set to NULL. */
	if (uio == NULL || zfs_uio_resid(uio) == 0) {
		if (xzp)
			error = zfs_remove(dxzp, (char *)name, cr, 0);

		goto out;
	}

	/* Lookup failed create a new xattr. */
	if (xzp == NULL) {
		struct vnode_attr  vattr;
		/* Set va_size and 0 - skip zfs_freesp below? */
		VATTR_INIT(&vattr);
		VATTR_SET(&vattr, va_type, VREG);
		VATTR_SET(&vattr, va_mode, xattr_mode);
		VATTR_SET(&vattr, va_uid, crgetfsuid(cr));
		VATTR_SET(&vattr, va_gid, crgetfsgid(cr));
		error = zfs_create(dxzp, (char *)name, &vattr, 0, 0644, &xzp,
		    cr, 0, NULL, NULL);
		if (error)
			goto out;
	}

	ASSERT(xzp != NULL);

	error = zfs_freesp(xzp, 0, 0, xattr_mode, TRUE);
	if (error)
		goto out;

	error = zfs_write(xzp, uio, 0, cr);

out:
	if (error == 0) {
		/*
		 * ip->z_ctime = current_time(ip);
		 * zfs_mark_inode_dirty(ip);
		 */
	}

	if (xzp)
		zrele(xzp);

	if (dxzp)
		zrele(dxzp);

	if (error == ENOENT)
		error = ENOATTR;

	return (error);
}

static int
zpl_xattr_set_sa(struct vnode *ip, const char *name, zfs_uio_t *uio,
    int flags, cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	nvlist_t *nvl;
	size_t sa_size;
	int error = 0;
	void *buf = NULL;
	int len = 0;
	size_t used;
	int allocated = 0;

	mutex_enter(&zp->z_lock);
	if (zp->z_xattr_cached == NULL)
		error = zfs_sa_get_xattr(zp);
	mutex_exit(&zp->z_lock);

	if (error)
		return (error);

	/*
	 * We have to be careful not to "consume" the uio,
	 * in the error cases, as it is to be used next in
	 * xattr=dir.
	 * This only supports "one iovec" for the data
	 */
	if (uio != NULL) {
		buf = zfs_uio_iovbase(uio, 0);
		len = zfs_uio_iovlen(uio, 0);
	}

	ASSERT(zp->z_xattr_cached);
	nvl = zp->z_xattr_cached;

	if (uio == NULL || zfs_uio_resid(uio) == 0) {
		error = nvlist_remove(nvl, name, DATA_TYPE_BYTE_ARRAY);
		if (error == ENOENT)
			error = zpl_xattr_set_dir(ip, name, NULL, flags, cr);
	} else {
		/* Limited to 32k to keep nvpair memory allocations small */
		if (zfs_uio_resid(uio) > DXATTR_MAX_ENTRY_SIZE)
			return (EFBIG);

		/* Prevent the DXATTR SA from consuming the entire SA region */
		error = nvlist_size(nvl, &sa_size, NV_ENCODE_XDR);
		if (error)
			return (error);

		if (sa_size > DXATTR_MAX_SA_SIZE)
			return (EFBIG);

		/*
		 * Allocate memory to copyin, which is a shame as nvlist
		 * will also allocate memory to hold it. Could consider a
		 * nvlist_add_byte_array_uio() so the memcpy(KM_SLEEP);
		 * zfs_uiomove(buf, ) uses uiomove()
		 * instead.
		 */
		if (zfs_uio_segflg(uio) != UIO_SYSSPACE) {
			allocated = 1;
			buf = kmem_alloc(len, KM_SLEEP);
			/* Don't consume uio yet; uiocopy, not uiomove */
			zfs_uiocopy(buf, len, UIO_WRITE, uio, &used);
		}

		error = nvlist_add_byte_array(nvl, name,
		    (uchar_t *)buf, len);

		/* Free this after zfs_sa_set_xattr() */

	}

	/*
	 * Update the SA for additions, modifications, and removals. On
	 * error drop the inconsistent cached version of the nvlist, it
	 * will be reconstructed from the ARC when next accessed.
	 */
	if (error == 0) {
		error = zfs_sa_set_xattr(zp, name, buf, len);
	}

	if (allocated == 1)
		kmem_free(buf, len);

	if (error) {
		nvlist_free(nvl);
		zp->z_xattr_cached = NULL;
	} else if (uio != NULL) {
		/* Finally consume uio */
		zfs_uio_advance(uio, len);
	}

	return (error);
}

static int
_zpl_xattr_set(struct vnode *ip, const char *name, zfs_uio_t *uio, int flags,
    cred_t *cr)
{
	znode_t *zp = ITOZ(ip);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	int where;
	int error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		goto out1;
	rw_enter(&zp->z_xattr_lock, RW_WRITER);

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
	if (error != 0) {
		if (error != ENOATTR)
			goto out;
		if (flags & XATTR_REPLACE)
			goto out;

		/* The xattr to be removed already doesn't exist */
		error = 0;
	} else {
		error = EEXIST;
		if (flags & XATTR_CREATE)
			goto out;
	}

	/* Preferentially store the xattr as a SA for better performance */
	if (zfsvfs->z_use_sa && zp->z_is_sa &&
	    (zfsvfs->z_xattr_sa ||
	    (uio == NULL && where & XATTR_IN_SA))) {
		error = zpl_xattr_set_sa(ip, name, uio, flags, cr);
		if (error == 0) {
			/*
			 * Successfully put into SA, we need to clear the one
			 * in dir.
			 */
			if (where & XATTR_IN_DIR)
				zpl_xattr_set_dir(ip, name, NULL, 0, cr);
			goto out;
		}
	}

	error = zpl_xattr_set_dir(ip, name, uio, flags, cr);

	/*
	 * Successfully put into dir, we need to clear the one in SA.
	 */
	if (error == 0 && (where & XATTR_IN_SA))
		zpl_xattr_set_sa(ip, name, NULL, 0, cr);
out:
	rw_exit(&zp->z_xattr_lock);
	zfs_exit(zfsvfs, FTAG);
out1:

	return (error);
}

int
zpl_xattr_set(struct vnode *ip, const char *name, zfs_uio_t *uio, int flags,
    cred_t *cr)
{
	int error;

	/*
	 * Remove alternate compat version of the xattr so we only set the
	 * version specified by the zfs_xattr_compat tunable.
	 *
	 * The following flags must be handled correctly:
	 *
	 *   XATTR_CREATE: fail if xattr already exists
	 *   XATTR_REPLACE: fail if xattr does not exist
	 */

	char *prefixed_name = kmem_asprintf("%s%s", XATTR_USER_PREFIX, name);
	const char *clear_name, *set_name;
	if (zfs_xattr_compat) {
		clear_name = prefixed_name;
		set_name = name;
	} else {
		clear_name = name;
		set_name = prefixed_name;
	}

	/*
	 * Clear the old value with the alternative name format, if it exists.
	 */
	error = _zpl_xattr_set(ip, clear_name, NULL, flags, cr);

	/*
	 * XATTR_CREATE was specified and we failed to clear the xattr
	 * because it already exists.  Stop here.
	 */
	if (error == EEXIST)
		goto out;

	/*
	 * If XATTR_REPLACE was specified and we succeeded to clear
	 * an xattr, we don't need to replace anything when setting
	 * the new value.  If we failed with -ENODATA that's fine,
	 * there was nothing to be cleared and we can ignore the error.
	 */
	if (error == 0)
		flags &= ~XATTR_REPLACE;
	/*
	 * Set the new value with the configured name format.
	 */
	error = _zpl_xattr_set(ip, set_name, uio, flags, cr);
out:
	kmem_strfree(prefixed_name);
	return (error);
}


ZFS_MODULE_PARAM(zfs, zfs_, xattr_compat, UINT, ZMOD_RW,
	"Use legacy ZFS xattr naming for writing new user namespace xattrs");
