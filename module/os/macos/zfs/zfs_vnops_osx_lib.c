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
 * Copyright (c) 2013 Will Andrews <will@firepipe.net>
 * Copyright (c) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 */
#include <sys/cred.h>
#include <sys/vnode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/spa.h>
#include <sys/txg.h>
#include <sys/dbuf.h>
#include <sys/zap.h>
#include <sys/sa.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>

#include <sys/unistd.h>
#include <sys/xattr.h>
#include <sys/uuid.h>
#include <sys/utfconv.h>
#include <sys/finderinfo.h>
#include <libkern/crypto/md5.h>

extern int zfs_vnop_force_formd_normalized_output; /* disabled by default */

static uint32_t zfs_hardlink_sequence = 1ULL<<31;

/*
 * Unfortunately Apple defines "KAUTH_VNODE_ACCESS (1<<31)" which
 * generates: "warning: signed shift result (0x80000000) sets the
 * sign bit of the shift expression's type ('int') and becomes negative."
 * So until they fix their define, we override it here.
 */

#if KAUTH_VNODE_ACCESS == 0x80000000
#undef KAUTH_VNODE_ACCESS
#define	KAUTH_VNODE_ACCESS (1ULL<<31)
#endif



int zfs_hardlink_addmap(znode_t *zp, uint64_t parentid, uint32_t linkid);

/* Originally from illumos:uts/common/sys/vfs.h */
typedef uint64_t vfs_feature_t;
#define	VFSFT_XVATTR		0x100000001	/* Supports xvattr for attrs */
#define	VFSFT_CASEINSENSITIVE	0x100000002	/* Supports case-insensitive */
#define	VFSFT_NOCASESENSITIVE	0x100000004	/* NOT case-sensitive */
#define	VFSFT_DIRENTFLAGS	0x100000008	/* Supports dirent flags */
#define	VFSFT_ACLONCREATE	0x100000010	/* Supports ACL on create */
#define	VFSFT_ACEMASKONACCESS	0x100000020	/* Can use ACEMASK for access */
#define	VFSFT_SYSATTR_VIEWS	0x100000040	/* Supports sysattr view i/f */
#define	VFSFT_ACCESS_FILTER	0x100000080	/* dirents filtered by access */
#define	VFSFT_REPARSE		0x100000100	/* Supports reparse point */
#define	VFSFT_ZEROCOPY_SUPPORTED 0x100000200	/* Supports loaning buffers */

/*
 * fnv_32a_str - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a string
 *
 * input:
 *	str	- string to hash
 *	hval	- previous hash value or 0 if first call
 *
 * returns:
 *	32 bit hash as a static hash type
 *
 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
 *   hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
 */
uint32_t
fnv_32a_str(const char *str, uint32_t hval)
{
	unsigned char *s = (unsigned char *)str;	/* unsigned string */

	/*
	 * FNV-1a hash each octet in the buffer
	 */
	while (*s) {

		/* xor the bottom with the current octet */
		hval ^= (uint32_t)*s++;

		/* multiply by the 32 bit FNV magic prime mod 2^32 */
#if defined(NO_FNV_GCC_OPTIMIZATION)
		hval *= FNV_32_PRIME;
#else
		hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) +
		    (hval<<24);
#endif
	}

	/* return our new hash value */
	return (hval);
}

/*
 * fnv_32a_buf - perform a 32 bit Fowler/Noll/Vo FNV-1a hash on a buffer
 *
 * input:
 * buf- start of buffer to hash
 * len- length of buffer in octets
 * hval- previous hash value or 0 if first call
 *
 * returns:
 * 32 bit hash as a static hash type
 *
 * NOTE: To use the recommended 32 bit FNV-1a hash, use FNV1_32A_INIT as the
 * hval arg on the first call to either fnv_32a_buf() or fnv_32a_str().
 */
uint32_t
fnv_32a_buf(void *buf, size_t len, uint32_t hval)
{
	unsigned char *bp = (unsigned char *)buf; /* start of buffer */
	unsigned char *be = bp + len; /* beyond end of buffer */

	/*
	 * FNV-1a hash each octet in the buffer
	 */
	while (bp < be) {

		/* xor the bottom with the current octet */
		hval ^= (uint32_t)*bp++;

		/* multiply by the 32 bit FNV magic prime mod 2^32 */
#if defined(NO_FNV_GCC_OPTIMIZATION)
		hval *= FNV_32_PRIME;
#else
		hval += (hval<<1) + (hval<<4) + (hval<<7) + (hval<<8) +
		    (hval<<24);
#endif
	}

	/* return our new hash value */
	return (hval);
}

int
zfs_getattr_znode_unlocked(struct vnode *vp, vattr_t *vap)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error = 0;
	uint64_t	parent;
	sa_bulk_attr_t bulk[4];
	int count = 0;
#ifdef VNODE_ATTR_va_addedtime
	uint64_t addtime[2] = { 0 };
#endif
	int ishardlink = 0;

	// printf("getattr_osx\n");

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	// If wanted, return NULL guids
	if (VATTR_IS_ACTIVE(vap, va_uuuid))
		VATTR_RETURN(vap, va_uuuid, kauth_null_guid);
	if (VATTR_IS_ACTIVE(vap, va_guuid))
		VATTR_RETURN(vap, va_guuid, kauth_null_guid);

#if 0 // Issue #192
	if (VATTR_IS_ACTIVE(vap, va_uuuid)) {
		kauth_cred_uid2guid(zp->z_uid, &vap->va_uuuid);
		VATTR_RETURN(vap, va_uuuid, vap->va_uuuid);
	}
	if (VATTR_IS_ACTIVE(vap, va_guuid)) {
		kauth_cred_gid2guid(zp->z_gid, &vap->va_guuid);
		VATTR_RETURN(vap, va_guuid, vap->va_guuid);
	}
#endif

	// But if we are to check acl, can fill in guids
	if (VATTR_IS_ACTIVE(vap, va_acl)) {
		// dprintf("Calling getacl\n");
		if ((error = zfs_getacl(zp, &vap->va_acl, B_FALSE, NULL))) {
			// dprintf("zfs_getacl returned error %d\n", error);
			error = 0;
		} else {
			VATTR_SET_SUPPORTED(vap, va_acl);
		}

	}

	mutex_enter(&zp->z_lock);

	ishardlink = ((zp->z_links > 1) &&
	    (IFTOVT((mode_t)zp->z_mode) == VREG)) ? 1 : 0;
	if (zp->z_finder_hardlink == TRUE)
		ishardlink = 1;
	else if (ishardlink)
		zp->z_finder_hardlink = TRUE;

	/* Work out which SA we need to fetch */

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL, &parent, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);

	/*
	 * Unfortunately, sa_bulk_lookup does not let you handle optional
	 * SA entries - so have to look up the optionals individually.
	 */
	error = sa_bulk_lookup(zp->z_sa_hdl, bulk, count);
	if (error) {
		dprintf("ZFS: Warning: getattr failed sa_bulk_lookup: %d, "
		    "parent %llu, flags %llu\n", error, parent, zp->z_pflags);
		mutex_exit(&zp->z_lock);
		zfs_exit(zfsvfs, FTAG);
		return (0);
	}

	/*
	 * On Mac OS X we always export the root directory id as 2
	 */
	vap->va_fileid = INO_ZFSTOXNU(zp->z_id, zfsvfs->z_root);

	vap->va_data_size = zp->z_size;
	vap->va_total_size = zp->z_size;
	if (zp->z_gen == 0)
		zp->z_gen = 1;
	vap->va_gen = zp->z_gen;

#if defined(DEBUG) || defined(ZFS_DEBUG)
	if (zp->z_gen != 0)
		dprintf("%s: va_gen %lld -> 0\n", __func__, zp->z_gen);
