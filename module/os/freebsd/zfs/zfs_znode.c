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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2014 by Delphix. All rights reserved.
 * Copyright (c) 2014 Integros [integros.com]
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2011 Martin Matuska <mm@FreeBSD.org> */

#ifdef _KERNEL
#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/resource.h>
#include <sys/mntent.h>
#include <sys/u8_textprep.h>
#include <sys/dsl_dataset.h>
#include <sys/vfs.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/errno.h>
#include <sys/unistd.h>
#include <sys/atomic.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_fuid.h>
#include <sys/dnode.h>
#include <sys/fs/zfs.h>
#endif /* _KERNEL */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/zfs_refcount.h>
#include <sys/stat.h>
#include <sys/zap.h>
#include <sys/zfs_znode.h>
#include <sys/sa.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>

#include "zfs_prop.h"
#include "zfs_comutil.h"

/* Used by fstat(1). */
SYSCTL_INT(_debug_sizeof, OID_AUTO, znode, CTLFLAG_RD,
	SYSCTL_NULL_INT_PTR, sizeof (znode_t), "sizeof(znode_t)");

/*
 * Define ZNODE_STATS to turn on statistic gathering. By default, it is only
 * turned on when DEBUG is also defined.
 */
#ifdef	ZFS_DEBUG
#define	ZNODE_STATS
#endif	/* DEBUG */

#ifdef	ZNODE_STATS
#define	ZNODE_STAT_ADD(stat)			((stat)++)
#else
#define	ZNODE_STAT_ADD(stat)			/* nothing */
#endif	/* ZNODE_STATS */

/*
 * Functions needed for userland (ie: libzpool) are not put under
 * #ifdef_KERNEL; the rest of the functions have dependencies
 * (such as VFS logic) that will not compile easily in userland.
 */
#ifdef _KERNEL
#if !defined(KMEM_DEBUG) && __FreeBSD_version >= 1300102
#define	_ZFS_USE_SMR
static uma_zone_t znode_uma_zone;
#else
static kmem_cache_t *znode_cache = NULL;
#endif

extern struct vop_vector zfs_vnodeops;
extern struct vop_vector zfs_fifoops;
extern struct vop_vector zfs_shareops;


/*
 * This callback is invoked when acquiring a RL_WRITER or RL_APPEND lock on
 * z_rangelock. It will modify the offset and length of the lock to reflect
 * znode-specific information, and convert RL_APPEND to RL_WRITER.  This is
 * called with the rangelock_t's rl_lock held, which avoids races.
 */
static void
zfs_rangelock_cb(zfs_locked_range_t *new, void *arg)
{
	znode_t *zp = arg;

	/*
	 * If in append mode, convert to writer and lock starting at the
	 * current end of file.
	 */
	if (new->lr_type == RL_APPEND) {
		new->lr_offset = zp->z_size;
		new->lr_type = RL_WRITER;
	}

	/*
	 * If we need to grow the block size then lock the whole file range.
	 */
	uint64_t end_size = MAX(zp->z_size, new->lr_offset + new->lr_length);
	if (end_size > zp->z_blksz && (!ISP2(zp->z_blksz) ||
	    zp->z_blksz < ZTOZSB(zp)->z_max_blksz)) {
		new->lr_offset = 0;
		new->lr_length = UINT64_MAX;
	}
}

static int
zfs_znode_cache_constructor(void *buf, void *arg, int kmflags)
{
	znode_t *zp = buf;

	POINTER_INVALIDATE(&zp->z_zfsvfs);

	list_link_init(&zp->z_link_node);

	mutex_init(&zp->z_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zp->z_acl_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zp->z_xattr_lock, NULL, RW_DEFAULT, NULL);

	zfs_rangelock_init(&zp->z_rangelock, zfs_rangelock_cb, zp);

	zp->z_acl_cached = NULL;
	zp->z_xattr_cached = NULL;
	zp->z_xattr_parent = 0;
	zp->z_vnode = NULL;
	return (0);
}

/*ARGSUSED*/
static void
zfs_znode_cache_destructor(void *buf, void *arg)
{
	znode_t *zp = buf;

	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs));
	ASSERT3P(zp->z_vnode, ==, NULL);
	ASSERT(!list_link_active(&zp->z_link_node));
	mutex_destroy(&zp->z_lock);
	mutex_destroy(&zp->z_acl_lock);
	rw_destroy(&zp->z_xattr_lock);
	zfs_rangelock_fini(&zp->z_rangelock);

	ASSERT3P(zp->z_acl_cached, ==, NULL);
	ASSERT3P(zp->z_xattr_cached, ==, NULL);
}


#ifdef _ZFS_USE_SMR
VFS_SMR_DECLARE;

static int
zfs_znode_cache_constructor_smr(void *mem, int size __unused, void *private,
    int flags)
{

	return (zfs_znode_cache_constructor(mem, private, flags));
}

static void
zfs_znode_cache_destructor_smr(void *mem, int size __unused, void *private)
{

	zfs_znode_cache_destructor(mem, private);
}

void
zfs_znode_init(void)
{
	/*
	 * Initialize zcache
	 */
	ASSERT3P(znode_uma_zone, ==, NULL);
	znode_uma_zone = uma_zcreate("zfs_znode_cache",
	    sizeof (znode_t), zfs_znode_cache_constructor_smr,
	    zfs_znode_cache_destructor_smr, NULL, NULL, 0, 0);
	VFS_SMR_ZONE_SET(znode_uma_zone);
}

static znode_t *
zfs_znode_alloc_kmem(int flags)
{

	return (uma_zalloc_smr(znode_uma_zone, flags));
}

static void
zfs_znode_free_kmem(znode_t *zp)
{
	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}
	uma_zfree_smr(znode_uma_zone, zp);
}
#else
void
zfs_znode_init(void)
{
	/*
	 * Initialize zcache
	 */
	ASSERT3P(znode_cache, ==, NULL);
	znode_cache = kmem_cache_create("zfs_znode_cache",
	    sizeof (znode_t), 0, zfs_znode_cache_constructor,
	    zfs_znode_cache_destructor, NULL, NULL, NULL, 0);
}

static znode_t *
zfs_znode_alloc_kmem(int flags)
{

	return (kmem_cache_alloc(znode_cache, flags));
}

static void
zfs_znode_free_kmem(znode_t *zp)
{
	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}
	kmem_cache_free(znode_cache, zp);
}
#endif

void
zfs_znode_fini(void)
{
	/*
	 * Cleanup zcache
	 */
#ifdef _ZFS_USE_SMR
	if (znode_uma_zone) {
		uma_zdestroy(znode_uma_zone);
		znode_uma_zone = NULL;
	}
#else
	if (znode_cache) {
		kmem_cache_destroy(znode_cache);
		znode_cache = NULL;
	}
#endif
}


static int
zfs_create_share_dir(zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
	zfs_acl_ids_t acl_ids;
	vattr_t vattr;
	znode_t *sharezp;
	znode_t *zp;
	int error;

	vattr.va_mask = AT_MODE|AT_UID|AT_GID;
	vattr.va_type = VDIR;
	vattr.va_mode = S_IFDIR|0555;
	vattr.va_uid = crgetuid(kcred);
	vattr.va_gid = crgetgid(kcred);

	sharezp = zfs_znode_alloc_kmem(KM_SLEEP);
	ASSERT(!POINTER_IS_VALID(sharezp->z_zfsvfs));
	sharezp->z_unlinked = 0;
	sharezp->z_atime_dirty = 0;
	sharezp->z_zfsvfs = zfsvfs;
	sharezp->z_is_sa = zfsvfs->z_use_sa;

	VERIFY0(zfs_acl_ids_create(sharezp, IS_ROOT_NODE, &vattr,
	    kcred, NULL, &acl_ids));
	zfs_mknode(sharezp, &vattr, tx, kcred, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, sharezp);
	POINTER_INVALIDATE(&sharezp->z_zfsvfs);
	error = zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
	    ZFS_SHARES_DIR, 8, 1, &sharezp->z_id, tx);
	zfsvfs->z_shares_dir = sharezp->z_id;

	zfs_acl_ids_free(&acl_ids);
	sa_handle_destroy(sharezp->z_sa_hdl);
	zfs_znode_free_kmem(sharezp);

	return (error);
}