#endif

	vap->va_nlink = zp->z_links;

	/*
	 * Carbon compatibility, pretend to support this legacy attribute
	 */
	if (VATTR_IS_ACTIVE(vap, va_backup_time)) {
		vap->va_backup_time.tv_sec = 0;
		vap->va_backup_time.tv_nsec = 0;
		VATTR_SET_SUPPORTED(vap, va_backup_time);
	}
	vap->va_flags = zfs_getbsdflags(zp);
	/*
	 * On Mac OS X we always export the root directory id as 2
	 * and its parent as 1
	 */
	if (zp->z_id == zfsvfs->z_root)
		vap->va_parentid = 1;
	else if (parent == zfsvfs->z_root)
		vap->va_parentid = 2;
	else
		vap->va_parentid = INO_ZFSTOXNU(parent, zfsvfs->z_root);

	// Hardlinks: Return cached parentid, make it 2 if root.
	if (ishardlink && zp->z_finder_parentid)
		vap->va_parentid =
		    INO_ZFSTOXNU(zp->z_finder_parentid, zfsvfs->z_root);

	vap->va_iosize = zp->z_blksz ? zp->z_blksz : zfsvfs->z_max_blksz;
	// vap->va_iosize = 512;
	if (VATTR_IS_ACTIVE(vap, va_iosize))
		VATTR_SET_SUPPORTED(vap, va_iosize);

	/* Don't include '.' and '..' in the number of entries */
	if (VATTR_IS_ACTIVE(vap, va_nchildren) && vnode_isdir(vp)) {
		VATTR_RETURN(vap, va_nchildren, vap->va_nlink - 2);
	}

	/*
	 * va_dirlinkcount is the count of directory hard links. When a file
	 * system does not support ATTR_DIR_LINKCOUNT, xnu will default to 1.
	 * Since we claim to support ATTR_DIR_LINKCOUNT both as valid and as
	 * native, we'll just return 1. We set 1 for this value in dirattrpack
	 * as well. If in the future ZFS actually supports directory hard links,
	 * we can return a real value.
	 */
	if (VATTR_IS_ACTIVE(vap, va_dirlinkcount) /* && vnode_isdir(vp) */) {
		VATTR_RETURN(vap, va_dirlinkcount, 1);
	}


	if (VATTR_IS_ACTIVE(vap, va_data_alloc) ||
	    VATTR_IS_ACTIVE(vap, va_total_alloc)) {
		uint32_t  blksize;
		u_longlong_t  nblks;
		sa_object_size(zp->z_sa_hdl, &blksize, &nblks);
		vap->va_data_alloc = (uint64_t)512LL * (uint64_t)nblks;
		vap->va_total_alloc = vap->va_data_alloc;
		vap->va_supported |= VNODE_ATTR_va_data_alloc |
		    VNODE_ATTR_va_total_alloc;
	}

	if (VATTR_IS_ACTIVE(vap, va_name)) {
		vap->va_name[0] = 0;

		if (!vnode_isvroot(vp)) {

			/*
			 * Finder (Carbon) relies on getattr returning the
			 * correct name for hardlinks to work, so we store the
			 * lookup name in vnop_lookup if file references are
			 * high, then set the return name here.
			 * If we also want ATTR_CMN_* lookups to work, we need
			 * to set a unique va_linkid for each entry, and based
			 * on the linkid in the lookup, return the correct name.
			 * It is set in zfs_vnop_lookup().
			 * Since zap_value_search is a slow call, we only use
			 * it if we have not cached the name in vnop_lookup.
			 */

			// Cached name, from vnop_lookup
			if (ishardlink &&
			    zp->z_name_cache[0]) {

				strlcpy(vap->va_name, zp->z_name_cache,
				    MAXPATHLEN);
				VATTR_SET_SUPPORTED(vap, va_name);

			} else if (zp->z_name_cache[0]) {

				strlcpy(vap->va_name, zp->z_name_cache,
				    MAXPATHLEN);
				VATTR_SET_SUPPORTED(vap, va_name);

			} else {

				// Go find the name.
				if (zap_value_search(zfsvfs->z_os, parent,
				    zp->z_id, ZFS_DIRENT_OBJ(-1ULL),
				    vap->va_name) == 0) {
					VATTR_SET_SUPPORTED(vap, va_name);
					// Might as well keep this name too.
					strlcpy(zp->z_name_cache, vap->va_name,
					    MAXPATHLEN);
				} // zap_value_search

			}

			dprintf("getattr: %p return name '%s':%04llx\n", vp,
			    vap->va_name, vap->va_linkid);


		} else {
			/*
			 * The vroot objects must return a unique name for
			 * Finder to be able to distringuish between mounts.
			 * For this reason we simply return the fullname,
			 * from the statfs mountedfrom
			 *
			 * dataset     mountpoint
			 * foo         /bar
			 * As we used to return "foo" to ATTR_CMN_NAME of
			 * "/bar" we change this to return "bar" as expected.
			 */
			char *r, *osname;
			osname = vfs_statfs(zfsvfs->z_vfs)->f_mntonname;
			r = strrchr(osname, '/');
			strlcpy(vap->va_name,
			    r ? &r[1] : osname,
			    MAXPATHLEN);
			VATTR_SET_SUPPORTED(vap, va_name);
			dprintf("getattr root returning '%s'\n", vap->va_name);
		}
	}

	if (VATTR_IS_ACTIVE(vap, va_linkid)) {

		/*
		 * Apple needs a little extra care with HARDLINKs. All hardlink
		 * targets return the same va_fileid (POSIX) but also return
		 * a unique va_linkid. This we generate by hashing the (unique)
		 * name and store as va_linkid. However, Finder will call
		 * vfs_vget() with linkid and expect to receive the correct link
		 * target, so we need to add it to the AVL z_hardlinks.
		 */
		if (ishardlink) {
			hardlinks_t *searchnode, *findnode;
			avl_index_t loc;

			// If we don't have a linkid, make one.
			searchnode = kmem_alloc(sizeof (hardlinks_t), KM_SLEEP);
			searchnode->hl_parent =
			    INO_XNUTOZFS(vap->va_parentid, zfsvfs->z_root);
			searchnode->hl_fileid = zp->z_id;
			strlcpy(searchnode->hl_name, zp->z_name_cache,
			    PATH_MAX);

			rw_enter(&zfsvfs->z_hardlinks_lock, RW_READER);
			findnode = avl_find(&zfsvfs->z_hardlinks, searchnode,
			    &loc);
			rw_exit(&zfsvfs->z_hardlinks_lock);
			kmem_free(searchnode, sizeof (hardlinks_t));

			if (!findnode) {
				uint32_t id;

				id = atomic_inc_32_nv(&zfs_hardlink_sequence);

				zfs_hardlink_addmap(zp, vap->va_parentid, id);
				if (VATTR_IS_ACTIVE(vap, va_linkid))
					VATTR_RETURN(vap, va_linkid, id);

			} else {
				VATTR_RETURN(vap, va_linkid,
				    findnode->hl_linkid);
			}

		} else { // !ishardlink - use same as fileid

			VATTR_RETURN(vap, va_linkid, vap->va_fileid);

		}

	} // active linkid

	if (VATTR_IS_ACTIVE(vap, va_filerev)) {
		VATTR_RETURN(vap, va_filerev, 0);
	}
	if (VATTR_IS_ACTIVE(vap, va_fsid)) {
		VATTR_RETURN(vap, va_fsid, zfsvfs->z_rdev);
	}
	if (VATTR_IS_ACTIVE(vap, va_type)) {
		VATTR_RETURN(vap, va_type, vnode_vtype(ZTOV(zp)));
	}
	if (VATTR_IS_ACTIVE(vap, va_encoding)) {
		VATTR_RETURN(vap, va_encoding, kTextEncodingMacUnicode);
	}
#ifdef VNODE_ATTR_va_addedtime
	/*
	 * ADDEDTIME should come from finderinfo according to hfs_attrlist.c
	 * in ZFS we can use crtime, and add logic to getxattr finderinfo to
	 * copy the ADDEDTIME into the structure. See vnop_getxattr
	 */
	if (VATTR_IS_ACTIVE(vap, va_addedtime)) {
		if (sa_lookup(zp->z_sa_hdl, SA_ZPL_ADDTIME(zfsvfs),
		    &addtime, sizeof (addtime)) != 0) {
			/*
			 * Lookup the ADDTIME if it exists, if not, use CRTIME.
			 * We add CRTIME to WANTED in zfs_vnop_getattr()
			 * so we know we have the value here.
			 */
			vap->va_addedtime.tv_sec  = vap->va_crtime.tv_sec;
			vap->va_addedtime.tv_nsec = vap->va_crtime.tv_nsec;
		} else {
			ZFS_TIME_DECODE(&vap->va_addedtime, addtime);
		}
		VATTR_SET_SUPPORTED(vap, va_addedtime);
	}
#endif
#ifdef VNODE_ATTR_va_fsid64
	if (VATTR_IS_ACTIVE(vap, va_fsid64)) {
		vap->va_fsid64.val[0] =
		    vfs_statfs(zfsvfs->z_vfs)->f_fsid.val[0];
		vap->va_fsid64.val[1] = vfs_typenum(zfsvfs->z_vfs);
		VATTR_SET_SUPPORTED(vap, va_fsid64);
	}
#endif
#ifdef VNODE_ATTR_va_write_gencount
	if (VATTR_IS_ACTIVE(vap, va_write_gencount)) {
		if (!zp->z_write_gencount)
			atomic_inc_64(&zp->z_write_gencount);
		VATTR_RETURN(vap, va_write_gencount,
		    (uint32_t)zp->z_write_gencount);
	}
#endif

#ifdef VNODE_ATTR_va_document_id
	if (VATTR_IS_ACTIVE(vap, va_document_id)) {

		if (!zp->z_document_id) {
			zfs_setattr_generate_id(zp, parent, vap->va_name);
		}

		VATTR_RETURN(vap, va_document_id, zp->z_document_id);
	}
#endif /* VNODE_ATTR_va_document_id */

#ifdef VNODE_ATTR_va_devid
	if (VATTR_IS_ACTIVE(vap, va_devid)) {
		VATTR_RETURN(vap, va_devid,
		    vfs_statfs(zfsvfs->z_vfs)->f_fsid.val[0]);
	}