/*
 * define a couple of values we need available
 * for both 64 and 32 bit environments.
 */
#ifndef NBITSMINOR64
#define	NBITSMINOR64	32
#endif
#ifndef MAXMAJ64
#define	MAXMAJ64	0xffffffffUL
#endif
#ifndef	MAXMIN64
#define	MAXMIN64	0xffffffffUL
#endif

/*
 * Create special expldev for ZFS private use.
 * Can't use standard expldev since it doesn't do
 * what we want.  The standard expldev() takes a
 * dev32_t in LP64 and expands it to a long dev_t.
 * We need an interface that takes a dev32_t in ILP32
 * and expands it to a long dev_t.
 */
static uint64_t
zfs_expldev(dev_t dev)
{
	return (((uint64_t)major(dev) << NBITSMINOR64) | minor(dev));
}
/*
 * Special cmpldev for ZFS private use.
 * Can't use standard cmpldev since it takes
 * a long dev_t and compresses it to dev32_t in
 * LP64.  We need to do a compaction of a long dev_t
 * to a dev32_t in ILP32.
 */
dev_t
zfs_cmpldev(uint64_t dev)
{
	return (makedev((dev >> NBITSMINOR64), (dev & MAXMIN64)));
}

static void
zfs_znode_sa_init(zfsvfs_t *zfsvfs, znode_t *zp,
    dmu_buf_t *db, dmu_object_type_t obj_type, sa_handle_t *sa_hdl)
{
	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs) || (zfsvfs == zp->z_zfsvfs));
	ASSERT(MUTEX_HELD(ZFS_OBJ_MUTEX(zfsvfs, zp->z_id)));

	ASSERT3P(zp->z_sa_hdl, ==, NULL);
	ASSERT3P(zp->z_acl_cached, ==, NULL);
	if (sa_hdl == NULL) {
		VERIFY0(sa_handle_get_from_db(zfsvfs->z_os, db, zp,
		    SA_HDL_SHARED, &zp->z_sa_hdl));
	} else {
		zp->z_sa_hdl = sa_hdl;
		sa_set_userp(sa_hdl, zp);
	}

	zp->z_is_sa = (obj_type == DMU_OT_SA) ? B_TRUE : B_FALSE;

	/*
	 * Slap on VROOT if we are the root znode unless we are the root
	 * node of a snapshot mounted under .zfs.
	 */
	if (zp->z_id == zfsvfs->z_root && zfsvfs->z_parent == zfsvfs)
		ZTOV(zp)->v_flag |= VROOT;

	vn_exists(ZTOV(zp));
}

void
zfs_znode_dmu_fini(znode_t *zp)
{
	ASSERT(MUTEX_HELD(ZFS_OBJ_MUTEX(zp->z_zfsvfs, zp->z_id)) ||
	    zp->z_unlinked ||
	    ZFS_TEARDOWN_INACTIVE_WRITE_HELD(zp->z_zfsvfs));

	sa_handle_destroy(zp->z_sa_hdl);
	zp->z_sa_hdl = NULL;
}

static void
zfs_vnode_forget(vnode_t *vp)
{

	/* copied from insmntque_stddtr */
	vp->v_data = NULL;
	vp->v_op = &dead_vnodeops;
	vgone(vp);
	vput(vp);
}

/*
 * Construct a new znode/vnode and initialize.
 *
 * This does not do a call to dmu_set_user() that is
 * up to the caller to do, in case you don't want to
 * return the znode
 */
static znode_t *
zfs_znode_alloc(zfsvfs_t *zfsvfs, dmu_buf_t *db, int blksz,
    dmu_object_type_t obj_type, sa_handle_t *hdl)
{
	znode_t	*zp;
	vnode_t *vp;
	uint64_t mode;
	uint64_t parent;
#ifdef notyet
	uint64_t mtime[2], ctime[2];
#endif
	uint64_t projid = ZFS_DEFAULT_PROJID;
	sa_bulk_attr_t bulk[9];
	int count = 0;
	int error;

	zp = zfs_znode_alloc_kmem(KM_SLEEP);

#ifndef _ZFS_USE_SMR
	KASSERT((zfsvfs->z_parent->z_vfs->mnt_kern_flag & MNTK_FPLOOKUP) == 0,
	    ("%s: fast path lookup enabled without smr", __func__));
#endif

#if __FreeBSD_version >= 1300076
	KASSERT(curthread->td_vp_reserved != NULL,
	    ("zfs_znode_alloc: getnewvnode without any vnodes reserved"));
#else
	KASSERT(curthread->td_vp_reserv > 0,
	    ("zfs_znode_alloc: getnewvnode without any vnodes reserved"));
#endif
	error = getnewvnode("zfs", zfsvfs->z_parent->z_vfs, &zfs_vnodeops, &vp);
	if (error != 0) {
		zfs_znode_free_kmem(zp);
		return (NULL);
	}
	zp->z_vnode = vp;
	vp->v_data = zp;

	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs));

	zp->z_sa_hdl = NULL;
	zp->z_unlinked = 0;
	zp->z_atime_dirty = 0;
	zp->z_mapcnt = 0;
	zp->z_id = db->db_object;
	zp->z_blksz = blksz;
	zp->z_seq = 0x7A4653;
	zp->z_sync_cnt = 0;
#if __FreeBSD_version >= 1300139
	atomic_store_ptr(&zp->z_cached_symlink, NULL);
#endif

	vp = ZTOV(zp);

	zfs_znode_sa_init(zfsvfs, zp, db, obj_type, hdl);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL, &mode, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GEN(zfsvfs), NULL, &zp->z_gen, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &zp->z_links, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL, &parent, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
	    &zp->z_atime, 16);
#ifdef notyet
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
	    &mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    &ctime, 16);
#endif
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
	    &zp->z_uid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
	    &zp->z_gid, 8);

	if (sa_bulk_lookup(zp->z_sa_hdl, bulk, count) != 0 || zp->z_gen == 0 ||
	    (dmu_objset_projectquota_enabled(zfsvfs->z_os) &&
	    (zp->z_pflags & ZFS_PROJID) &&
	    sa_lookup(zp->z_sa_hdl, SA_ZPL_PROJID(zfsvfs), &projid, 8) != 0)) {
		if (hdl == NULL)
			sa_handle_destroy(zp->z_sa_hdl);
		zfs_vnode_forget(vp);
		zp->z_vnode = NULL;
		zfs_znode_free_kmem(zp);
		return (NULL);
	}

	zp->z_projid = projid;
	zp->z_mode = mode;

	/* Cache the xattr parent id */
	if (zp->z_pflags & ZFS_XATTR)
		zp->z_xattr_parent = parent;

	vp->v_type = IFTOVT((mode_t)mode);

	switch (vp->v_type) {
	case VDIR:
		zp->z_zn_prefetch = B_TRUE; /* z_prefetch default is enabled */
		break;
	case VFIFO:
		vp->v_op = &zfs_fifoops;
		break;
	case VREG:
		if (parent == zfsvfs->z_shares_dir) {
			ASSERT0(zp->z_uid);
			ASSERT0(zp->z_gid);
			vp->v_op = &zfs_shareops;
		}
		break;
	default:
			break;
	}

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	zfsvfs->z_nr_znodes++;
	zp->z_zfsvfs = zfsvfs;
	mutex_exit(&zfsvfs->z_znodes_lock);

	/*
	 * Acquire vnode lock before making it available to the world.
	 */
	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
	VN_LOCK_AREC(vp);
	if (vp->v_type != VFIFO)
		VN_LOCK_ASHARE(vp);

	return (zp);
}