#endif /* VNODE_ATTR_va_document_id */

	if (ishardlink) {
		dprintf("ZFS:getattr(%s,%llu,%llu) parent %llu: cache_parent "
		    "%llu: va_nlink %llu\n", VATTR_IS_ACTIVE(vap, va_name) ?
		    vap->va_name : zp->z_name_cache,
		    vap->va_fileid,
		    VATTR_IS_ACTIVE(vap, va_linkid) ? vap->va_linkid : 0,
		    vap->va_parentid,
		    zp->z_finder_parentid,
		    vap->va_nlink);
	}

	/* A bunch of vattrs are handled inside zfs_getattr() */
	if (VATTR_IS_ACTIVE(vap, va_mode))
		VATTR_SET_SUPPORTED(vap, va_mode);
	if (VATTR_IS_ACTIVE(vap, va_nlink))
		VATTR_SET_SUPPORTED(vap, va_nlink);
	if (VATTR_IS_ACTIVE(vap, va_uid))
		VATTR_SET_SUPPORTED(vap, va_uid);
	if (VATTR_IS_ACTIVE(vap, va_gid))
		VATTR_SET_SUPPORTED(vap, va_gid);
	if (VATTR_IS_ACTIVE(vap, va_fileid))
		VATTR_SET_SUPPORTED(vap, va_fileid);
	if (VATTR_IS_ACTIVE(vap, va_data_size))
		VATTR_SET_SUPPORTED(vap, va_data_size);
	if (VATTR_IS_ACTIVE(vap, va_total_size))
		VATTR_SET_SUPPORTED(vap, va_total_size);
	if (VATTR_IS_ACTIVE(vap, va_rdev))
		VATTR_SET_SUPPORTED(vap, va_rdev);
	if (VATTR_IS_ACTIVE(vap, va_gen))
		VATTR_SET_SUPPORTED(vap, va_gen);
	if (VATTR_IS_ACTIVE(vap, va_create_time))
		VATTR_SET_SUPPORTED(vap, va_create_time);
	if (VATTR_IS_ACTIVE(vap, va_access_time))
		VATTR_SET_SUPPORTED(vap, va_access_time);
	if (VATTR_IS_ACTIVE(vap, va_modify_time))
		VATTR_SET_SUPPORTED(vap, va_modify_time);
	if (VATTR_IS_ACTIVE(vap, va_change_time))
		VATTR_SET_SUPPORTED(vap, va_change_time);
	if (VATTR_IS_ACTIVE(vap, va_backup_time))
		VATTR_SET_SUPPORTED(vap, va_backup_time);
	if (VATTR_IS_ACTIVE(vap, va_flags))
		VATTR_SET_SUPPORTED(vap, va_flags);
	if (VATTR_IS_ACTIVE(vap, va_parentid))
		VATTR_SET_SUPPORTED(vap, va_parentid);

	uint64_t missing = 0;
	missing = (vap->va_active ^ (vap->va_active & vap->va_supported));
	if (missing != 0) {
		dprintf("vnop_getattr:: asked %08llx replied %08llx "
		    " missing %08llx\n",
		    vap->va_active, vap->va_supported,
		    missing);
	}

	mutex_exit(&zp->z_lock);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

boolean_t
vfs_has_feature(vfs_t *vfsp, vfs_feature_t vfsft)
{

	switch (vfsft) {
	case VFSFT_CASEINSENSITIVE:
	case VFSFT_NOCASESENSITIVE:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

int
zfs_access_native_mode(struct vnode *vp, int *mode, cred_t *cr,
    caller_context_t *ct)
{
	int accmode = *mode & (VREAD|VWRITE|VEXEC /* |VAPPEND */);
	int error = 0;
	int flag = 0; // FIXME

	if (accmode != 0)
		error = zfs_access(VTOZ(vp), accmode, flag, cr);

	*mode &= ~(accmode);

	return (error);
}

int
zfs_ioflags(int ap_ioflag)
{
	int flags = 0;

	if (ap_ioflag & IO_APPEND)
		flags |= FAPPEND;
	if (ap_ioflag & IO_NDELAY)
		flags |= FNONBLOCK;
	if (ap_ioflag & IO_SYNC)
		flags |= (FSYNC | FDSYNC | FRSYNC);

	return (flags);
}

int
zfs_vnop_ioctl_fullfsync(struct vnode *vp, vfs_context_t ct, zfsvfs_t *zfsvfs)
{
	int error;

	error = zfs_fsync(VTOZ(vp), /* syncflag */ 0, NULL);
	if (error)
		return (error);

	if (zfsvfs->z_log != NULL)
		zil_commit(zfsvfs->z_log, 0);
	else
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	return (0);
}

uint32_t
zfs_getbsdflags(znode_t *zp)
{
	uint32_t  bsdflags = 0;
	uint64_t zflags = zp->z_pflags;

	if (zflags & ZFS_NODUMP)
		bsdflags |= UF_NODUMP;
	if (zflags & ZFS_UIMMUTABLE)
		bsdflags |= UF_IMMUTABLE;
	if (zflags & ZFS_UAPPENDONLY)
		bsdflags |= UF_APPEND;
	if (zflags & ZFS_OPAQUE)
		bsdflags |= UF_OPAQUE;
	if (zflags & ZFS_HIDDEN)
		bsdflags |= UF_HIDDEN;
	if (zflags & ZFS_TRACKED)
		bsdflags |= UF_TRACKED;
	if (zflags & ZFS_COMPRESSED)
		bsdflags |= UF_COMPRESSED;

	if (zflags & ZFS_SIMMUTABLE)
		bsdflags |= SF_IMMUTABLE;
	if (zflags & ZFS_SAPPENDONLY)
		bsdflags |= SF_APPEND;
	/*
	 * Due to every file getting archive set automatically, and OSX
	 * don't let you move/copy it as a user, we disable archive connection
	 * for now
	 * if (zflags & ZFS_ARCHIVE)
	 * bsdflags |= SF_ARCHIVED;
	 */
	dprintf("getbsd changing zfs %08llx to osx %08x\n",
	    zflags, bsdflags);
	return (bsdflags);
}

void
zfs_setbsdflags(znode_t *zp, uint32_t bsdflags)
{
	uint64_t zflags;
	VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_FLAGS(zp->z_zfsvfs),
	    &zflags, sizeof (zflags)) == 0);

	if (bsdflags & UF_NODUMP)
		zflags |= ZFS_NODUMP;
	else
		zflags &= ~ZFS_NODUMP;

	if (bsdflags & UF_IMMUTABLE)
		zflags |= ZFS_UIMMUTABLE;
	else
		zflags &= ~ZFS_UIMMUTABLE;

	if (bsdflags & UF_APPEND)
		zflags |= ZFS_UAPPENDONLY;
	else
		zflags &= ~ZFS_UAPPENDONLY;

	if (bsdflags & UF_OPAQUE)
		zflags |= ZFS_OPAQUE;
	else
		zflags &= ~ZFS_OPAQUE;

	if (bsdflags & UF_HIDDEN)
		zflags |= ZFS_HIDDEN;
	else
		zflags &= ~ZFS_HIDDEN;

	if (bsdflags & UF_TRACKED)
		zflags |= ZFS_TRACKED;
	else
		zflags &= ~ZFS_TRACKED;

	if (bsdflags & UF_COMPRESSED)
		zflags |= ZFS_COMPRESSED;
	else
		zflags &= ~ZFS_COMPRESSED;

	/*
	 * if (bsdflags & SF_ARCHIVED)
	 *   zflags |= ZFS_ARCHIVE;
	 * else
	 *   zflags &= ~ZFS_ARCHIVE;
	 */
	if (bsdflags & SF_IMMUTABLE)
		zflags |= ZFS_SIMMUTABLE;
	else
		zflags &= ~ZFS_SIMMUTABLE;

	if (bsdflags & SF_APPEND)
		zflags |= ZFS_SAPPENDONLY;
	else
		zflags &= ~ZFS_SAPPENDONLY;

	zp->z_pflags = zflags;
	dprintf("setbsd changing osx %08x to zfs %08llx\n",
	    bsdflags, zflags);

	/*
	 *  (void )sa_update(zp->z_sa_hdl, SA_ZPL_FLAGS(zp->z_zfsvfs),
	 * (void *)&zp->z_pflags, sizeof (uint64_t), tx);
	 */
}

/*
 * Lookup/Create an extended attribute entry.
 *
 * Input arguments:
 *	dzp	- znode for hidden attribute directory
 *	name	- name of attribute
 *	flag	- ZNEW: if the entry already exists, fail with EEXIST.
 *		  ZEXISTS: if the entry does not exist, fail with ENOENT.
 *
 * Output arguments:
 *	vpp	- pointer to the vnode for the entry (NULL if there isn't one)
 *
 * Return value: 0 on success or errno value on failure.
 */
int
zpl_obtain_xattr(znode_t *dzp, const char *name, mode_t mode, cred_t *cr,
    vnode_t **vpp, int flag)
{
	znode_t  *xzp = NULL;
	zfsvfs_t  *zfsvfs = dzp->z_zfsvfs;
	zilog_t  *zilog;
	zfs_dirlock_t  *dl;
	dmu_tx_t  *tx;
	struct vnode_attr  vattr;
	int error;
	struct componentname cn = { 0 };
	zfs_acl_ids_t	acl_ids;

	/* zfs_dirent_lock() expects a component name */

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);

	zilog = zfsvfs->z_log;

	VATTR_INIT(&vattr);
	VATTR_SET(&vattr, va_type, VREG);
	VATTR_SET(&vattr, va_mode, mode & ~S_IFMT);

	if ((error = zfs_acl_ids_create(dzp, 0,
	    &vattr, cr, NULL, &acl_ids, NULL)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	cn.cn_namelen = strlen(name)+1;
	cn.cn_nameptr = (char *)kmem_zalloc(cn.cn_namelen, KM_SLEEP);

top:
	/* Lock the attribute entry name. */
	if ((error = zfs_dirent_lock(&dl, dzp, (char *)name, &xzp, flag,
	    NULL, &cn))) {
		goto out;
	}
	/* If the name already exists, we're done. */
	if (xzp != NULL) {
		zfs_dirent_unlock(dl);
		goto out;
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, (char *)name);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);

#if 1 // FIXME
	if (dzp->z_pflags & ZFS_INHERIT_ACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, SPA_MAXBLOCKSIZE);
	}
#endif
	zfs_sa_upgrade_txholds(tx, dzp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		dmu_tx_abort(tx);
		goto out;
	}

	zfs_mknode(dzp, &vattr, tx, cr, 0, &xzp, &acl_ids);

	/*
	 * ASSERT(xzp->z_id == zoid);
	 */
	(void) zfs_link_create(dl, xzp, tx, ZNEW);
	zfs_log_create(zilog, tx, TX_CREATE, dzp, xzp, (char *)name,
	    NULL /* vsecp */, 0 /* acl_ids.z_fuidp */, &vattr);
	dmu_tx_commit(tx);

	/*
	 * OS X - attach the vnode _after_ committing the transaction
	 */
	zfs_znode_getvnode(xzp, zfsvfs);

	zfs_dirent_unlock(dl);