static uint64_t empty_xattr;
static uint64_t pad[4];
static zfs_acl_phys_t acl_phys;
/*
 * Create a new DMU object to hold a zfs znode.
 *
 *	IN:	dzp	- parent directory for new znode
 *		vap	- file attributes for new znode
 *		tx	- dmu transaction id for zap operations
 *		cr	- credentials of caller
 *		flag	- flags:
 *			  IS_ROOT_NODE	- new object will be root
 *			  IS_XATTR	- new object is an attribute
 *		bonuslen - length of bonus buffer
 *		setaclp  - File/Dir initial ACL
 *		fuidp	 - Tracks fuid allocation.
 *
 *	OUT:	zpp	- allocated znode
 *
 */
void
zfs_mknode(znode_t *dzp, vattr_t *vap, dmu_tx_t *tx, cred_t *cr,
    uint_t flag, znode_t **zpp, zfs_acl_ids_t *acl_ids)
{
	uint64_t	crtime[2], atime[2], mtime[2], ctime[2];
	uint64_t	mode, size, links, parent, pflags;
	uint64_t	dzp_pflags = 0;
	uint64_t	rdev = 0;
	zfsvfs_t	*zfsvfs = dzp->z_zfsvfs;
	dmu_buf_t	*db;
	timestruc_t	now;
	uint64_t	gen, obj;
	int		bonuslen;
	int		dnodesize;
	sa_handle_t	*sa_hdl;
	dmu_object_type_t obj_type;
	sa_bulk_attr_t	*sa_attrs;
	int		cnt = 0;
	zfs_acl_locator_cb_t locate = { 0 };

	ASSERT3P(vap, !=, NULL);
	ASSERT3U((vap->va_mask & AT_MODE), ==, AT_MODE);

	if (zfsvfs->z_replay) {
		obj = vap->va_nodeid;
		now = vap->va_ctime;		/* see zfs_replay_create() */
		gen = vap->va_nblocks;		/* ditto */
		dnodesize = vap->va_fsid;	/* ditto */
	} else {
		obj = 0;
		vfs_timestamp(&now);
		gen = dmu_tx_get_txg(tx);
		dnodesize = dmu_objset_dnodesize(zfsvfs->z_os);
	}

	if (dnodesize == 0)
		dnodesize = DNODE_MIN_SIZE;

	obj_type = zfsvfs->z_use_sa ? DMU_OT_SA : DMU_OT_ZNODE;
	bonuslen = (obj_type == DMU_OT_SA) ?
	    DN_BONUS_SIZE(dnodesize) : ZFS_OLD_ZNODE_PHYS_SIZE;

	/*
	 * Create a new DMU object.
	 */
	/*
	 * There's currently no mechanism for pre-reading the blocks that will
	 * be needed to allocate a new object, so we accept the small chance
	 * that there will be an i/o error and we will fail one of the
	 * assertions below.
	 */
	if (vap->va_type == VDIR) {
		if (zfsvfs->z_replay) {
			VERIFY0(zap_create_claim_norm_dnsize(zfsvfs->z_os, obj,
			    zfsvfs->z_norm, DMU_OT_DIRECTORY_CONTENTS,
			    obj_type, bonuslen, dnodesize, tx));
		} else {
			obj = zap_create_norm_dnsize(zfsvfs->z_os,
			    zfsvfs->z_norm, DMU_OT_DIRECTORY_CONTENTS,
			    obj_type, bonuslen, dnodesize, tx);
		}
	} else {
		if (zfsvfs->z_replay) {
			VERIFY0(dmu_object_claim_dnsize(zfsvfs->z_os, obj,
			    DMU_OT_PLAIN_FILE_CONTENTS, 0,
			    obj_type, bonuslen, dnodesize, tx));
		} else {
			obj = dmu_object_alloc_dnsize(zfsvfs->z_os,
			    DMU_OT_PLAIN_FILE_CONTENTS, 0,
			    obj_type, bonuslen, dnodesize, tx);
		}
	}

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj);
	VERIFY0(sa_buf_hold(zfsvfs->z_os, obj, NULL, &db));

	/*
	 * If this is the root, fix up the half-initialized parent pointer
	 * to reference the just-allocated physical data area.
	 */
	if (flag & IS_ROOT_NODE) {
		dzp->z_id = obj;
	} else {
		dzp_pflags = dzp->z_pflags;
	}

	/*
	 * If parent is an xattr, so am I.
	 */
	if (dzp_pflags & ZFS_XATTR) {
		flag |= IS_XATTR;
	}

	if (zfsvfs->z_use_fuids)
		pflags = ZFS_ARCHIVE | ZFS_AV_MODIFIED;
	else
		pflags = 0;

	if (vap->va_type == VDIR) {
		size = 2;		/* contents ("." and "..") */
		links = (flag & (IS_ROOT_NODE | IS_XATTR)) ? 2 : 1;
	} else {
		size = links = 0;
	}

	if (vap->va_type == VBLK || vap->va_type == VCHR) {
		rdev = zfs_expldev(vap->va_rdev);
	}

	parent = dzp->z_id;
	mode = acl_ids->z_mode;
	if (flag & IS_XATTR)
		pflags |= ZFS_XATTR;

	/*
	 * No execs denied will be determined when zfs_mode_compute() is called.
	 */
	pflags |= acl_ids->z_aclp->z_hints &
	    (ZFS_ACL_TRIVIAL|ZFS_INHERIT_ACE|ZFS_ACL_AUTO_INHERIT|
	    ZFS_ACL_DEFAULTED|ZFS_ACL_PROTECTED);

	ZFS_TIME_ENCODE(&now, crtime);
	ZFS_TIME_ENCODE(&now, ctime);

	if (vap->va_mask & AT_ATIME) {
		ZFS_TIME_ENCODE(&vap->va_atime, atime);
	} else {
		ZFS_TIME_ENCODE(&now, atime);
	}

	if (vap->va_mask & AT_MTIME) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
	} else {
		ZFS_TIME_ENCODE(&now, mtime);
	}

	/* Now add in all of the "SA" attributes */
	VERIFY0(sa_handle_get_from_db(zfsvfs->z_os, db, NULL, SA_HDL_SHARED,
	    &sa_hdl));

	/*
	 * Setup the array of attributes to be replaced/set on the new file
	 *
	 * order for  DMU_OT_ZNODE is critical since it needs to be constructed
	 * in the old znode_phys_t format.  Don't change this ordering
	 */
	sa_attrs = kmem_alloc(sizeof (sa_bulk_attr_t) * ZPL_END, KM_SLEEP);

	if (obj_type == DMU_OT_ZNODE) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ATIME(zfsvfs),
		    NULL, &atime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MTIME(zfsvfs),
		    NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CRTIME(zfsvfs),
		    NULL, &crtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GEN(zfsvfs),
		    NULL, &gen, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_SIZE(zfsvfs),
		    NULL, &size, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PARENT(zfsvfs),
		    NULL, &parent, 8);
	} else {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MODE(zfsvfs),
		    NULL, &mode, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_SIZE(zfsvfs),
		    NULL, &size, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GEN(zfsvfs),
		    NULL, &gen, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_UID(zfsvfs),
		    NULL, &acl_ids->z_fuid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GID(zfsvfs),
		    NULL, &acl_ids->z_fgid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PARENT(zfsvfs),
		    NULL, &parent, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &pflags, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ATIME(zfsvfs),
		    NULL, &atime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_MTIME(zfsvfs),
		    NULL, &mtime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, 16);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_CRTIME(zfsvfs),
		    NULL, &crtime, 16);
	}

	SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_LINKS(zfsvfs), NULL, &links, 8);

	if (obj_type == DMU_OT_ZNODE) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_XATTR(zfsvfs), NULL,
		    &empty_xattr, 8);
	}
	if (obj_type == DMU_OT_ZNODE ||
	    (vap->va_type == VBLK || vap->va_type == VCHR)) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_RDEV(zfsvfs),
		    NULL, &rdev, 8);

	}
	if (obj_type == DMU_OT_ZNODE) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &pflags, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_UID(zfsvfs), NULL,
		    &acl_ids->z_fuid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_GID(zfsvfs), NULL,
		    &acl_ids->z_fgid, 8);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_PAD(zfsvfs), NULL, pad,
		    sizeof (uint64_t) * 4);
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_ZNODE_ACL(zfsvfs), NULL,
		    &acl_phys, sizeof (zfs_acl_phys_t));
	} else if (acl_ids->z_aclp->z_version >= ZFS_ACL_VERSION_FUID) {
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_DACL_COUNT(zfsvfs), NULL,
		    &acl_ids->z_aclp->z_acl_count, 8);
		locate.cb_aclp = acl_ids->z_aclp;
		SA_ADD_BULK_ATTR(sa_attrs, cnt, SA_ZPL_DACL_ACES(zfsvfs),
		    zfs_acl_data_locator, &locate,
		    acl_ids->z_aclp->z_acl_bytes);
		mode = zfs_mode_compute(mode, acl_ids->z_aclp, &pflags,
		    acl_ids->z_fuid, acl_ids->z_fgid);
	}

	VERIFY0(sa_replace_all_by_template(sa_hdl, sa_attrs, cnt, tx));

	if (!(flag & IS_ROOT_NODE)) {
		*zpp = zfs_znode_alloc(zfsvfs, db, 0, obj_type, sa_hdl);
		ASSERT3P(*zpp, !=, NULL);
	} else {
		/*
		 * If we are creating the root node, the "parent" we
		 * passed in is the znode for the root.
		 */
		*zpp = dzp;

		(*zpp)->z_sa_hdl = sa_hdl;
	}

	(*zpp)->z_pflags = pflags;
	(*zpp)->z_mode = mode;
	(*zpp)->z_dnodesize = dnodesize;

	if (vap->va_mask & AT_XVATTR)
		zfs_xvattr_set(*zpp, (xvattr_t *)vap, tx);

	if (obj_type == DMU_OT_ZNODE ||
	    acl_ids->z_aclp->z_version < ZFS_ACL_VERSION_FUID) {
		VERIFY0(zfs_aclset_common(*zpp, acl_ids->z_aclp, cr, tx));
	}
	if (!(flag & IS_ROOT_NODE)) {
		vnode_t *vp = ZTOV(*zpp);
		vp->v_vflag |= VV_FORCEINSMQ;
		int err = insmntque(vp, zfsvfs->z_vfs);
		vp->v_vflag &= ~VV_FORCEINSMQ;
		(void) err;
		KASSERT(err == 0, ("insmntque() failed: error %d", err));
	}
	kmem_free(sa_attrs, sizeof (sa_bulk_attr_t) * ZPL_END);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj);
}