out:
	zfs_acl_ids_free(&acl_ids);
	if (cn.cn_nameptr)
		kmem_free(cn.cn_nameptr, cn.cn_namelen);

	/* The REPLACE error if doesn't exist is ENOATTR */
	if ((flag & ZEXISTS) && (error == ENOENT))
		error = ENOATTR;

	if (xzp)
		*vpp = ZTOV(xzp);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

/*
 * ace_trivial:
 * determine whether an ace_t acl is trivial
 *
 * Trivialness implies that the acl is composed of only
 * owner, group, everyone entries.  ACL can't
 * have read_acl denied, and write_owner/write_acl/write_attributes
 * can only be owner@ entry.
 */
int
ace_trivial_common(void *acep, int aclcnt,
    uint64_t (*walk)(void *, uint64_t, int aclcnt,
    uint16_t *, uint16_t *, uint32_t *))
{
	uint16_t flags;
	uint32_t mask;
	uint16_t type;
	uint64_t cookie = 0;

	while ((cookie = walk(acep, cookie, aclcnt, &flags, &type, &mask))) {
		switch (flags & ACE_TYPE_FLAGS) {
			case ACE_OWNER:
			case ACE_GROUP|ACE_IDENTIFIER_GROUP:
			case ACE_EVERYONE:
				break;
			default:
				return (1);

		}

		if (flags & (ACE_FILE_INHERIT_ACE|
		    ACE_DIRECTORY_INHERIT_ACE|ACE_NO_PROPAGATE_INHERIT_ACE|
		    ACE_INHERIT_ONLY_ACE))
			return (1);

		/*
		 * Special check for some special bits
		 *
		 * Don't allow anybody to deny reading basic
		 * attributes or a files ACL.
		 */
		if ((mask & (ACE_READ_ACL|ACE_READ_ATTRIBUTES)) &&
		    (type == ACE_ACCESS_DENIED_ACE_TYPE))
			return (1);

		/*
		 * Delete permission is never set by default
		 */
		if (mask & ACE_DELETE)
			return (1);

		/*
		 * Child delete permission should be accompanied by write
		 */
		if ((mask & ACE_DELETE_CHILD) && !(mask & ACE_WRITE_DATA))
			return (1);
		/*
		 * only allow owner@ to have
		 * write_acl/write_owner/write_attributes/write_xattr/
		 */

		if (type == ACE_ACCESS_ALLOWED_ACE_TYPE &&
		    (!(flags & ACE_OWNER) && (mask &
		    (ACE_WRITE_OWNER|ACE_WRITE_ACL| ACE_WRITE_ATTRIBUTES|
		    ACE_WRITE_NAMED_ATTRS))))
			return (1);

	}

	return (0);
}


void
acl_trivial_access_masks(mode_t mode, boolean_t isdir, trivial_acl_t *masks)
{
	uint32_t read_mask = ACE_READ_DATA;
	uint32_t write_mask = ACE_WRITE_DATA|ACE_APPEND_DATA;
	uint32_t execute_mask = ACE_EXECUTE;

	if (isdir)
		write_mask |= ACE_DELETE_CHILD;

	masks->deny1 = 0;
	if (!(mode & S_IRUSR) && (mode & (S_IRGRP|S_IROTH)))
		masks->deny1 |= read_mask;
	if (!(mode & S_IWUSR) && (mode & (S_IWGRP|S_IWOTH)))
		masks->deny1 |= write_mask;
	if (!(mode & S_IXUSR) && (mode & (S_IXGRP|S_IXOTH)))
		masks->deny1 |= execute_mask;

	masks->deny2 = 0;
	if (!(mode & S_IRGRP) && (mode & S_IROTH))
		masks->deny2 |= read_mask;
	if (!(mode & S_IWGRP) && (mode & S_IWOTH))
		masks->deny2 |= write_mask;
	if (!(mode & S_IXGRP) && (mode & S_IXOTH))
		masks->deny2 |= execute_mask;

	masks->allow0 = 0;
	if ((mode & S_IRUSR) && (!(mode & S_IRGRP) && (mode & S_IROTH)))
		masks->allow0 |= read_mask;
	if ((mode & S_IWUSR) && (!(mode & S_IWGRP) && (mode & S_IWOTH)))
		masks->allow0 |= write_mask;
	if ((mode & S_IXUSR) && (!(mode & S_IXGRP) && (mode & S_IXOTH)))
		masks->allow0 |= execute_mask;

	masks->owner = ACE_WRITE_ATTRIBUTES|ACE_WRITE_OWNER|ACE_WRITE_ACL|
	    ACE_WRITE_NAMED_ATTRS|ACE_READ_ACL|ACE_READ_ATTRIBUTES|
	    ACE_READ_NAMED_ATTRS|ACE_SYNCHRONIZE;
	if (mode & S_IRUSR)
		masks->owner |= read_mask;
	if (mode & S_IWUSR)
		masks->owner |= write_mask;
	if (mode & S_IXUSR)
		masks->owner |= execute_mask;

	masks->group = ACE_READ_ACL|ACE_READ_ATTRIBUTES|ACE_READ_NAMED_ATTRS|
	    ACE_SYNCHRONIZE;
	if (mode & S_IRGRP)
		masks->group |= read_mask;
	if (mode & S_IWGRP)
		masks->group |= write_mask;
	if (mode & S_IXGRP)
		masks->group |= execute_mask;

	masks->everyone = ACE_READ_ACL|ACE_READ_ATTRIBUTES|ACE_READ_NAMED_ATTRS|
	    ACE_SYNCHRONIZE;
	if (mode & S_IROTH)
		masks->everyone |= read_mask;
	if (mode & S_IWOTH)
		masks->everyone |= write_mask;
	if (mode & S_IXOTH)
		masks->everyone |= execute_mask;
}