/*
 * Update in-core attributes.  It is assumed the caller will be doing an
 * sa_bulk_update to push the changes out.
 */
void
zfs_xvattr_set(znode_t *zp, xvattr_t *xvap, dmu_tx_t *tx)
{
	xoptattr_t *xoap;

	xoap = xva_getxoptattr(xvap);
	ASSERT3P(xoap, !=, NULL);

	if (zp->z_zfsvfs->z_replay == B_FALSE) {
		ASSERT_VOP_IN_SEQC(ZTOV(zp));
	}

	if (XVA_ISSET_REQ(xvap, XAT_CREATETIME)) {
		uint64_t times[2];
		ZFS_TIME_ENCODE(&xoap->xoa_createtime, times);
		(void) sa_update(zp->z_sa_hdl, SA_ZPL_CRTIME(zp->z_zfsvfs),
		    &times, sizeof (times), tx);
		XVA_SET_RTN(xvap, XAT_CREATETIME);
	}
	if (XVA_ISSET_REQ(xvap, XAT_READONLY)) {
		ZFS_ATTR_SET(zp, ZFS_READONLY, xoap->xoa_readonly,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_READONLY);
	}
	if (XVA_ISSET_REQ(xvap, XAT_HIDDEN)) {
		ZFS_ATTR_SET(zp, ZFS_HIDDEN, xoap->xoa_hidden,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_HIDDEN);
	}
	if (XVA_ISSET_REQ(xvap, XAT_SYSTEM)) {
		ZFS_ATTR_SET(zp, ZFS_SYSTEM, xoap->xoa_system,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_SYSTEM);
	}
	if (XVA_ISSET_REQ(xvap, XAT_ARCHIVE)) {
		ZFS_ATTR_SET(zp, ZFS_ARCHIVE, xoap->xoa_archive,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_ARCHIVE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
		ZFS_ATTR_SET(zp, ZFS_IMMUTABLE, xoap->xoa_immutable,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_IMMUTABLE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
		ZFS_ATTR_SET(zp, ZFS_NOUNLINK, xoap->xoa_nounlink,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_NOUNLINK);
	}
	if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
		ZFS_ATTR_SET(zp, ZFS_APPENDONLY, xoap->xoa_appendonly,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_APPENDONLY);
	}
	if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
		ZFS_ATTR_SET(zp, ZFS_NODUMP, xoap->xoa_nodump,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_NODUMP);
	}
	if (XVA_ISSET_REQ(xvap, XAT_OPAQUE)) {
		ZFS_ATTR_SET(zp, ZFS_OPAQUE, xoap->xoa_opaque,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_OPAQUE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
		ZFS_ATTR_SET(zp, ZFS_AV_QUARANTINED,
		    xoap->xoa_av_quarantined, zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_AV_QUARANTINED);
	}
	if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
		ZFS_ATTR_SET(zp, ZFS_AV_MODIFIED, xoap->xoa_av_modified,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_AV_MODIFIED);
	}
	if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) {
		zfs_sa_set_scanstamp(zp, xvap, tx);
		XVA_SET_RTN(xvap, XAT_AV_SCANSTAMP);
	}
	if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
		ZFS_ATTR_SET(zp, ZFS_REPARSE, xoap->xoa_reparse,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_REPARSE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_OFFLINE)) {
		ZFS_ATTR_SET(zp, ZFS_OFFLINE, xoap->xoa_offline,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_OFFLINE);
	}
	if (XVA_ISSET_REQ(xvap, XAT_SPARSE)) {
		ZFS_ATTR_SET(zp, ZFS_SPARSE, xoap->xoa_sparse,
		    zp->z_pflags, tx);
		XVA_SET_RTN(xvap, XAT_SPARSE);
	}
}