void commonattrpack(attrinfo_t *aip, zfsvfs_t *zfsvfs, znode_t *zp,
    const char *name, ino64_t objnum, enum vtype vtype,
    boolean_t user64)
{
	attrgroup_t commonattr = aip->ai_attrlist->commonattr;
	void *attrbufptr = *aip->ai_attrbufpp;
	void *varbufptr = *aip->ai_varbufpp;
	struct mount *mp = zfsvfs->z_vfs;
	cred_t  *cr = (cred_t *)vfs_context_ucred(aip->ai_context);
	finderinfo_t finderinfo;

	/*
	 * We should probably combine all the sa_lookup into a bulk
	 * lookup operand.
	 */

	finderinfo.fi_flags = 0;

	if (ATTR_CMN_NAME & commonattr) {
		nameattrpack(aip, name, strlen(name));
		attrbufptr = *aip->ai_attrbufpp;
		varbufptr = *aip->ai_varbufpp;
	}
	if (ATTR_CMN_DEVID & commonattr) {
		*((dev_t *)attrbufptr) = vfs_statfs(mp)->f_fsid.val[0];
		attrbufptr = ((dev_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_FSID & commonattr) {
		*((fsid_t *)attrbufptr) = vfs_statfs(mp)->f_fsid;
		attrbufptr = ((fsid_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_OBJTYPE & commonattr) {
		*((fsobj_type_t *)attrbufptr) = vtype;
		attrbufptr = ((fsobj_type_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_OBJTAG & commonattr) {
		*((fsobj_tag_t *)attrbufptr) = VT_ZFS;
		attrbufptr = ((fsobj_tag_t *)attrbufptr) + 1;
	}
	/*
	 * Note: ATTR_CMN_OBJID is lossy (only 32 bits).
	 */
	if ((ATTR_CMN_OBJID | ATTR_CMN_OBJPERMANENTID) & commonattr) {
		u_int32_t fileid;
		/*
		 * On Mac OS X we always export the root directory id as 2
		 */
		fileid = (objnum == zfsvfs->z_root) ? 2 : objnum;

		if (ATTR_CMN_OBJID & commonattr) {
			((fsobj_id_t *)attrbufptr)->fid_objno = fileid;
			((fsobj_id_t *)attrbufptr)->fid_generation = 0;
			attrbufptr = ((fsobj_id_t *)attrbufptr) + 1;
		}
		if (ATTR_CMN_OBJPERMANENTID & commonattr) {
			((fsobj_id_t *)attrbufptr)->fid_objno = fileid;
			((fsobj_id_t *)attrbufptr)->fid_generation = 0;
			attrbufptr = ((fsobj_id_t *)attrbufptr) + 1;
		}
	}
	/*
	 * Note: ATTR_CMN_PAROBJID is lossy (only 32 bits).
	 */
	if (ATTR_CMN_PAROBJID & commonattr) {
		uint64_t parentid;

		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
		    &parentid, sizeof (parentid)) == 0);

		/*
		 * On Mac OS X we always export the root
		 * directory id as 2 and its parent as 1
		 */
		if (zp && zp->z_id == zfsvfs->z_root)
			parentid = 1;
		else if (parentid == zfsvfs->z_root)
			parentid = 2;

		ASSERT(parentid != 0);

		((fsobj_id_t *)attrbufptr)->fid_objno = (uint32_t)parentid;
		((fsobj_id_t *)attrbufptr)->fid_generation = 0;
		attrbufptr = ((fsobj_id_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_SCRIPT & commonattr) {
		*((text_encoding_t *)attrbufptr) = kTextEncodingMacUnicode;
		attrbufptr = ((text_encoding_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_CRTIME & commonattr) {
		uint64_t times[2];
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_CRTIME(zfsvfs),
		    times, sizeof (times)) == 0);
		if (user64) {
			ZFS_TIME_DECODE((timespec_user64_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user64_t *)attrbufptr) + 1;
		} else {
			ZFS_TIME_DECODE((timespec_user32_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user32_t *)attrbufptr) + 1;
		}
	}
	if (ATTR_CMN_MODTIME & commonattr) {
		uint64_t times[2];
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_MTIME(zfsvfs),
		    times, sizeof (times)) == 0);
		if (user64) {
			ZFS_TIME_DECODE((timespec_user64_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user64_t *)attrbufptr) + 1;
		} else {
			ZFS_TIME_DECODE((timespec_user32_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user32_t *)attrbufptr) + 1;
		}
	}
	if (ATTR_CMN_CHGTIME & commonattr) {
		uint64_t times[2];
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_CTIME(zfsvfs),
		    times, sizeof (times)) == 0);
		if (user64) {
			ZFS_TIME_DECODE((timespec_user64_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user64_t *)attrbufptr) + 1;
		} else {
			ZFS_TIME_DECODE((timespec_user32_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user32_t *)attrbufptr) + 1;
		}
	}
	if (ATTR_CMN_ACCTIME & commonattr) {
		uint64_t times[2];
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_ATIME(zfsvfs),
		    times, sizeof (times)) == 0);
		if (user64) {
			ZFS_TIME_DECODE((timespec_user64_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user64_t *)attrbufptr) + 1;
		} else {
			ZFS_TIME_DECODE((timespec_user32_t *)attrbufptr,
			    times);
			attrbufptr = ((timespec_user32_t *)attrbufptr) + 1;
		}
	}
	if (ATTR_CMN_BKUPTIME & commonattr) {
		/* legacy attribute -- just pass zero */
		if (user64) {
			((timespec_user64_t *)attrbufptr)->tv_sec = 0;
			((timespec_user64_t *)attrbufptr)->tv_nsec = 0;
			attrbufptr = ((timespec_user64_t *)attrbufptr) + 1;
		} else {
			((timespec_user32_t *)attrbufptr)->tv_sec = 0;
			((timespec_user32_t *)attrbufptr)->tv_nsec = 0;
			attrbufptr = ((timespec_user32_t *)attrbufptr) + 1;
		}
	}
	if (ATTR_CMN_FNDRINFO & commonattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
		    &val, sizeof (val)) == 0);
		getfinderinfo(zp, cr, &finderinfo);
		/* Shadow ZFS_HIDDEN to Finder Info's invisible bit */
		if (val & ZFS_HIDDEN) {
			finderinfo.fi_flags |=
			    OSSwapHostToBigConstInt16(kIsInvisible);
		}
		memcpy(attrbufptr, &finderinfo, sizeof (finderinfo));
		attrbufptr = (char *)attrbufptr + 32;
	}
	if (ATTR_CMN_OWNERID & commonattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_UID(zfsvfs),
		    &val, sizeof (val)) == 0);
		*((uid_t *)attrbufptr) = val;
		attrbufptr = ((uid_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_GRPID & commonattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_GID(zfsvfs),
		    &val, sizeof (val)) == 0);
		*((gid_t *)attrbufptr) = val;
		attrbufptr = ((gid_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_ACCESSMASK & commonattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs),
		    &val, sizeof (val)) == 0);
		*((u_int32_t *)attrbufptr) = val;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_FLAGS & commonattr) {
		// TODO, sa_lookup of ZPL_FLAGS
		u_int32_t flags = zfs_getbsdflags(zp);

		/* Shadow Finder Info's invisible bit to UF_HIDDEN */
		if ((ATTR_CMN_FNDRINFO & commonattr) &&
		    (OSSwapBigToHostInt16(finderinfo.fi_flags) & kIsInvisible))
			flags |= UF_HIDDEN;

		*((u_int32_t *)attrbufptr) = flags;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_USERACCESS & commonattr) {
		u_int32_t user_access = 0;
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
		    &val, sizeof (val)) == 0);

		user_access = getuseraccess(zp, aip->ai_context);

		/* Also consider READ-ONLY file system. */
		if (vfs_flags(mp) & MNT_RDONLY) {
			user_access &= ~W_OK;
		}

		/* Locked objects are not writable either */
		if ((val & ZFS_IMMUTABLE) &&
		    (vfs_context_suser(aip->ai_context) != 0)) {
			user_access &= ~W_OK;
		}

		*((u_int32_t *)attrbufptr) = user_access;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_FILEID & commonattr) {
		/*
		 * On Mac OS X we always export the root directory id as 2
		 */
		if (objnum == zfsvfs->z_root)
			objnum = 2;

		*((u_int64_t *)attrbufptr) = objnum;
		attrbufptr = ((u_int64_t *)attrbufptr) + 1;
	}
	if (ATTR_CMN_PARENTID & commonattr) {
		uint64_t parentid;

		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
		    &parentid, sizeof (parentid)) == 0);

		/*
		 * On Mac OS X we always export the root
		 * directory id as 2 and its parent as 1
		 */
		if (zp && zp->z_id == zfsvfs->z_root)
			parentid = 1;
		else if (parentid == zfsvfs->z_root)
			parentid = 2;

		ASSERT(parentid != 0);

		*((u_int64_t *)attrbufptr) = parentid;
		attrbufptr = ((u_int64_t *)attrbufptr) + 1;
	}

	*aip->ai_attrbufpp = attrbufptr;
	*aip->ai_varbufpp = varbufptr;
}

void
dirattrpack(attrinfo_t *aip, znode_t *zp)
{
	attrgroup_t dirattr = aip->ai_attrlist->dirattr;
	void *attrbufptr = *aip->ai_attrbufpp;

	if (ATTR_DIR_LINKCOUNT & dirattr) {
		*((u_int32_t *)attrbufptr) = 1;  /* no dir hard links */
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_DIR_ENTRYCOUNT & dirattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_SIZE(zp->z_zfsvfs),
		    &val, sizeof (val)) == 0);
		*((u_int32_t *)attrbufptr) = (uint32_t)val;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_DIR_MOUNTSTATUS & dirattr && zp) {
		vnode_t *vp = ZTOV(zp);

		if (vp != NULL && vnode_mountedhere(vp) != NULL)
			*((u_int32_t *)attrbufptr) = DIR_MNTSTATUS_MNTPOINT;
		else
			*((u_int32_t *)attrbufptr) = 0;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	*aip->ai_attrbufpp = attrbufptr;
}

void
fileattrpack(attrinfo_t *aip, zfsvfs_t *zfsvfs, znode_t *zp)
{
	attrgroup_t fileattr = aip->ai_attrlist->fileattr;
	void *attrbufptr = *aip->ai_attrbufpp;
	void *varbufptr = *aip->ai_varbufpp;
	uint64_t allocsize = 0;
	cred_t  *cr = (cred_t *)vfs_context_ucred(aip->ai_context);

	if ((ATTR_FILE_ALLOCSIZE | ATTR_FILE_DATAALLOCSIZE) & fileattr && zp) {
		uint32_t  blksize;
		u_longlong_t  nblks;

		sa_object_size(zp->z_sa_hdl, &blksize, &nblks);
		allocsize = (uint64_t)512LL * (uint64_t)nblks;
	}
	if (ATTR_FILE_LINKCOUNT & fileattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_LINKS(zfsvfs),
		    &val, sizeof (val)) == 0);
		*((u_int32_t *)attrbufptr) = val;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_FILE_TOTALSIZE & fileattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
		    &val, sizeof (val)) == 0);
		*((off_t *)attrbufptr) = val;
		attrbufptr = ((off_t *)attrbufptr) + 1;
	}
	if (ATTR_FILE_ALLOCSIZE & fileattr) {
		*((off_t *)attrbufptr) = allocsize;
		attrbufptr = ((off_t *)attrbufptr) + 1;
	}
	if (ATTR_FILE_IOBLOCKSIZE & fileattr && zp) {
		*((u_int32_t *)attrbufptr) =
		    zp->z_blksz ? zp->z_blksz : zfsvfs->z_max_blksz;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_FILE_DEVTYPE & fileattr) {
		uint64_t mode, val = 0;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs),
		    &mode, sizeof (mode)) == 0);
		sa_lookup(zp->z_sa_hdl, SA_ZPL_RDEV(zfsvfs),
		    &val, sizeof (val));
		if (S_ISBLK(mode) || S_ISCHR(mode))
			*((u_int32_t *)attrbufptr) = (u_int32_t)val;
		else
			*((u_int32_t *)attrbufptr) = 0;
		attrbufptr = ((u_int32_t *)attrbufptr) + 1;
	}
	if (ATTR_FILE_DATALENGTH & fileattr) {
		uint64_t val;
		VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
		    &val, sizeof (val)) == 0);
		*((off_t *)attrbufptr) = val;
		attrbufptr = ((off_t *)attrbufptr) + 1;
	}
	if (ATTR_FILE_DATAALLOCSIZE & fileattr) {
		*((off_t *)attrbufptr) = allocsize;
		attrbufptr = ((off_t *)attrbufptr) + 1;
	}
	if ((ATTR_FILE_RSRCLENGTH | ATTR_FILE_RSRCALLOCSIZE) & fileattr) {
		uint64_t rsrcsize = 0;
		uint64_t xattr;

		if (!sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr, sizeof (xattr)) &&
		    xattr) {
			znode_t *xdzp = NULL, *xzp = NULL;
			struct componentname cn = { 0 };
			char *name = NULL;

			name = spa_strdup(XATTR_RESOURCEFORK_NAME);
			cn.cn_namelen = strlen(name)+1;
			cn.cn_nameptr = kmem_zalloc(cn.cn_namelen, KM_SLEEP);

			/* Grab the hidden attribute directory vnode. */
			if (zfs_get_xattrdir(zp, &xdzp, cr, 0) == 0 &&
			    zfs_dirlook(xdzp, name, &xzp, 0, NULL,
			    &cn) == 0) {
				rsrcsize = xzp->z_size;
			}
			spa_strfree(name);
			kmem_free(cn.cn_nameptr, cn.cn_namelen);

			if (xzp)
				zrele(xzp);
			if (xdzp)
				zrele(xdzp);
		}
		if (ATTR_FILE_RSRCLENGTH & fileattr) {
			*((off_t *)attrbufptr) = rsrcsize;
			attrbufptr = ((off_t *)attrbufptr) + 1;
		}
		if (ATTR_FILE_RSRCALLOCSIZE & fileattr) {
			*((off_t *)attrbufptr) = roundup(rsrcsize, 512);
			attrbufptr = ((off_t *)attrbufptr) + 1;
		}
	}
	*aip->ai_attrbufpp = attrbufptr;
	*aip->ai_varbufpp = varbufptr;
}