int
zfs_zget(zfsvfs_t *zfsvfs, uint64_t obj_num, znode_t **zpp)
{
	dmu_object_info_t doi;
	dmu_buf_t	*db;
	znode_t		*zp;
	vnode_t		*vp;
	sa_handle_t	*hdl;
	int locked;
	int err;

	getnewvnode_reserve_();
again:
	*zpp = NULL;
	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num);

	err = sa_buf_hold(zfsvfs->z_os, obj_num, NULL, &db);
	if (err) {
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		getnewvnode_drop_reserve();
		return (err);
	}

	dmu_object_info_from_db(db, &doi);
	if (doi.doi_bonus_type != DMU_OT_SA &&
	    (doi.doi_bonus_type != DMU_OT_ZNODE ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t)))) {
		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		getnewvnode_drop_reserve();
		return (SET_ERROR(EINVAL));
	}

	hdl = dmu_buf_get_user(db);
	if (hdl != NULL) {
		zp = sa_get_userdata(hdl);

		/*
		 * Since "SA" does immediate eviction we
		 * should never find a sa handle that doesn't
		 * know about the znode.
		 */
		ASSERT3P(zp, !=, NULL);
		ASSERT3U(zp->z_id, ==, obj_num);
		if (zp->z_unlinked) {
			err = SET_ERROR(ENOENT);
		} else {
			vp = ZTOV(zp);
			/*
			 * Don't let the vnode disappear after
			 * ZFS_OBJ_HOLD_EXIT.
			 */
			VN_HOLD(vp);
			*zpp = zp;
			err = 0;
		}

		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);

		if (err) {
			getnewvnode_drop_reserve();
			return (err);
		}

		locked = VOP_ISLOCKED(vp);
		VI_LOCK(vp);
		if (VN_IS_DOOMED(vp) && locked != LK_EXCLUSIVE) {
			/*
			 * The vnode is doomed and this thread doesn't
			 * hold the exclusive lock on it, so the vnode
			 * must be being reclaimed by another thread.
			 * Otherwise the doomed vnode is being reclaimed
			 * by this thread and zfs_zget is called from
			 * ZIL internals.
			 */
			VI_UNLOCK(vp);

			/*
			 * XXX vrele() locks the vnode when the last reference
			 * is dropped.  Although in this case the vnode is
			 * doomed / dead and so no inactivation is required,
			 * the vnode lock is still acquired.  That could result
			 * in a LOR with z_teardown_lock if another thread holds
			 * the vnode's lock and tries to take z_teardown_lock.
			 * But that is only possible if the other thread peforms
			 * a ZFS vnode operation on the vnode.  That either
			 * should not happen if the vnode is dead or the thread
			 * should also have a reference to the vnode and thus
			 * our reference is not last.
			 */
			VN_RELE(vp);
			goto again;
		}
		VI_UNLOCK(vp);
		getnewvnode_drop_reserve();
		return (err);
	}

	/*
	 * Not found create new znode/vnode
	 * but only if file exists.
	 *
	 * There is a small window where zfs_vget() could
	 * find this object while a file create is still in
	 * progress.  This is checked for in zfs_znode_alloc()
	 *
	 * if zfs_znode_alloc() fails it will drop the hold on the
	 * bonus buffer.
	 */
	zp = zfs_znode_alloc(zfsvfs, db, doi.doi_data_block_size,
	    doi.doi_bonus_type, NULL);
	if (zp == NULL) {
		err = SET_ERROR(ENOENT);
	} else {
		*zpp = zp;
	}
	if (err == 0) {
		vnode_t *vp = ZTOV(zp);

		err = insmntque(vp, zfsvfs->z_vfs);
		if (err == 0) {
			vp->v_hash = obj_num;
			VOP_UNLOCK1(vp);
		} else {
			zp->z_vnode = NULL;
			zfs_znode_dmu_fini(zp);
			zfs_znode_free(zp);
			*zpp = NULL;
		}
	}
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
	getnewvnode_drop_reserve();
	return (err);
}

int
zfs_rezget(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	dmu_object_info_t doi;
	dmu_buf_t *db;
	vnode_t *vp;
	uint64_t obj_num = zp->z_id;
	uint64_t mode, size;
	sa_bulk_attr_t bulk[8];
	int err;
	int count = 0;
	uint64_t gen;

	/*
	 * Remove cached pages before reloading the znode, so that they are not
	 * lingering after we run into any error.  Ideally, we should vgone()
	 * the vnode in case of error, but currently we cannot do that
	 * because of the LOR between the vnode lock and z_teardown_lock.
	 * So, instead, we have to "doom" the znode in the illumos style.
	 *
	 * Ignore invalid pages during the scan.  This is to avoid deadlocks
	 * between page busying and the teardown lock, as pages are busied prior
	 * to a VOP_GETPAGES operation, which acquires the teardown read lock.
	 * Such pages will be invalid and can safely be skipped here.
	 */
	vp = ZTOV(zp);
#if __FreeBSD_version >= 1400042
	vn_pages_remove_valid(vp, 0, 0);
#else
	vn_pages_remove(vp, 0, 0);
#endif

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num);

	mutex_enter(&zp->z_acl_lock);
	if (zp->z_acl_cached) {
		zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = NULL;
	}
	mutex_exit(&zp->z_acl_lock);

	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}
	rw_exit(&zp->z_xattr_lock);

	ASSERT3P(zp->z_sa_hdl, ==, NULL);
	err = sa_buf_hold(zfsvfs->z_os, obj_num, NULL, &db);
	if (err) {
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (err);
	}

	dmu_object_info_from_db(db, &doi);
	if (doi.doi_bonus_type != DMU_OT_SA &&
	    (doi.doi_bonus_type != DMU_OT_ZNODE ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t)))) {
		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (SET_ERROR(EINVAL));
	}

	zfs_znode_sa_init(zfsvfs, zp, db, doi.doi_bonus_type, NULL);
	size = zp->z_size;

	/* reload cached values */
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GEN(zfsvfs), NULL,
	    &gen, sizeof (gen));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &zp->z_size, sizeof (zp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &zp->z_links, sizeof (zp->z_links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
	    &zp->z_atime, sizeof (zp->z_atime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
	    &zp->z_uid, sizeof (zp->z_uid));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
	    &zp->z_gid, sizeof (zp->z_gid));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
	    &mode, sizeof (mode));

	if (sa_bulk_lookup(zp->z_sa_hdl, bulk, count)) {
		zfs_znode_dmu_fini(zp);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (SET_ERROR(EIO));
	}

	zp->z_mode = mode;

	if (gen != zp->z_gen) {
		zfs_znode_dmu_fini(zp);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (SET_ERROR(EIO));
	}

	/*
	 * It is highly improbable but still quite possible that two
	 * objects in different datasets are created with the same
	 * object numbers and in transaction groups with the same
	 * numbers.  znodes corresponding to those objects would
	 * have the same z_id and z_gen, but their other attributes
	 * may be different.
	 * zfs recv -F may replace one of such objects with the other.
	 * As a result file properties recorded in the replaced
	 * object's vnode may no longer match the received object's
	 * properties.  At present the only cached property is the
	 * files type recorded in v_type.
	 * So, handle this case by leaving the old vnode and znode
	 * disassociated from the actual object.  A new vnode and a
	 * znode will be created if the object is accessed
	 * (e.g. via a look-up).  The old vnode and znode will be
	 * recycled when the last vnode reference is dropped.
	 */
	if (vp->v_type != IFTOVT((mode_t)zp->z_mode)) {
		zfs_znode_dmu_fini(zp);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (SET_ERROR(EIO));
	}

	/*
	 * If the file has zero links, then it has been unlinked on the send
	 * side and it must be in the received unlinked set.
	 * We call zfs_znode_dmu_fini() now to prevent any accesses to the
	 * stale data and to prevent automatically removal of the file in
	 * zfs_zinactive().  The file will be removed either when it is removed
	 * on the send side and the next incremental stream is received or
	 * when the unlinked set gets processed.
	 */
	zp->z_unlinked = (zp->z_links == 0);
	if (zp->z_unlinked) {
		zfs_znode_dmu_fini(zp);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (0);
	}

	zp->z_blksz = doi.doi_data_block_size;
	if (zp->z_size != size)
		vnode_pager_setsize(vp, zp->z_size);

	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);

	return (0);
}

void
zfs_znode_delete(znode_t *zp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	objset_t *os = zfsvfs->z_os;
	uint64_t obj = zp->z_id;
	uint64_t acl_obj = zfs_external_acl(zp);

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj);
	if (acl_obj) {
		VERIFY(!zp->z_is_sa);
		VERIFY0(dmu_object_free(os, acl_obj, tx));
	}
	VERIFY0(dmu_object_free(os, obj, tx));
	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj);
	zfs_znode_free(zp);
}