void
nameattrpack(attrinfo_t *aip, const char *name, int namelen)
{
	void *varbufptr;
	struct attrreference *attr_refptr;
	u_int32_t attrlen;
	size_t nfdlen, freespace;
	int force_formd_normalized_output;

	varbufptr = *aip->ai_varbufpp;
	attr_refptr = (struct attrreference *)(*aip->ai_attrbufpp);

	freespace = (char *)aip->ai_varbufend - (char *)varbufptr;
	/*
	 * Mac OS X: non-ascii names are UTF-8 NFC on disk
	 * so convert to NFD before exporting them.
	 */

	if (zfs_vnop_force_formd_normalized_output &&
	    !is_ascii_str(name))
		force_formd_normalized_output = 1;
	else
		force_formd_normalized_output = 0;

	namelen = strlen(name);
	if (!force_formd_normalized_output ||
	    utf8_normalizestr((const u_int8_t *)name, namelen,
	    (u_int8_t *)varbufptr, &nfdlen,
	    freespace, UTF_DECOMPOSED) != 0) {
		/* ASCII or normalization failed, just copy zap name. */
		strncpy((char *)varbufptr, name, MIN(freespace, namelen+1));
	} else {
		/* Normalization succeeded (already in buffer). */
		namelen = nfdlen;
	}
	attrlen = namelen + 1;
	attr_refptr->attr_dataoffset = (char *)varbufptr - (char *)attr_refptr;
	attr_refptr->attr_length = attrlen;
	/*
	 * Advance beyond the space just allocated and
	 * round up to the next 4-byte boundary:
	 */
	varbufptr = ((char *)varbufptr) + attrlen + ((4 - (attrlen & 3)) & 3);
	++attr_refptr;

	*aip->ai_attrbufpp = attr_refptr;
	*aip->ai_varbufpp = varbufptr;
}

int
getpackedsize(struct attrlist *alp, boolean_t user64)
{
	attrgroup_t attrs;
	int timespecsize;
	int size = 0;

	timespecsize = user64 ? sizeof (timespec_user64_t) :
	    sizeof (timespec_user32_t);

	if ((attrs = alp->commonattr) != 0) {
		if (attrs & ATTR_CMN_NAME)
			size += sizeof (struct attrreference);
		if (attrs & ATTR_CMN_DEVID)
			size += sizeof (dev_t);
		if (attrs & ATTR_CMN_FSID)
			size += sizeof (fsid_t);
		if (attrs & ATTR_CMN_OBJTYPE)
			size += sizeof (fsobj_type_t);
		if (attrs & ATTR_CMN_OBJTAG)
			size += sizeof (fsobj_tag_t);
		if (attrs & ATTR_CMN_OBJID)
			size += sizeof (fsobj_id_t);
		if (attrs & ATTR_CMN_OBJPERMANENTID)
			size += sizeof (fsobj_id_t);
		if (attrs & ATTR_CMN_PAROBJID)
			size += sizeof (fsobj_id_t);
		if (attrs & ATTR_CMN_SCRIPT)
			size += sizeof (text_encoding_t);
		if (attrs & ATTR_CMN_CRTIME)
			size += timespecsize;
		if (attrs & ATTR_CMN_MODTIME)
			size += timespecsize;
		if (attrs & ATTR_CMN_CHGTIME)
			size += timespecsize;
		if (attrs & ATTR_CMN_ACCTIME)
			size += timespecsize;
		if (attrs & ATTR_CMN_BKUPTIME)
			size += timespecsize;
		if (attrs & ATTR_CMN_FNDRINFO)
			size += 32 * sizeof (u_int8_t);
		if (attrs & ATTR_CMN_OWNERID)
			size += sizeof (uid_t);
		if (attrs & ATTR_CMN_GRPID)
			size += sizeof (gid_t);
		if (attrs & ATTR_CMN_ACCESSMASK)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_CMN_FLAGS)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_CMN_USERACCESS)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_CMN_FILEID)
			size += sizeof (u_int64_t);
		if (attrs & ATTR_CMN_PARENTID)
			size += sizeof (u_int64_t);
		/*
		 * Also add:
		 * ATTR_CMN_GEN_COUNT         (|FSOPT_ATTR_CMN_EXTENDED)
		 * ATTR_CMN_DOCUMENT_ID       (|FSOPT_ATTR_CMN_EXTENDED)
		 * ATTR_CMN_EXTENDED_SECURITY
		 * ATTR_CMN_UUID
		 * ATTR_CMN_GRPUUID
		 * ATTR_CMN_FULLPATH
		 * ATTR_CMN_ADDEDTIME
		 * ATTR_CMN_ERROR
		 * ATTR_CMN_DATA_PROTECT_FLAGS
		 */
	}
	if ((attrs = alp->dirattr) != 0) {
		if (attrs & ATTR_DIR_LINKCOUNT)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_DIR_ENTRYCOUNT)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_DIR_MOUNTSTATUS)
			size += sizeof (u_int32_t);
	}
	if ((attrs = alp->fileattr) != 0) {
		if (attrs & ATTR_FILE_LINKCOUNT)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_FILE_TOTALSIZE)
			size += sizeof (off_t);
		if (attrs & ATTR_FILE_ALLOCSIZE)
			size += sizeof (off_t);
		if (attrs & ATTR_FILE_IOBLOCKSIZE)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_FILE_DEVTYPE)
			size += sizeof (u_int32_t);
		if (attrs & ATTR_FILE_DATALENGTH)
			size += sizeof (off_t);
		if (attrs & ATTR_FILE_DATAALLOCSIZE)
			size += sizeof (off_t);
		if (attrs & ATTR_FILE_RSRCLENGTH)
			size += sizeof (off_t);
		if (attrs & ATTR_FILE_RSRCALLOCSIZE)
			size += sizeof (off_t);
	}
	return (size);
}


void
getfinderinfo(znode_t *zp, cred_t *cr, finderinfo_t *fip)
{
	znode_t	*xdzp = NULL;
	znode_t	*xzp = NULL;
	struct uio		*auio = NULL;
	struct componentname  cn = { 0 };
	int		error;
	uint64_t xattr = 0;
	char *name = NULL;

	if (sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zp->z_zfsvfs),
	    &xattr, sizeof (xattr)) ||
	    (xattr == 0)) {
		goto nodata;
	}

	// Change this to internal uio ?
	auio = uio_create(1, 0, UIO_SYSSPACE, UIO_READ);
	if (auio == NULL) {
		goto nodata;
	}
	uio_addiov(auio, CAST_USER_ADDR_T(fip), sizeof (finderinfo_t));

	/*
	 * Grab the hidden attribute directory vnode.
	 *
	 * XXX - switch to embedded Finder Info when it becomes available
	 */
	if ((error = zfs_get_xattrdir(zp, &xdzp, cr, 0))) {
		goto out;
	}

	name = spa_strdup(XATTR_FINDERINFO_NAME);
	cn.cn_namelen = strlen(name)+1;
	cn.cn_nameptr = kmem_zalloc(cn.cn_namelen, KM_SLEEP);

	if ((error = zfs_dirlook(xdzp, name, &xzp, 0, NULL, &cn))) {
		goto out;
	}

	ZFS_UIO_INIT_XNU(uio, auio);
	error = dmu_read_uio(zp->z_zfsvfs->z_os, xzp->z_id, uio,
	    sizeof (finderinfo_t));
out:
	if (name)
		spa_strfree(name);
	if (cn.cn_nameptr)
		kmem_free(cn.cn_nameptr, cn.cn_namelen);
	if (auio)
		uio_free(auio);
	if (xzp)
		zrele(xzp);
	if (xdzp)
		zrele(xdzp);
	if (error == 0)
		return;
nodata:
	memset(fip, 0, sizeof (finderinfo_t));
}

#define	KAUTH_DIR_WRITE (KAUTH_VNODE_ACCESS | KAUTH_VNODE_ADD_FILE |	\
    KAUTH_VNODE_ADD_SUBDIRECTORY |	\
    KAUTH_VNODE_DELETE_CHILD)

#define	KAUTH_DIR_READ	(KAUTH_VNODE_ACCESS | KAUTH_VNODE_LIST_DIRECTORY)

#define	KAUTH_DIR_EXECUTE	(KAUTH_VNODE_ACCESS | KAUTH_VNODE_SEARCH)

#define	KAUTH_FILE_WRITE	(KAUTH_VNODE_ACCESS | KAUTH_VNODE_WRITE_DATA)

#define	KAUTH_FILE_READ		(KAUTH_VNODE_ACCESS | KAUTH_VNODE_READ_DATA)

#define	KAUTH_FILE_EXECUTE	(KAUTH_VNODE_ACCESS | KAUTH_VNODE_EXECUTE)

/*
 * Compute the same user access value as getattrlist(2)
 */
u_int32_t
getuseraccess(znode_t *zp, vfs_context_t ctx)
{
	vnode_t	*vp;
	u_int32_t	user_access = 0;
	zfs_acl_phys_t acl_phys;
	int error;
	/* Only take the expensive vnode_authorize path when we have an ACL */

	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_ZNODE_ACL(zp->z_zfsvfs),
	    &acl_phys, sizeof (acl_phys));

	if (error || acl_phys.z_acl_count == 0) {
		kauth_cred_t	cred = vfs_context_ucred(ctx);
		uint64_t		obj_uid;
		uint64_t    	obj_mode;

		/* User id 0 (root) always gets access. */
		if (!vfs_context_suser(ctx)) {
			return (R_OK | W_OK | X_OK);
		}

		sa_lookup(zp->z_sa_hdl, SA_ZPL_UID(zp->z_zfsvfs),
		    &obj_uid, sizeof (obj_uid));
		sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zp->z_zfsvfs),
		    &obj_mode, sizeof (obj_mode));

		// obj_uid = pzp->zp_uid;
		obj_mode = obj_mode & MODEMASK;
		if (obj_uid == UNKNOWNUID) {
			obj_uid = kauth_cred_getuid(cred);
		}
		if ((obj_uid == kauth_cred_getuid(cred)) ||
		    (obj_uid == UNKNOWNUID)) {
			return (((u_int32_t)obj_mode & S_IRWXU) >> 6);
		}
		/* Otherwise, settle for 'others' access. */
		return ((u_int32_t)obj_mode & S_IRWXO);
	}
	vp = ZTOV(zp);
	if (vnode_isdir(vp)) {
		if (vnode_authorize(vp, NULLVP, KAUTH_DIR_WRITE, ctx) == 0)
			user_access |= W_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_DIR_READ, ctx) == 0)
			user_access |= R_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_DIR_EXECUTE, ctx) == 0)
			user_access |= X_OK;
	} else {
		if (vnode_authorize(vp, NULLVP, KAUTH_FILE_WRITE, ctx) == 0)
			user_access |= W_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_FILE_READ, ctx) == 0)
			user_access |= R_OK;
		if (vnode_authorize(vp, NULLVP, KAUTH_FILE_EXECUTE, ctx) == 0)
			user_access |= X_OK;
	}
	return (user_access);
}



static unsigned char fingerprint[] = {0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef,
    0xab, 0xcd, 0xef, 0xab, 0xcd, 0xef};

/*
 * Convert "Well Known" GUID to enum type.
 */
int
kauth_wellknown_guid(guid_t *guid)
{
	uint32_t last = 0;

	if (memcmp(fingerprint, guid->g_guid, sizeof (fingerprint)))
		return (KAUTH_WKG_NOT);

	last = BE_32(*((u_int32_t *)&guid->g_guid[12]));

	switch (last) {
		case 0x0c:
			return (KAUTH_WKG_EVERYBODY);
		case 0x0a:
			return (KAUTH_WKG_OWNER);
		case 0x10:
			return (KAUTH_WKG_GROUP);
		case 0xFFFFFFFE:
			return (KAUTH_WKG_NOBODY);
	}

	return (KAUTH_WKG_NOT);
}


/*
 * Set GUID to "well known" guid, based on enum type
 */
void
nfsacl_set_wellknown(int wkg, guid_t *guid)
{
	/*
	 * All WKGs begin with the same 12 bytes.
	 */
	memcpy((void *)guid, fingerprint, 12);
	/*
	 * The final 4 bytes are our code (in network byte order).
	 */
	switch (wkg) {
		case 4:
			*((u_int32_t *)&guid->g_guid[12]) = BE_32(0x0000000c);
			break;
		case 3:
			*((u_int32_t *)&guid->g_guid[12]) = BE_32(0xfffffffe);
			break;
		case 1:
			*((u_int32_t *)&guid->g_guid[12]) = BE_32(0x0000000a);
			break;
		case 2:
			*((u_int32_t *)&guid->g_guid[12]) = BE_32(0x00000010);
	};
}


/*
 * Convert Darwin ACL list, into ZFS ACL "aces" list.
 */
void
aces_from_acl(ace_t *aces, int *nentries, struct kauth_acl *k_acl,
    int *seen_type)
{
	int i;
	ace_t *ace;
	guid_t *guidp;
	kauth_ace_rights_t  ace_rights;
	uid_t  who;
	uint32_t  mask = 0;
	uint16_t  flags = 0;
	uint16_t  type = 0;
	u_int32_t  ace_flags;
	int wkg;
	int err = 0;

	*nentries = k_acl->acl_entrycount;

	// memset(aces, 0, sizeof (*aces) * *nentries);

	// *nentries = aclp->acl_cnt;

	for (i = 0; i < *nentries; i++) {
		// entry = &(aclp->acl_entry[i]);

		flags = 0;
		mask  = 0;

		ace = &(aces[i]);

		/* Note Mac OS X GUID is a 128-bit identifier */
		guidp = &k_acl->acl_ace[i].ace_applicable;

		who = -1;
		wkg = kauth_wellknown_guid(guidp);

		switch (wkg) {
			case KAUTH_WKG_OWNER:
				flags |= ACE_OWNER;
				if (seen_type) *seen_type |= ACE_OWNER;
				break;
			case KAUTH_WKG_GROUP:
				flags |= ACE_GROUP|ACE_IDENTIFIER_GROUP;
				if (seen_type) *seen_type |= ACE_GROUP;
				break;
			case KAUTH_WKG_EVERYBODY:
				flags |= ACE_EVERYONE;
				if (seen_type) *seen_type |= ACE_EVERYONE;
				break;

			case KAUTH_WKG_NOBODY:
			default:
				/* Try to get a uid from supplied guid */
				err = kauth_cred_guid2uid(guidp, &who);
				if (err) {
					err = kauth_cred_guid2gid(guidp, &who);
					if (!err) {
						flags |= ACE_IDENTIFIER_GROUP;
					}
				}
				if (err) {
					*nentries = 0;
					return;
				}

		} // switch

		ace->a_who = who;

		ace_rights = k_acl->acl_ace[i].ace_rights;
		if (ace_rights & KAUTH_VNODE_READ_DATA)
			mask |= ACE_READ_DATA;
		if (ace_rights & KAUTH_VNODE_WRITE_DATA)
			mask |= ACE_WRITE_DATA;
		if (ace_rights & KAUTH_VNODE_APPEND_DATA)
			mask |= ACE_APPEND_DATA;
		if (ace_rights & KAUTH_VNODE_READ_EXTATTRIBUTES)
			mask |= ACE_READ_NAMED_ATTRS;
		if (ace_rights & KAUTH_VNODE_WRITE_EXTATTRIBUTES)
			mask |= ACE_WRITE_NAMED_ATTRS;
		if (ace_rights & KAUTH_VNODE_EXECUTE)
			mask |= ACE_EXECUTE;
		if (ace_rights & KAUTH_VNODE_DELETE_CHILD)
			mask |= ACE_DELETE_CHILD;
		if (ace_rights & KAUTH_VNODE_READ_ATTRIBUTES)
			mask |= ACE_READ_ATTRIBUTES;
		if (ace_rights & KAUTH_VNODE_WRITE_ATTRIBUTES)
			mask |= ACE_WRITE_ATTRIBUTES;
		if (ace_rights & KAUTH_VNODE_DELETE)
			mask |= ACE_DELETE;
		if (ace_rights & KAUTH_VNODE_READ_SECURITY)
			mask |= ACE_READ_ACL;
		if (ace_rights & KAUTH_VNODE_WRITE_SECURITY)
			mask |= ACE_WRITE_ACL;
		if (ace_rights & KAUTH_VNODE_TAKE_OWNERSHIP)
			mask |= ACE_WRITE_OWNER;
		if (ace_rights & KAUTH_VNODE_SYNCHRONIZE)
			mask |= ACE_SYNCHRONIZE;
		ace->a_access_mask = mask;

		ace_flags = k_acl->acl_ace[i].ace_flags;
		if (ace_flags & KAUTH_ACE_FILE_INHERIT)
			flags |= ACE_FILE_INHERIT_ACE;
		if (ace_flags & KAUTH_ACE_DIRECTORY_INHERIT)
			flags |= ACE_DIRECTORY_INHERIT_ACE;
		if (ace_flags & KAUTH_ACE_LIMIT_INHERIT)
			flags |= ACE_NO_PROPAGATE_INHERIT_ACE;
		if (ace_flags & KAUTH_ACE_ONLY_INHERIT)
			flags |= ACE_INHERIT_ONLY_ACE;
		ace->a_flags = flags;

		switch (ace_flags & KAUTH_ACE_KINDMASK) {
			case KAUTH_ACE_PERMIT:
				type = ACE_ACCESS_ALLOWED_ACE_TYPE;
				break;
			case KAUTH_ACE_DENY:
				type = ACE_ACCESS_DENIED_ACE_TYPE;
				break;
			case KAUTH_ACE_AUDIT:
				type = ACE_SYSTEM_AUDIT_ACE_TYPE;
				break;
			case KAUTH_ACE_ALARM:
				type = ACE_SYSTEM_ALARM_ACE_TYPE;
				break;
		}
		ace->a_type = type;
		dprintf("  ACL: %d type %04x, mask %04x, flags %04x, who %d\n",
		    i, type, mask, flags, who);
	}

}

void
finderinfo_update(uint8_t *finderinfo, znode_t *zp)
{
	u_int8_t *finfo = NULL;

	/* Advance finfo by 16 bytes to the 2nd half of the finderinfo */
	finfo = (u_int8_t *)finderinfo + 16;

	/* Don't expose a symlink's private type/creator. */
	if (IFTOVT((mode_t)zp->z_mode) == VLNK) {
		struct FndrFileInfo *fip;

		fip = (struct FndrFileInfo *)finderinfo;
		fip->fdType = 0;
		fip->fdCreator = 0;
	}

	/* hfs_xattr.c hfs_zero_hidden_fields() */
	if ((IFTOVT((mode_t)zp->z_mode) == VREG) ||
	    (IFTOVT((mode_t)zp->z_mode) == VLNK)) {
		struct FndrExtendedFileInfo *extinfo =
		    (struct FndrExtendedFileInfo *)finfo;
		extinfo->document_id = 0;
		extinfo->date_added = 0;
		extinfo->write_gen_counter = 0;
	}

	if (IFTOVT((mode_t)zp->z_mode) == VDIR) {
		struct FndrExtendedDirInfo *extinfo =
		    (struct FndrExtendedDirInfo *)finfo;
		extinfo->document_id = 0;
		extinfo->date_added = 0;
		extinfo->write_gen_counter = 0;
	}

}