void
zfs_zinactive(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t z_id = zp->z_id;

	ASSERT3P(zp->z_sa_hdl, !=, NULL);

	/*
	 * Don't allow a zfs_zget() while were trying to release this znode
	 */
	ZFS_OBJ_HOLD_ENTER(zfsvfs, z_id);

	/*
	 * If this was the last reference to a file with no links, remove
	 * the file from the file system unless the file system is mounted
	 * read-only.  That can happen, for example, if the file system was
	 * originally read-write, the file was opened, then unlinked and
	 * the file system was made read-only before the file was finally
	 * closed.  The file will remain in the unlinked set.
	 */
	if (zp->z_unlinked) {
		ASSERT(!zfsvfs->z_issnap);
		if ((zfsvfs->z_vfs->vfs_flag & VFS_RDONLY) == 0) {
			ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
			zfs_rmnode(zp);
			return;
		}
	}

	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
	zfs_znode_free(zp);
}

void
zfs_znode_free(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
#if __FreeBSD_version >= 1300139
	char *symlink;
#endif

	ASSERT3P(zp->z_sa_hdl, ==, NULL);
	zp->z_vnode = NULL;
	mutex_enter(&zfsvfs->z_znodes_lock);
	POINTER_INVALIDATE(&zp->z_zfsvfs);
	list_remove(&zfsvfs->z_all_znodes, zp);
	zfsvfs->z_nr_znodes--;
	mutex_exit(&zfsvfs->z_znodes_lock);

#if __FreeBSD_version >= 1300139
	symlink = atomic_load_ptr(&zp->z_cached_symlink);
	if (symlink != NULL) {
		atomic_store_rel_ptr((uintptr_t *)&zp->z_cached_symlink,
		    (uintptr_t)NULL);
		cache_symlink_free(symlink, strlen(symlink) + 1);
	}
#endif

	if (zp->z_acl_cached) {
		zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = NULL;
	}

	zfs_znode_free_kmem(zp);
}

void
zfs_tstamp_update_setup_ext(znode_t *zp, uint_t flag, uint64_t mtime[2],
    uint64_t ctime[2], boolean_t have_tx)
{
	timestruc_t	now;

	vfs_timestamp(&now);

	if (have_tx) {	/* will sa_bulk_update happen really soon? */
		zp->z_atime_dirty = 0;
		zp->z_seq++;
	} else {
		zp->z_atime_dirty = 1;
	}

	if (flag & AT_ATIME) {
		ZFS_TIME_ENCODE(&now, zp->z_atime);
	}

	if (flag & AT_MTIME) {
		ZFS_TIME_ENCODE(&now, mtime);
		if (zp->z_zfsvfs->z_use_fuids) {
			zp->z_pflags |= (ZFS_ARCHIVE |
			    ZFS_AV_MODIFIED);
		}
	}

	if (flag & AT_CTIME) {
		ZFS_TIME_ENCODE(&now, ctime);
		if (zp->z_zfsvfs->z_use_fuids)
			zp->z_pflags |= ZFS_ARCHIVE;
	}
}


void
zfs_tstamp_update_setup(znode_t *zp, uint_t flag, uint64_t mtime[2],
    uint64_t ctime[2])
{
	zfs_tstamp_update_setup_ext(zp, flag, mtime, ctime, B_TRUE);
}
/*
 * Grow the block size for a file.
 *
 *	IN:	zp	- znode of file to free data in.
 *		size	- requested block size
 *		tx	- open transaction.
 *
 * NOTE: this function assumes that the znode is write locked.
 */
void
zfs_grow_blocksize(znode_t *zp, uint64_t size, dmu_tx_t *tx)
{
	int		error;
	u_longlong_t	dummy;

	if (size <= zp->z_blksz)
		return;
	/*
	 * If the file size is already greater than the current blocksize,
	 * we will not grow.  If there is more than one block in a file,
	 * the blocksize cannot change.
	 */
	if (zp->z_blksz && zp->z_size > zp->z_blksz)
		return;

	error = dmu_object_set_blocksize(zp->z_zfsvfs->z_os, zp->z_id,
	    size, 0, tx);

	if (error == ENOTSUP)
		return;
	ASSERT0(error);

	/* What blocksize did we actually get? */
	dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &zp->z_blksz, &dummy);
}

/*
 * Increase the file length
 *
 *	IN:	zp	- znode of file to free data in.
 *		end	- new end-of-file
 *
 *	RETURN:	0 on success, error code on failure
 */
static int
zfs_extend(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	uint64_t newblksz;
	int error;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	lr = zfs_rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end <= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	if (end > zp->z_blksz &&
	    (!ISP2(zp->z_blksz) || zp->z_blksz < zfsvfs->z_max_blksz)) {
		/*
		 * We are growing the file past the current block size.
		 */
		if (zp->z_blksz > zp->z_zfsvfs->z_max_blksz) {
			/*
			 * File's blocksize is already larger than the
			 * "recordsize" property.  Only let it grow to
			 * the next power of 2.
			 */
			ASSERT(!ISP2(zp->z_blksz));
			newblksz = MIN(end, 1 << highbit64(zp->z_blksz));
		} else {
			newblksz = MIN(end, zp->z_zfsvfs->z_max_blksz);
		}
		dmu_tx_hold_write(tx, zp->z_id, 0, newblksz);
	} else {
		newblksz = 0;
	}

	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		return (error);
	}

	if (newblksz)
		zfs_grow_blocksize(zp, newblksz, tx);

	zp->z_size = end;

	VERIFY0(sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zp->z_zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx));

	vnode_pager_setsize(ZTOV(zp), end);

	zfs_rangelock_exit(lr);

	dmu_tx_commit(tx);

	return (0);
}

/*
 * Free space in a file.
 *
 *	IN:	zp	- znode of file to free data in.
 *		off	- start of section to free.
 *		len	- length of section to free.
 *
 *	RETURN:	0 on success, error code on failure
 */
static int
zfs_free_range(znode_t *zp, uint64_t off, uint64_t len)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	zfs_locked_range_t *lr;
	int error;

	/*
	 * Lock the range being freed.
	 */
	lr = zfs_rangelock_enter(&zp->z_rangelock, off, len, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (off >= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	if (off + len > zp->z_size)
		len = zp->z_size - off;

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, off, len);

	if (error == 0) {
#if __FreeBSD_version >= 1400032
		vnode_pager_purge_range(ZTOV(zp), off, off + len);
#else
		/*
		 * Before __FreeBSD_version 1400032 we cannot free block in the
		 * middle of a file, but only at the end of a file, so this code
		 * path should never happen.
		 */
		vnode_pager_setsize(ZTOV(zp), off);
#endif
	}

	zfs_rangelock_exit(lr);

	return (error);
}

/*
 * Truncate a file
 *
 *	IN:	zp	- znode of file to free data in.
 *		end	- new end-of-file.
 *
 *	RETURN:	0 on success, error code on failure
 */
static int
zfs_trunc(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	vnode_t *vp = ZTOV(zp);
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	int error;
	sa_bulk_attr_t bulk[2];
	int count = 0;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	lr = zfs_rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end >= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, end,
	    DMU_OBJECT_END);
	if (error) {
		zfs_rangelock_exit(lr);
		return (error);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		return (error);
	}

	zp->z_size = end;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &zp->z_size, sizeof (zp->z_size));

	if (end == 0) {
		zp->z_pflags &= ~ZFS_SPARSE;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &zp->z_pflags, 8);
	}
	VERIFY0(sa_bulk_update(zp->z_sa_hdl, bulk, count, tx));

	dmu_tx_commit(tx);

	/*
	 * Clear any mapped pages in the truncated region.  This has to
	 * happen outside of the transaction to avoid the possibility of
	 * a deadlock with someone trying to push a page that we are
	 * about to invalidate.
	 */
	vnode_pager_setsize(vp, end);

	zfs_rangelock_exit(lr);

	return (0);
}

/*
 * Free space in a file
 *
 *	IN:	zp	- znode of file to free data in.
 *		off	- start of range
 *		len	- end of range (0 => EOF)
 *		flag	- current file open mode flags.
 *		log	- TRUE if this action should be logged
 *
 *	RETURN:	0 on success, error code on failure
 */
int
zfs_freesp(znode_t *zp, uint64_t off, uint64_t len, int flag, boolean_t log)
{
	dmu_tx_t *tx;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	zilog_t *zilog = zfsvfs->z_log;
	uint64_t mode;
	uint64_t mtime[2], ctime[2];
	sa_bulk_attr_t bulk[3];
	int count = 0;
	int error;

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs), &mode,
	    sizeof (mode))) != 0)
		return (error);

	if (off > zp->z_size) {
		error =  zfs_extend(zp, off+len);
		if (error == 0 && log)
			goto log;
		else
			return (error);
	}

	if (len == 0) {
		error = zfs_trunc(zp, off);
	} else {
		if ((error = zfs_free_range(zp, off, len)) == 0 &&
		    off + len > zp->z_size)
			error = zfs_extend(zp, off+len);
	}
	if (error || !log)
		return (error);
log:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &zp->z_pflags, 8);
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	zfs_log_truncate(zilog, tx, TX_TRUNCATE, zp, off, len);

	dmu_tx_commit(tx);
	return (0);
}

void
zfs_create_fs(objset_t *os, cred_t *cr, nvlist_t *zplprops, dmu_tx_t *tx)
{
	uint64_t	moid, obj, sa_obj, version;
	uint64_t	sense = ZFS_CASE_SENSITIVE;
	uint64_t	norm = 0;
	nvpair_t	*elem;
	int		error;
	int		i;
	znode_t		*rootzp = NULL;
	zfsvfs_t	*zfsvfs;
	vattr_t		vattr;
	znode_t		*zp;
	zfs_acl_ids_t	acl_ids;

	/*
	 * First attempt to create master node.
	 */
	/*
	 * In an empty objset, there are no blocks to read and thus
	 * there can be no i/o errors (which we assert below).
	 */
	moid = MASTER_NODE_OBJ;
	error = zap_create_claim(os, moid, DMU_OT_MASTER_NODE,
	    DMU_OT_NONE, 0, tx);
	ASSERT0(error);

	/*
	 * Set starting attributes.
	 */
	version = zfs_zpl_version_map(spa_version(dmu_objset_spa(os)));
	elem = NULL;
	while ((elem = nvlist_next_nvpair(zplprops, elem)) != NULL) {
		/* For the moment we expect all zpl props to be uint64_ts */
		uint64_t val;
		char *name;

		ASSERT3S(nvpair_type(elem), ==, DATA_TYPE_UINT64);
		val = fnvpair_value_uint64(elem);
		name = nvpair_name(elem);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_VERSION)) == 0) {
			if (val < version)
				version = val;
		} else {
			error = zap_update(os, moid, name, 8, 1, &val, tx);
		}
		ASSERT0(error);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_NORMALIZE)) == 0)
			norm = val;
		else if (strcmp(name, zfs_prop_to_name(ZFS_PROP_CASE)) == 0)
			sense = val;
	}
	ASSERT3U(version, !=, 0);
	error = zap_update(os, moid, ZPL_VERSION_STR, 8, 1, &version, tx);

	/*
	 * Create zap object used for SA attribute registration
	 */

	if (version >= ZPL_VERSION_SA) {
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);
		error = zap_add(os, moid, ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		ASSERT0(error);
	} else {
		sa_obj = 0;
	}
	/*
	 * Create a delete queue.
	 */
	obj = zap_create(os, DMU_OT_UNLINKED_SET, DMU_OT_NONE, 0, tx);

	error = zap_add(os, moid, ZFS_UNLINKED_SET, 8, 1, &obj, tx);
	ASSERT0(error);

	/*
	 * Create root znode.  Create minimal znode/vnode/zfsvfs
	 * to allow zfs_mknode to work.
	 */
	VATTR_NULL(&vattr);
	vattr.va_mask = AT_MODE|AT_UID|AT_GID;
	vattr.va_type = VDIR;
	vattr.va_mode = S_IFDIR|0755;
	vattr.va_uid = crgetuid(cr);
	vattr.va_gid = crgetgid(cr);

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	rootzp = zfs_znode_alloc_kmem(KM_SLEEP);
	ASSERT(!POINTER_IS_VALID(rootzp->z_zfsvfs));
	rootzp->z_unlinked = 0;
	rootzp->z_atime_dirty = 0;
	rootzp->z_is_sa = USE_SA(version, os);

	zfsvfs->z_os = os;
	zfsvfs->z_parent = zfsvfs;
	zfsvfs->z_version = version;
	zfsvfs->z_use_fuids = USE_FUIDS(version, os);
	zfsvfs->z_use_sa = USE_SA(version, os);
	zfsvfs->z_norm = norm;

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);

	ASSERT0(error);

	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (sense == ZFS_CASE_INSENSITIVE || sense == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));

	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	rootzp->z_zfsvfs = zfsvfs;
	VERIFY0(zfs_acl_ids_create(rootzp, IS_ROOT_NODE, &vattr,
	    cr, NULL, &acl_ids));
	zfs_mknode(rootzp, &vattr, tx, cr, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, rootzp);
	error = zap_add(os, moid, ZFS_ROOT_OBJ, 8, 1, &rootzp->z_id, tx);
	ASSERT0(error);
	zfs_acl_ids_free(&acl_ids);
	POINTER_INVALIDATE(&rootzp->z_zfsvfs);

	sa_handle_destroy(rootzp->z_sa_hdl);
	zfs_znode_free_kmem(rootzp);

	/*
	 * Create shares directory
	 */

	error = zfs_create_share_dir(zfsvfs, tx);

	ASSERT0(error);

	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
}
#endif /* _KERNEL */

static int
zfs_sa_setup(objset_t *osp, sa_attr_type_t **sa_table)
{
	uint64_t sa_obj = 0;
	int error;

	error = zap_lookup(osp, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa_obj);
	if (error != 0 && error != ENOENT)
		return (error);

	error = sa_setup(osp, sa_obj, zfs_attr_table, ZPL_END, sa_table);
	return (error);
}

static int
zfs_grab_sa_handle(objset_t *osp, uint64_t obj, sa_handle_t **hdlp,
    dmu_buf_t **db, void *tag)
{
	dmu_object_info_t doi;
	int error;

	if ((error = sa_buf_hold(osp, obj, tag, db)) != 0)
		return (error);

	dmu_object_info_from_db(*db, &doi);
	if ((doi.doi_bonus_type != DMU_OT_SA &&
	    doi.doi_bonus_type != DMU_OT_ZNODE) ||
	    (doi.doi_bonus_type == DMU_OT_ZNODE &&
	    doi.doi_bonus_size < sizeof (znode_phys_t))) {
		sa_buf_rele(*db, tag);
		return (SET_ERROR(ENOTSUP));
	}

	error = sa_handle_get(osp, obj, NULL, SA_HDL_PRIVATE, hdlp);
	if (error != 0) {
		sa_buf_rele(*db, tag);
		return (error);
	}

	return (0);
}

static void
zfs_release_sa_handle(sa_handle_t *hdl, dmu_buf_t *db, void *tag)
{
	sa_handle_destroy(hdl);
	sa_buf_rele(db, tag);
}

/*
 * Given an object number, return its parent object number and whether
 * or not the object is an extended attribute directory.
 */