/*
 * Document ID. Persistant IDs that can survive "safe saving".
 * 'revisiond' appears to use fchflags(UF_TRACKED) on files/dirs
 * that it wishes to use DocumentIDs with. Here, we will lookup
 * if an entry already has a DocumentID stored in SA, but if not,
 * hash the DocumentID for (PARENTID + filename) and return it.
 * In vnop_setattr for UF_TRACKED, we will store the DocumentID to
 * disk.
 * Although it is not entirely clear which situations we should handle
 * we do handle:
 *
 * Case 1:
 *   "file.txt" gets chflag(UF_TRACKED) and DocumentID set.
 *   "file.txt" is renamed to "file.tmp". DocumentID is kept.
 *   "file.txt" is re-created, DocumentID remains same, but not saved.
 *
 * Case 2:
 *   "file.txt" gets chflag(UF_TRACKED) and DocumentID set.
 *   "file.txt" is moved to another directory. DocumentID is kept.
 *
 * It is interesting to note that HFS+ has "tombstones" which is
 * created when a UF_TRACKED entry is unlinked, or, renamed.
 * Then if a new entry is created with same PARENT+name, and matching
 * tombstone is found, will inherit the DocumentID, and UF_TRACKED flag.
 *
 * We may need to implement this as well.
 *
 * If "name" or "parent" is known, pass it along, or it needs to look it up.
 *
 */
void
zfs_setattr_generate_id(znode_t *zp, uint64_t val, char *name)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	char *nameptr = NULL;
	char *filename = NULL;
	uint64_t parent = val;
	int error = 0;
	uint64_t docid = 0;

	if (!zp->z_document_id && zp->z_sa_hdl) {

		error = sa_lookup(zp->z_sa_hdl, SA_ZPL_DOCUMENTID(zfsvfs),
		    &docid, sizeof (docid));
		if (!error && docid) {
			zp->z_document_id = docid;
			return;
		}

		/* Have name? */
		if (name && *name) {
			nameptr = name;
		} else {
			/* Do we have parent? */
			if (!parent) {
				VERIFY(sa_lookup(zp->z_sa_hdl,
				    SA_ZPL_PARENT(zfsvfs), &parent,
				    sizeof (parent)) == 0);
			}
			/* Lookup filename */
			filename = kmem_zalloc(MAXPATHLEN + 2, KM_SLEEP);
			if (zap_value_search(zfsvfs->z_os, parent, zp->z_id,
			    ZFS_DIRENT_OBJ(-1ULL), filename) == 0) {

				nameptr = filename;
				// Might as well keep this name too.
				strlcpy(zp->z_name_cache, filename,
				    MAXPATHLEN);
			}
		}

		zp->z_document_id = fnv_32a_buf(&parent, sizeof (parent),
		    FNV1_32A_INIT);
		if (nameptr)
			zp->z_document_id =
			    fnv_32a_str(nameptr, zp->z_document_id);

		if (filename)
			kmem_free(filename, MAXPATHLEN + 2);
	} // !document_id
}

/*
 * setattr asked for UF_TRACKED to be set, which means we will make sure
 * we have a hash made (includes getting filename) and stored in SA.
 */
int
zfs_setattr_set_documentid(znode_t *zp, boolean_t update_flags)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int error = 0;
	dmu_tx_t *tx;
	int count = 0;
	sa_bulk_attr_t  bulk[2];

	dprintf("ZFS: vnop_setattr(UF_TRACKED) obj %llu : documentid %08u\n",
	    zp->z_id,
	    zp->z_document_id);

	/* Write the new documentid to SA */
	if ((zfsvfs->z_use_sa == B_TRUE) &&
	    !vfs_isrdonly(zfsvfs->z_vfs) &&
	    spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {

		uint64_t docid = zp->z_document_id;  // 32->64

		if (update_flags == B_TRUE) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
			    NULL, &zp->z_pflags, 8);
		}
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_DOCUMENTID(zfsvfs), NULL,
		    &docid, sizeof (docid));

		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);

		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
		} else {
			error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
			dmu_tx_commit(tx);
		}

		if (error)
			dprintf("ZFS: sa_update(SA_ZPL_DOCUMENTID) failed %d\n",
			    error);

	} // if z_use_sa && !readonly

	return (error);
}

int
zfs_hardlink_addmap(znode_t *zp, uint64_t parentid, uint32_t linkid)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	hardlinks_t *searchnode, *findnode;
	avl_index_t loc;

	if (zp->z_name_cache[0] == 0) {
		dprintf("Addmap: skipping id %llu due to no name.\n",
		    zp->z_id);
		return (0);
	}

	dprintf("Addmap('%s' parentid %llu linkid %u (ZFS parentid %llu)\n",
	    zp->z_name_cache, parentid, linkid,
	    INO_XNUTOZFS(parentid, zfsvfs->z_root));
	parentid = INO_XNUTOZFS(parentid, zfsvfs->z_root);

	if (linkid == 0)
		linkid = atomic_inc_32_nv(&zfs_hardlink_sequence);

	searchnode = kmem_alloc(sizeof (hardlinks_t), KM_SLEEP);
	searchnode->hl_parent = parentid;
	searchnode->hl_fileid = zp->z_id;
	strlcpy(searchnode->hl_name, zp->z_name_cache, PATH_MAX);

	rw_enter(&zfsvfs->z_hardlinks_lock, RW_WRITER);
	findnode = avl_find(&zfsvfs->z_hardlinks, searchnode, &loc);
	kmem_free(searchnode, sizeof (hardlinks_t));
	if (!findnode) {
		// Add hash entry
		zp->z_finder_hardlink = TRUE;
		findnode = kmem_alloc(sizeof (hardlinks_t), KM_SLEEP);

		findnode->hl_parent = parentid;
		findnode->hl_fileid = zp->z_id;
		strlcpy(findnode->hl_name, zp->z_name_cache, PATH_MAX);

		findnode->hl_linkid = linkid;

		avl_add(&zfsvfs->z_hardlinks, findnode);
		avl_add(&zfsvfs->z_hardlinks_linkid, findnode);
		dprintf("ZFS: Inserted new hardlink node (%llu,%llu,'%s') "
		    "<-> (%x,%u)\n",
		    findnode->hl_parent,
		    findnode->hl_fileid, findnode->hl_name,
		    findnode->hl_linkid, findnode->hl_linkid);
	}
	rw_exit(&zfsvfs->z_hardlinks_lock);

	return (findnode ? 1 : 0);
}

/* dst buffer must be at least UUID_PRINTABLE_STRING_LENGTH bytes */

int
zfs_vfs_uuid_unparse(uuid_t uuid, char *dst)
{
	if (!uuid || !dst) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	snprintf(dst, UUID_PRINTABLE_STRING_LENGTH, "%02X%02X%02X%02X-"
	    "%02X%02X-%02X%02X-%02X%02X-%02X%02X%02X%02X%02X%02X",
	    uuid[0], uuid[1], uuid[2], uuid[3],
	    uuid[4], uuid[5], uuid[6], uuid[7],
	    uuid[8], uuid[9], uuid[10], uuid[11],
	    uuid[12], uuid[13], uuid[14], uuid[15]);

	return (0);
}

int
zfs_vfs_uuid_gen(const char *osname, uuid_t uuid)
{
	MD5_CTX  md5c;
	/* namespace (generated by uuidgen) */
	/* 50670853-FBD2-4EC3-9802-73D847BF7E62 */
	char namespace[16] = {0x50, 0x67, 0x08, 0x53, /* - */
	    0xfb, 0xd2, /* - */ 0x4e, 0xc3, /* - */
	    0x98, 0x02, /* - */
	    0x73, 0xd8, 0x47, 0xbf, 0x7e, 0x62};

	/* Validate arguments */
	if (!osname || !uuid || strlen(osname) == 0) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	/*
	 * UUID version 3 (MD5) namespace variant:
	 * hash namespace (uuid) together with name
	 */
	MD5Init(&md5c);
	MD5Update(&md5c, &namespace, sizeof (namespace));
	MD5Update(&md5c, osname, strlen(osname));
	MD5Final(uuid, &md5c);

	/*
	 * To make UUID version 3, twiddle a few bits:
	 * xxxxxxxx-xxxx-Mxxx-Nxxx-xxxxxxxxxxxx
	 * [uint32]-[uin-t32]-[uin-t32][uint32]
	 * M should be 0x3 to indicate uuid v3
	 * N should be 0x8, 0x9, 0xa, or 0xb
	 */
	uuid[6] = (uuid[6] & 0x0F) | 0x30;
	uuid[8] = (uuid[8] & 0x3F) | 0x80;

	/* Print all caps */
	// dprintf("%s UUIDgen: [%s](%ld)->"
	dprintf("%s UUIDgen: [%s](%ld) -> "
	    "[%02X%02X%02X%02X-%02X%02X-%02X%02X-"
	    "%02X%02X-%02X%02X%02X%02X%02X%02X]\n",
	    __func__, osname, strlen(osname),
	    uuid[0], uuid[1], uuid[2], uuid[3],
	    uuid[4], uuid[5], uuid[6], uuid[7],
	    uuid[8], uuid[9], uuid[10], uuid[11],
	    uuid[12], uuid[13], uuid[14], uuid[15]);

	return (0);
}

int
uio_prefaultpages(ssize_t n, struct uio *uio)
{
	return (0);
}