static int
zfs_obj_to_pobj(objset_t *osp, sa_handle_t *hdl, sa_attr_type_t *sa_table,
    uint64_t *pobjp, int *is_xattrdir)
{
	uint64_t parent;
	uint64_t pflags;
	uint64_t mode;
	uint64_t parent_mode;
	sa_bulk_attr_t bulk[3];
	sa_handle_t *sa_hdl;
	dmu_buf_t *sa_db;
	int count = 0;
	int error;

	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_PARENT], NULL,
	    &parent, sizeof (parent));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_FLAGS], NULL,
	    &pflags, sizeof (pflags));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_MODE], NULL,
	    &mode, sizeof (mode));

	if ((error = sa_bulk_lookup(hdl, bulk, count)) != 0)
		return (error);

	/*
	 * When a link is removed its parent pointer is not changed and will
	 * be invalid.  There are two cases where a link is removed but the
	 * file stays around, when it goes to the delete queue and when there
	 * are additional links.
	 */
	error = zfs_grab_sa_handle(osp, parent, &sa_hdl, &sa_db, FTAG);
	if (error != 0)
		return (error);

	error = sa_lookup(sa_hdl, ZPL_MODE, &parent_mode, sizeof (parent_mode));
	zfs_release_sa_handle(sa_hdl, sa_db, FTAG);
	if (error != 0)
		return (error);

	*is_xattrdir = ((pflags & ZFS_XATTR) != 0) && S_ISDIR(mode);

	/*
	 * Extended attributes can be applied to files, directories, etc.
	 * Otherwise the parent must be a directory.
	 */
	if (!*is_xattrdir && !S_ISDIR(parent_mode))
		return (SET_ERROR(EINVAL));

	*pobjp = parent;

	return (0);
}

/*
 * Given an object number, return some zpl level statistics
 */
static int
zfs_obj_to_stats_impl(sa_handle_t *hdl, sa_attr_type_t *sa_table,
    zfs_stat_t *sb)
{
	sa_bulk_attr_t bulk[4];
	int count = 0;

	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_MODE], NULL,
	    &sb->zs_mode, sizeof (sb->zs_mode));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_GEN], NULL,
	    &sb->zs_gen, sizeof (sb->zs_gen));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_LINKS], NULL,
	    &sb->zs_links, sizeof (sb->zs_links));
	SA_ADD_BULK_ATTR(bulk, count, sa_table[ZPL_CTIME], NULL,
	    &sb->zs_ctime, sizeof (sb->zs_ctime));

	return (sa_bulk_lookup(hdl, bulk, count));
}

static int
zfs_obj_to_path_impl(objset_t *osp, uint64_t obj, sa_handle_t *hdl,
    sa_attr_type_t *sa_table, char *buf, int len)
{
	sa_handle_t *sa_hdl;
	sa_handle_t *prevhdl = NULL;
	dmu_buf_t *prevdb = NULL;
	dmu_buf_t *sa_db = NULL;
	char *path = buf + len - 1;
	int error;

	*path = '\0';
	sa_hdl = hdl;

	uint64_t deleteq_obj;
	VERIFY0(zap_lookup(osp, MASTER_NODE_OBJ,
	    ZFS_UNLINKED_SET, sizeof (uint64_t), 1, &deleteq_obj));
	error = zap_lookup_int(osp, deleteq_obj, obj);
	if (error == 0) {
		return (ESTALE);
	} else if (error != ENOENT) {
		return (error);
	}
	error = 0;

	for (;;) {
		uint64_t pobj;
		char component[MAXNAMELEN + 2];
		size_t complen;
		int is_xattrdir;

		if (prevdb) {
			ASSERT3P(prevhdl, !=, NULL);
			zfs_release_sa_handle(prevhdl, prevdb, FTAG);
		}

		if ((error = zfs_obj_to_pobj(osp, sa_hdl, sa_table, &pobj,
		    &is_xattrdir)) != 0)
			break;

		if (pobj == obj) {
			if (path[0] != '/')
				*--path = '/';
			break;
		}

		component[0] = '/';
		if (is_xattrdir) {
			(void) sprintf(component + 1, "<xattrdir>");
		} else {
			error = zap_value_search(osp, pobj, obj,
			    ZFS_DIRENT_OBJ(-1ULL), component + 1);
			if (error != 0)
				break;
		}

		complen = strlen(component);
		path -= complen;
		ASSERT3P(path, >=, buf);
		bcopy(component, path, complen);
		obj = pobj;

		if (sa_hdl != hdl) {
			prevhdl = sa_hdl;
			prevdb = sa_db;
		}
		error = zfs_grab_sa_handle(osp, obj, &sa_hdl, &sa_db, FTAG);
		if (error != 0) {
			sa_hdl = prevhdl;
			sa_db = prevdb;
			break;
		}
	}

	if (sa_hdl != NULL && sa_hdl != hdl) {
		ASSERT3P(sa_db, !=, NULL);
		zfs_release_sa_handle(sa_hdl, sa_db, FTAG);
	}

	if (error == 0)
		(void) memmove(buf, path, buf + len - path);

	return (error);
}

int
zfs_obj_to_path(objset_t *osp, uint64_t obj, char *buf, int len)
{
	sa_attr_type_t *sa_table;
	sa_handle_t *hdl;
	dmu_buf_t *db;
	int error;

	error = zfs_sa_setup(osp, &sa_table);
	if (error != 0)
		return (error);

	error = zfs_grab_sa_handle(osp, obj, &hdl, &db, FTAG);
	if (error != 0)
		return (error);

	error = zfs_obj_to_path_impl(osp, obj, hdl, sa_table, buf, len);

	zfs_release_sa_handle(hdl, db, FTAG);
	return (error);
}

int
zfs_obj_to_stats(objset_t *osp, uint64_t obj, zfs_stat_t *sb,
    char *buf, int len)
{
	char *path = buf + len - 1;
	sa_attr_type_t *sa_table;
	sa_handle_t *hdl;
	dmu_buf_t *db;
	int error;

	*path = '\0';

	error = zfs_sa_setup(osp, &sa_table);
	if (error != 0)
		return (error);

	error = zfs_grab_sa_handle(osp, obj, &hdl, &db, FTAG);
	if (error != 0)
		return (error);

	error = zfs_obj_to_stats_impl(hdl, sa_table, sb);
	if (error != 0) {
		zfs_release_sa_handle(hdl, db, FTAG);
		return (error);
	}

	error = zfs_obj_to_path_impl(osp, obj, hdl, sa_table, buf, len);

	zfs_release_sa_handle(hdl, db, FTAG);
	return (error);
}


void
zfs_znode_update_vfs(znode_t *zp)
{
	vm_object_t object;

	if ((object = ZTOV(zp)->v_object) == NULL ||
	    zp->z_size == object->un_pager.vnp.vnp_size)
		return;

	vnode_pager_setsize(ZTOV(zp), zp->z_size);
}


#ifdef _KERNEL
int
zfs_znode_parent_and_name(znode_t *zp, znode_t **dzpp, char *buf)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t parent;
	int is_xattrdir;
	int err;

	/* Extended attributes should not be visible as regular files. */
	if ((zp->z_pflags & ZFS_XATTR) != 0)
		return (SET_ERROR(EINVAL));

	err = zfs_obj_to_pobj(zfsvfs->z_os, zp->z_sa_hdl, zfsvfs->z_attr_table,
	    &parent, &is_xattrdir);
	if (err != 0)
		return (err);
	ASSERT0(is_xattrdir);

	/* No name as this is a root object. */
	if (parent == zp->z_id)
		return (SET_ERROR(EINVAL));

	err = zap_value_search(zfsvfs->z_os, parent, zp->z_id,
	    ZFS_DIRENT_OBJ(-1ULL), buf);
	if (err != 0)
		return (err);
	err = zfs_zget(zfsvfs, parent, dzpp);
	return (err);
}
#endif /* _KERNEL */
