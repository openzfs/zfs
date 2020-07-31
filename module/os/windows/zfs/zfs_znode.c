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
 * Portions Copyright 2007-2009 Apple Inc. All rights reserved.
 * Use is subject to license terms.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 */

/* Portions Copyright 2007 Jeremy Teo */
/* Portions Copyright 2011 Martin Matuska <mm@FreeBSD.org> */
/* Portions Copyright 2013 Jorgen Lundman <lundman@lundman.net> */

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
#include <sys/kidmap.h>
#include <sys/zfs_vnops.h>
#endif /* _KERNEL */

#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/refcount.h>
#include <sys/stat.h>
#include <sys/zap.h>
#include <sys/zfs_znode.h>
#include <sys/sa.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_stat.h>

#include "zfs_prop.h"
#include "zfs_comutil.h"

/* Used by fstat(1). */
#ifndef _WIN32
SYSCTL_INT(_debug_sizeof, OID_AUTO, znode, CTLFLAG_RD, 0, sizeof (znode_t),
			"sizeof(znode_t)");
#endif
void
zfs_release_sa_handle(sa_handle_t *hdl, dmu_buf_t *db, void *tag);


// #define dprintf printf


/*
 * Functions needed for userland (ie: libzpool) are not put under
 * #ifdef_KERNEL; the rest of the functions have dependencies
 * (such as VFS logic) that will not compile easily in userland.
 */
#ifdef _KERNEL
/*
 * Needed to close a small window in zfs_znode_move() that allows the zfsvfs to
 * be freed before it can be safely accessed.
 */
krwlock_t zfsvfs_lock;

/*
 * This is used by the test suite so that it can delay znodes from being
 * freed in order to inspect the unlinked set.
 */
int zfs_unlink_suspend_progress = 0;

/*
 * This callback is invoked when acquiring a RL_WRITER or RL_APPEND lock on
 * z_rangelock. It will modify the offset and length of the lock to reflect
 * znode-specific information, and convert RL_APPEND to RL_WRITER.  This is
 * called with the rangelock_t's rl_lock held, which avoids races.
 */

kmem_cache_t *znode_cache = NULL;

/*
 * This callback is invoked when acquiring a RL_WRITER or RL_APPEND lock on
 * z_rangelock. It will modify the offset and length of the lock to reflect
 * znode-specific information, and convert RL_APPEND to RL_WRITER.  This is
 * called with the rangelock_t's rl_lock held, which avoids races.
 */
static void
zfs_rangelock_cb(locked_range_t *new, void *arg)
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
	    zp->z_blksz < zp->z_zfsvfs->z_max_blksz)) {
		new->lr_offset = 0;
		new->lr_length = UINT64_MAX;
	}
}

/*ARGSUSED*/
#if 0 // unused function
static void
znode_evict_error(dmu_buf_t *dbuf, void *user_ptr)
{
	/*
	 * We should never drop all dbuf refs without first clearing
	 * the eviction callback.
	 */
	panic("evicting znode %p\n", user_ptr);
}
#endif

extern struct vop_vector zfs_vnodeops;
extern struct vop_vector zfs_fifoops;
extern struct vop_vector zfs_shareops;

/*
 * XXX: We cannot use this function as a cache constructor, because
 *      there is one global cache for all file systems and we need
 *      to pass vfsp here, which is not possible, because argument
 *      'cdrarg' is defined at kmem_cache_create() time.
 */
/*ARGSUSED*/
static int
zfs_znode_cache_constructor(void *buf, void *arg, int kmflags)
{
	znode_t *zp = buf;
#ifndef _WIN32
	vnode_t *vp;
	vfs_t *vfsp = arg;
	int error;
#endif

	bzero(zp, sizeof(znode_t));

	POINTER_INVALIDATE(&zp->z_zfsvfs);
	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs));

#ifndef _WIN32
	if (vfsp != NULL) {

		/*
		 * OSX can only set v_type in the vnode_create call, so we
		 * need to know now what we will be creating. So this call
		 * moved further down. We should consider restoring
		 * cache_constructor to the old automatic method.
		 */
		error = getnewvnode("zfs", arg, &zfs_vnodeops, &vp,
		    IFTOVT((mode_t)zp->z_mode));
		if (error != 0 && (kmflags & KM_NOSLEEP))
			return (-1);
		ASSERT(error == 0);
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		zp->z_vnode = vp;
		vp->v_data = (caddr_t)zp;
		VN_LOCK_AREC(vp);
		VN_LOCK_ASHARE(vp);
	} else {
		zp->z_vnode = NULL;
	}
#endif

	list_link_init(&zp->z_link_node);

	mutex_init(&zp->z_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zp->z_map_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zp->z_parent_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zp->z_name_lock, NULL, RW_DEFAULT, NULL);
	mutex_init(&zp->z_acl_lock, NULL, MUTEX_DEFAULT, NULL);
	rw_init(&zp->z_xattr_lock, NULL, RW_DEFAULT, NULL);
	rangelock_init(&zp->z_rangelock, zfs_rangelock_cb, zp);

	zp->z_dirlocks = NULL;
	zp->z_acl_cached = NULL;
	zp->z_xattr_cached = NULL;
	zp->z_moved = 0;
	zp->z_fastpath = B_FALSE;
	return (0);
}

/*ARGSUSED*/
static void
zfs_znode_cache_destructor(void *buf, void *arg)
{
	znode_t *zp = buf;
	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs));
	ASSERT(ZTOV(zp) == NULL);
	vn_free(ZTOV(zp));
	ASSERT(!list_link_active(&zp->z_link_node));
	mutex_destroy(&zp->z_lock);
	rw_destroy(&zp->z_map_lock);
	rw_destroy(&zp->z_parent_lock);
	rw_destroy(&zp->z_name_lock);
	mutex_destroy(&zp->z_acl_lock);
	rw_destroy(&zp->z_xattr_lock);
	rangelock_fini(&zp->z_rangelock);

	ASSERT(zp->z_dirlocks == NULL);
	ASSERT(zp->z_acl_cached == NULL);
	ASSERT(zp->z_xattr_cached == NULL);
}

#ifdef sun
static void
zfs_znode_move_impl(znode_t *ozp, znode_t *nzp)
{
	vnode_t *vp;

	/* Copy fields. */
	nzp->z_zfsvfs = ozp->z_zfsvfs;

	/* Swap vnodes. */
	vp = nzp->z_vnode;
	nzp->z_vnode = ozp->z_vnode;
	ozp->z_vnode = vp; /* let destructor free the overwritten vnode */
	ZTOV(ozp)->v_data = ozp;
	ZTOV(nzp)->v_data = nzp;

	nzp->z_id = ozp->z_id;
	ASSERT(ozp->z_dirlocks == NULL); /* znode not in use */
	ASSERT(avl_numnodes(&ozp->z_range_avl) == 0);
	nzp->z_unlinked = ozp->z_unlinked;
	nzp->z_atime_dirty = ozp->z_atime_dirty;
	nzp->z_zn_prefetch = ozp->z_zn_prefetch;
	nzp->z_blksz = ozp->z_blksz;
	nzp->z_seq = ozp->z_seq;
	nzp->z_mapcnt = ozp->z_mapcnt;
	nzp->z_gen = ozp->z_gen;
	nzp->z_sync_cnt = ozp->z_sync_cnt;
	nzp->z_is_sa = ozp->z_is_sa;
	nzp->z_sa_hdl = ozp->z_sa_hdl;
	bcopy(ozp->z_atime, nzp->z_atime, sizeof (uint64_t) * 2);
	nzp->z_links = ozp->z_links;
	nzp->z_size = ozp->z_size;
	nzp->z_pflags = ozp->z_pflags;
	nzp->z_uid = ozp->z_uid;
	nzp->z_gid = ozp->z_gid;
	nzp->z_mode = ozp->z_mode;

	/*
	 * Since this is just an idle znode and kmem is already dealing with
	 * memory pressure, release any cached ACL.
	 */
	if (ozp->z_acl_cached) {
		zfs_acl_free(ozp->z_acl_cached);
		ozp->z_acl_cached = NULL;
	}

	sa_set_userp(nzp->z_sa_hdl, nzp);

	/*
	 * Invalidate the original znode by clearing fields that provide a
	 * pointer back to the znode. Set the low bit of the vfs pointer to
	 * ensure that zfs_znode_move() recognizes the znode as invalid in any
	 * subsequent callback.
	 */
	ozp->z_sa_hdl = NULL;
	POINTER_INVALIDATE(&ozp->z_zfsvfs);

	/*
	 * Mark the znode.
	 */
	nzp->z_moved = 1;
	ozp->z_moved = (uint8_t)-1;
}

/*ARGSUSED*/
static kmem_cbrc_t
zfs_znode_move(void *buf, void *newbuf, size_t size, void *arg)
{
	znode_t *ozp = buf, *nzp = newbuf;
	zfsvfs_t *zfsvfs;
	vnode_t *vp;

	/*
	 * The znode is on the file system's list of known znodes if the vfs
	 * pointer is valid. We set the low bit of the vfs pointer when freeing
	 * the znode to invalidate it, and the memory patterns written by kmem
	 * (baddcafe and deadbeef) set at least one of the two low bits. A newly
	 * created znode sets the vfs pointer last of all to indicate that the
	 * znode is known and in a valid state to be moved by this function.
	 */
	zfsvfs = ozp->z_zfsvfs;
	if (!POINTER_IS_VALID(zfsvfs)) {
		ZNODE_STAT_ADD(znode_move_stats.zms_zfsvfs_invalid);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * Close a small window in which it's possible that the filesystem could
	 * be unmounted and freed, and zfsvfs, though valid in the previous
	 * statement, could point to unrelated memory by the time we try to
	 * prevent the filesystem from being unmounted.
	 */
	rw_enter(&zfsvfs_lock, RW_WRITER);
	if (zfsvfs != ozp->z_zfsvfs) {
		rw_exit(&zfsvfs_lock);
		ZNODE_STAT_ADD(znode_move_stats.zms_zfsvfs_recheck1);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * If the znode is still valid, then so is the file system. We know that
	 * no valid file system can be freed while we hold zfsvfs_lock, so we
	 * can safely ensure that the filesystem is not and will not be
	 * unmounted. The next statement is equivalent to ZFS_ENTER().
	 */
	rrm_enter(&zfsvfs->z_teardown_lock, RW_READER, FTAG);
	if (zfsvfs->z_unmounted) {
		ZFS_EXIT(zfsvfs);
		rw_exit(&zfsvfs_lock);
		ZNODE_STAT_ADD(znode_move_stats.zms_zfsvfs_unmounted);
		return (KMEM_CBRC_DONT_KNOW);
	}
	rw_exit(&zfsvfs_lock);

	mutex_enter(&zfsvfs->z_znodes_lock);
	/*
	 * Recheck the vfs pointer in case the znode was removed just before
	 * acquiring the lock.
	 */
	if (zfsvfs != ozp->z_zfsvfs) {
		mutex_exit(&zfsvfs->z_znodes_lock);
		ZFS_EXIT(zfsvfs);
		ZNODE_STAT_ADD(znode_move_stats.zms_zfsvfs_recheck2);
		return (KMEM_CBRC_DONT_KNOW);
	}

	/*
	 * At this point we know that as long as we hold z_znodes_lock, the
	 * znode cannot be freed and fields within the znode can be safely
	 * accessed. Now, prevent a race with zfs_zget().
	 */
	if (ZFS_OBJ_HOLD_TRYENTER(zfsvfs, ozp->z_id) == 0) {
		mutex_exit(&zfsvfs->z_znodes_lock);
		ZFS_EXIT(zfsvfs);
		ZNODE_STAT_ADD(znode_move_stats.zms_obj_held);
		return (KMEM_CBRC_LATER);
	}

	vp = ZTOV(ozp);
	if (mutex_tryenter(&vp->v_lock) == 0) {
		ZFS_OBJ_HOLD_EXIT(zfsvfs, ozp->z_id);
		mutex_exit(&zfsvfs->z_znodes_lock);
		ZFS_EXIT(zfsvfs);
		ZNODE_STAT_ADD(znode_move_stats.zms_vnode_locked);
		return (KMEM_CBRC_LATER);
	}

	/* Only move znodes that are referenced _only_ by the DNLC. */
	if (vp->v_count != 1 || !vn_in_dnlc(vp)) {
		mutex_exit(&vp->v_lock);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, ozp->z_id);
		mutex_exit(&zfsvfs->z_znodes_lock);
		ZFS_EXIT(zfsvfs);
		ZNODE_STAT_ADD(znode_move_stats.zms_not_only_dnlc);
		return (KMEM_CBRC_LATER);
	}

	/*
	 * The znode is known and in a valid state to move. We're holding the
	 * locks needed to execute the critical section.
	 */
	zfs_znode_move_impl(ozp, nzp);
	mutex_exit(&vp->v_lock);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, ozp->z_id);

	list_link_replace(&ozp->z_link_node, &nzp->z_link_node);
	mutex_exit(&zfsvfs->z_znodes_lock);
	ZFS_EXIT(zfsvfs);

	return (KMEM_CBRC_YES);
}
#endif /* sun */

void
zfs_znode_init(void)
{
	/*
	 * Initialize zcache.  The KMC_SLAB hint is used in order that it be
	 * backed by kmalloc() when on the Linux slab in order that any
	 * wait_on_bit() operations on the related inode operate properly.
	 */
	rw_init(&zfsvfs_lock, NULL, RW_DEFAULT, NULL);
	ASSERT(znode_cache == NULL);
	znode_cache = kmem_cache_create("zfs_znode_cache",
	    sizeof (znode_t), 0,
		zfs_znode_cache_constructor,
	    zfs_znode_cache_destructor, NULL, NULL,
	    NULL, 0);

	// BGH - dont support move semantics here yet.
	// zfs_znode_move() requires porting
	//kmem_cache_set_move(znode_cache, zfs_znode_move);
}

void
zfs_znode_fini(void)
{
#ifdef sun
	/*
	 * Cleanup vfs & vnode ops
	 */
	zfs_remove_op_tables();
#endif	/* sun */

	/*
	 * Cleanup zcache
	 */
	if (znode_cache)
		kmem_cache_destroy(znode_cache);
	znode_cache = NULL;
	rw_destroy(&zfsvfs_lock);
}

#ifdef sun
struct vnodeops *zfs_dvnodeops;
struct vnodeops *zfs_fvnodeops;
struct vnodeops *zfs_symvnodeops;
struct vnodeops *zfs_xdvnodeops;
struct vnodeops *zfs_evnodeops;
struct vnodeops *zfs_sharevnodeops;

void
zfs_remove_op_tables()
{
	/*
	 * Remove vfs ops
	 */
	ASSERT(zfsfstype);
	(void) vfs_freevfsops_by_type(zfsfstype);
	zfsfstype = 0;

	/*
	 * Remove vnode ops
	 */
	if (zfs_dvnodeops)
		vn_freevnodeops(zfs_dvnodeops);
	if (zfs_fvnodeops)
		vn_freevnodeops(zfs_fvnodeops);
	if (zfs_symvnodeops)
		vn_freevnodeops(zfs_symvnodeops);
	if (zfs_xdvnodeops)
		vn_freevnodeops(zfs_xdvnodeops);
	if (zfs_evnodeops)
		vn_freevnodeops(zfs_evnodeops);
	if (zfs_sharevnodeops)
		vn_freevnodeops(zfs_sharevnodeops);

	zfs_dvnodeops = NULL;
	zfs_fvnodeops = NULL;
	zfs_symvnodeops = NULL;
	zfs_xdvnodeops = NULL;
	zfs_evnodeops = NULL;
	zfs_sharevnodeops = NULL;
}

extern const fs_operation_def_t zfs_dvnodeops_template[];
extern const fs_operation_def_t zfs_fvnodeops_template[];
extern const fs_operation_def_t zfs_xdvnodeops_template[];
extern const fs_operation_def_t zfs_symvnodeops_template[];
extern const fs_operation_def_t zfs_evnodeops_template[];
extern const fs_operation_def_t zfs_sharevnodeops_template[];

int
zfs_create_op_tables()
{
	int error;

	/*
	 * zfs_dvnodeops can be set if mod_remove() calls mod_installfs()
	 * due to a failure to remove the the 2nd modlinkage (zfs_modldrv).
	 * In this case we just return as the ops vectors are already set up.
	 */
	if (zfs_dvnodeops)
		return (0);

	error = vn_make_ops(MNTTYPE_ZFS, zfs_dvnodeops_template,
	    &zfs_dvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_ZFS, zfs_fvnodeops_template,
	    &zfs_fvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_ZFS, zfs_symvnodeops_template,
	    &zfs_symvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_ZFS, zfs_xdvnodeops_template,
	    &zfs_xdvnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_ZFS, zfs_evnodeops_template,
	    &zfs_evnodeops);
	if (error)
		return (error);

	error = vn_make_ops(MNTTYPE_ZFS, zfs_sharevnodeops_template,
	    &zfs_sharevnodeops);

	return (error);
}
#endif	/* sun */

int
zfs_create_share_dir(zfsvfs_t *zfsvfs, dmu_tx_t *tx)
{
	int error = 0;
#if 0 // FIXME, uses vnode struct, not ptr
	zfs_acl_ids_t acl_ids;
	vattr_t vattr;
	znode_t *sharezp;
	struct vnode *vp, *vnode;
	znode_t *zp;

	vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
	vattr.va_type = VDIR;
	vattr.va_mode = S_IFDIR|0555;
	vattr.va_uid = crgetuid(kcred);
	vattr.va_gid = crgetgid(kcred);

	sharezp = kmem_cache_alloc(znode_cache, KM_SLEEP);
	sharezp->z_moved = 0;
	sharezp->z_unlinked = 0;
	sharezp->z_atime_dirty = 0;
	sharezp->z_zfsvfs = zfsvfs;
	sharezp->z_is_sa = zfsvfs->z_use_sa;

	sharezp->z_vnode = vnode;
	vnode.v_data = sharezp;

	vp = ZTOV(sharezp);
	vp->v_type = VDIR;

	VERIFY(0 == zfs_acl_ids_create(sharezp, IS_ROOT_NODE, &vattr,
	    kcred, NULL, &acl_ids));
	zfs_mknode(sharezp, &vattr, tx, kcred, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, sharezp);
	POINTER_INVALIDATE(&sharezp->z_zfsvfs);
	error = zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
	    ZFS_SHARES_DIR, 8, 1, &sharezp->z_id, tx);
	zfsvfs->z_shares_dir = sharezp->z_id;

	zfs_acl_ids_free(&acl_ids);
	ZTOV(sharezp)->v_data = NULL;
	ZTOV(sharezp)->v_count = 0;
	ZTOV(sharezp)->v_holdcnt = 0;
	zp->z_vnode = NULL;
	sa_handle_destroy(sharezp->z_sa_hdl);
	sharezp->z_vnode = NULL;
	kmem_cache_free(znode_cache, sharezp);
#endif
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
#if 0
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
#endif

static void
zfs_znode_sa_init(zfsvfs_t *zfsvfs, znode_t *zp,
				    dmu_buf_t *db, dmu_object_type_t obj_type,
				    sa_handle_t *sa_hdl)
{
	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs) || (zfsvfs == zp->z_zfsvfs));
	ASSERT(MUTEX_HELD(ZFS_OBJ_MUTEX(zfsvfs, zp->z_id)));

	mutex_enter(&zp->z_lock);

	ASSERT(zp->z_sa_hdl == NULL);
	ASSERT(zp->z_acl_cached == NULL);
	if (sa_hdl == NULL) {
		VERIFY(0 == sa_handle_get_from_db(zfsvfs->z_os, db, zp,
		    SA_HDL_SHARED, &zp->z_sa_hdl));
	} else {
		zp->z_sa_hdl = sa_hdl;
		sa_set_userp(sa_hdl, zp);
	}

	zp->z_is_sa = (obj_type == DMU_OT_SA) ? B_TRUE : B_FALSE;

#ifndef _WIN32
	/*
	 * Slap on VROOT if we are the root znode
	 */
	if (zp->z_id == zfsvfs->z_root)
		ZTOV(zp)->v_flag |= VROOT;
#endif

	mutex_exit(&zp->z_lock);
	vn_exists(ZTOV(zp));
}

void
zfs_znode_dmu_fini(znode_t *zp)
{
	ASSERT(MUTEX_HELD(ZFS_OBJ_MUTEX(zp->z_zfsvfs, zp->z_id)) ||
	    zp->z_unlinked ||
	    RW_WRITE_HELD(&zp->z_zfsvfs->z_teardown_inactive_lock));

	sa_handle_destroy(zp->z_sa_hdl);
	zp->z_sa_hdl = NULL;
}

static void
zfs_vnode_forget(struct vnode *vp)
{

	/* copied from insmntque_stddtr */
	if (vp) {
		vnode_clearfsnode(vp);
		VN_RELE(vp);
		vnode_recycle(vp);
	}
}

/*
 * Construct a new znode/vnode and intialize.
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
	struct vnode *vp;
	uint64_t mode;
	uint64_t parent;
	sa_bulk_attr_t bulk[9];
	int count = 0;

	zp = kmem_cache_alloc(znode_cache, KM_SLEEP);
	//zfs_znode_cache_constructor(zp, zfsvfs->z_parent->z_vfs, 0);

	ASSERT(zp->z_dirlocks == NULL);
	ASSERT(!POINTER_IS_VALID(zp->z_zfsvfs));
	zp->z_moved = 0;

	/*
	 * Defer setting z_zfsvfs until the znode is ready to be a candidate for
	 * the zfs_znode_move() callback.
	 */
	zp->z_vnode = NULL;
	zp->z_sa_hdl = NULL;
	zp->z_unlinked = 0;
	zp->z_atime_dirty = 0;
	zp->z_mapcnt = 0;
	zp->z_id = db->db_object;
	zp->z_blksz = blksz;
	zp->z_seq = 0x7A4653;
	zp->z_sync_cnt = 0;

	zp->z_is_zvol = 0;
	zp->z_is_mapped = 0;
	zp->z_is_ctldir = 0;
	zp->z_vid = 0;
	zp->z_uid = 0;
	zp->z_gid = 0;
	zp->z_size = 0;
	zp->z_name_cache = NULL;
	zp->z_fastpath = B_FALSE;

	vp = ZTOV(zp); /* Does nothing in OSX */

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
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
	    &zp->z_uid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
	    &zp->z_gid, 8);

	if (sa_bulk_lookup(zp->z_sa_hdl, bulk, count) != 0 || zp->z_gen == 0) {
		if (hdl == NULL)
			sa_handle_destroy(zp->z_sa_hdl);
		dprintf("znode_alloc: sa_bulk_lookup failed - aborting\n");
		//zfs_vnode_forget(vp);
		//zp->z_vnode = NULL;
		kmem_cache_free(znode_cache, zp);
		return (NULL);
	}

	zp->z_mode = mode;

#ifndef _WIN32
	vp->v_type = IFTOVT((mode_t)mode);

	switch (vp->v_type) {
	case VDIR:

		zp->z_zn_prefetch = B_TRUE; /* z_prefetch default is enabled */
		break;
#ifdef sun
	case VBLK:
	case VCHR:
		{
			uint64_t rdev;
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_RDEV(zfsvfs),
			    &rdev, sizeof (rdev)) == 0);

			vp->v_rdev = zfs_cmpldev(rdev);
		}
		break;
#endif	/* sun */
	case VFIFO:
#ifdef sun
	case VSOCK:
	case VDOOR:
#endif	/* sun */
		vp->v_op = &zfs_fifoops;
		break;
	case VREG:
		if (parent == zfsvfs->z_shares_dir) {
			ASSERT(zp->z_uid == 0 && zp->z_gid == 0);
			vp->v_op = &zfs_shareops;
		}
		break;
#ifdef sun
	case VLNK:
		vn_setops(vp, zfs_symvnodeops);
		break;
	default:
		vn_setops(vp, zfs_evnodeops);
		break;
#endif	/* sun */
	}

	if (vp->v_type != VFIFO)
		VN_LOCK_ASHARE(vp);

#else /* APPLE */

#ifdef APPLE_SA_RECOVER
	/*
	 * We have had some SA corruption, making for invalid entries. We
	 * attempt to handle this situation here, by not creating invalid
	 * type vnodes.
	 */
	if (zfs_recover) {
		if (( IFTOVT((mode_t)mode) == VNON) ||
			( IFTOVT((mode_t)mode) > VCPLX)) {

			printf("ZFS: WARNING! objid %llu has invalid SA data, please restore from backup. (mode %x)\n",
				   zp->z_id, (int)zp->z_mode);

			zp->z_mode = 0;

			uint64_t parent = zfsvfs->z_recover_parent;
			if (parent)	{

				zap_cursor_t zc;
				zap_attribute_t *za;
				int err;
				uint64_t mask =  ZFS_DIRENT_OBJ(-1ULL);

				za = kmem_alloc(sizeof (zap_attribute_t), KM_SLEEP);
				for (zap_cursor_init(&zc, zfsvfs->z_os, parent);
					 (err = zap_cursor_retrieve(&zc, za)) == 0;
					 zap_cursor_advance(&zc)) {
					if ((za->za_first_integer & mask) == (zp->z_id & mask)) {
						uint32_t vtype = DTTOVT(ZFS_DIRENT_TYPE(za->za_first_integer));
						printf("ZFS: correct vtype is %d\n", vtype);
						zp->z_mode = VTTOIF(vtype);
						zp->z_size = 0;
						break;
                }
				}
				zap_cursor_fini(&zc);
				kmem_free(za, sizeof (zap_attribute_t));
			}
			// Last ditch effort
			if (!zp->z_mode) zp->z_mode = VTTOIF(VREG);

#include <sys/dbuf.h>

		dmu_buf_impl_t *db2 = (dmu_buf_impl_t *)db;
		zbookmark_phys_t zb;

        // Log error in spa?

		SET_BOOKMARK(&zb, db2->db_objset->os_dsl_dataset ?
					 db2->db_objset->os_dsl_dataset->ds_object :
					 DMU_META_OBJSET,
					 db2->db.db_object, db2->db_level, db2->db_blkid);

		spa_log_error(db2->db_objset->os_spa, &zb);

		} // bad vtype

	} // zfs_recover

#endif /* APPLE_SA_RECOVER */



#endif /* Apple */

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	membar_producer();
	/*
	 * Everything else must be valid before assigning z_zfsvfs makes the
	 * znode eligible for zfs_znode_move().
	 */
	zp->z_zfsvfs = zfsvfs;
	mutex_exit(&zfsvfs->z_znodes_lock);

	// ZOL-0.6.2 calls
	// dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &blksize,
	//    (u_longlong_t *)&ip->i_blocks);

	VFS_HOLD(zfsvfs->z_vfs);
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
 * OS X implementation notes:
 *
 * The caller of zfs_mknode() is expected to call zfs_znode_getvnode()
 * AFTER the dmu_tx_commit() is performed.  This prevents deadlocks
 * since vnode_create can indirectly attempt to clean a dirty vnode.
 *
 * The current list of callers includes:
 *      zfs_vnop_create
 *      zfs_vnop_mkdir
 *      zfs_vnop_symlink
 *      zfs_obtain_xattr
 *      zfs_make_xattrdir
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
	dmu_buf_t	*db = NULL;
	timestruc_t	now;
	uint64_t	gen, obj;
	int		bonuslen;
	sa_handle_t	*sa_hdl = NULL;
	int		dnodesize;
	dmu_object_type_t obj_type;
	sa_bulk_attr_t  *sa_attrs;
	int		cnt = 0;
	zfs_acl_locator_cb_t locate = { 0 };
	int err = 0;

	ASSERT(vap && (vap->va_mask & (AT_TYPE|AT_MODE)) == (AT_TYPE|AT_MODE));

	if (zfsvfs->z_replay) {
		obj = vap->va_nodeid;
		now = vap->va_ctime;		/* see zfs_replay_create() */
		gen = vap->va_nblocks;		/* ditto */
		dnodesize = vap->va_fsid;	/* ditto */
	} else {
		obj = 0;
		gethrestime(&now);
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

	getnewvnode_reserve(1);
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
	//	rdev = zfs_expldev(vap->va_rdev);
	}

	parent = dzp->z_id;
	mode = acl_ids->z_mode;
	if (flag & IS_XATTR)
		pflags |= ZFS_XATTR;

	/*
	 * No execs denied will be deterimed when zfs_mode_compute() is called.
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
	VERIFY(0 == sa_handle_get_from_db(zfsvfs->z_os, db, NULL, SA_HDL_SHARED,
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

	VERIFY(sa_replace_all_by_template(sa_hdl, sa_attrs, cnt, tx) == 0);

	if (!(flag & IS_ROOT_NODE)) {
		/*
		 * We must not hold any locks while calling vnode_create inside
		 * zfs_znode_alloc(), as it may call either of vnop_reclaim, or
		 * vnop_fsync. If it is not enough to just release ZFS_OBJ_HOLD
		 * we will have to attach the vnode after the dmu_commit like
		 * maczfs does, in each vnop caller.
		 */
		*zpp = zfs_znode_alloc(zfsvfs, db, 0, obj_type, sa_hdl);
		ASSERT(*zpp != NULL);
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
		err = zfs_aclset_common(*zpp, acl_ids->z_aclp, cr, tx);
		ASSERT(err == 0);
	}
#ifndef _WIN32
	if (!(flag & IS_ROOT_NODE)) {
		struct vnode *vp;

		vp = ZTOV(*zpp);
		vp->v_vflag |= VV_FORCEINSMQ;
		err = insmntque(vp, zfsvfs->z_vfs);
		vp->v_vflag &= ~VV_FORCEINSMQ;
		KASSERT(err == 0, ("insmntque() failed: error %d", err));
	}
#endif

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
	ASSERT(xoap);

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
zfs_zget_ext(zfsvfs_t *zfsvfs, uint64_t obj_num, znode_t **zpp,
			 int flags)
{
	dmu_object_info_t doi;
	dmu_buf_t	*db;
	znode_t		*zp;
	struct vnode		*vp = NULL;
	sa_handle_t	*hdl;
	int err;
	uint64_t        vid;
	int crutch_count = 0;
	//dprintf("+zget %lld\n", obj_num);

	getnewvnode_reserve(1);

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
		return ((EINVAL));
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

		mutex_enter(&zp->z_lock);

		/*
		 * Since zp may disappear after we unlock below,
		 * we save a copy of vp and it's vid
		 */
		vid = zp->z_vid;
		vp = ZTOV(zp);

		/*
		 * Since we do immediate eviction of the z_dbuf, we
		 * should never find a dbuf with a znode that doesn't
		 * know about the dbuf.
		 */
		ASSERT3U(zp->z_id, ==, obj_num);

		/*
		 * OS X can return the znode when the file is unlinked
		 * in order to support the sync of open-unlinked files
		 */
		if (!(flags & ZGET_FLAG_UNLINKED) && zp->z_unlinked) {
			mutex_exit(&zp->z_lock);
			sa_buf_rele(db, NULL);
			ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
			return (ENOENT);
		}

		if ((flags & ZGET_FLAG_WITHOUT_VNODE_GET)) {
			/* Do not increase vnode iocount */
			*zpp = zp;
			mutex_exit(&zp->z_lock);
			sa_buf_rele(db, NULL);
			ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
			return 99; // Special returncode to signal we did not grab iocount
		}

		/* We are racing zfs_znode_getvnode() and we got here first, we
		 * need to let it get ahead */
		if (!vp) {
			static int count = 0;

			mutex_exit(&zp->z_lock);
			sa_buf_rele(db, NULL);
			ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);

			kpreempt(KPREEMPT_SYNC);
			dprintf("zget racing attach\n");
			//DbgBreakPoint();
			IOSleep(hz>>2);
			if (count++ > 50) {
				count = 0;
				dprintf("long time\n");
			}
			goto again;
		}

		/* Due to vnode_create() -> zfs_fsync() -> zil_commit() -> zget()
		 * -> vnode_getwithvid() -> deadlock. Unsure why vnode_getwithvid()
		 * ends up sleeping in msleep() but vnode_get() does not.
		 */
		if (!vp || (err=vnode_getwithvid(vp, zp->z_vid)) != 0) {
			// vid is no longer valid

			mutex_exit(&zp->z_lock);
			sa_buf_rele(db, NULL);
			ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);

			//ZTOV(zp) = NULL;
			dprintf("%s: vnode_get() returned %d\n", __func__, err);
			kpreempt(KPREEMPT_SYNC);
			IOSleep(hz >> 2);
			if (crutch_count++ > 50) {
				crutch_count = 0;
				dprintf("long time\n");
				//return ENOENT; // FIXME - why does this happen
			}
			goto again;
		}

		/*
		 * Since we had to drop all of our locks above, make sure
		 * that we have the vnode and znode we had before.
		 */
		if ((vid != zp->z_vid) || (vp != ZTOV(zp))) {
			/* Release the wrong vp from vnode_getwithvid(). This
			 * call is missing in 10a286 - lundman */
			VN_RELE(vp);

			mutex_exit(&zp->z_lock);
			sa_buf_rele(db, NULL);
			ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);

			dprintf("ZFS: the vids do not match part 1\n");
			IOSleep(hz >> 2);
			
			goto again;
		}

		mutex_exit(&zp->z_lock);
		sa_buf_rele(db, NULL);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);

		*zpp = zp;
		getnewvnode_drop_reserve();
		return (0);
	} // if vnode != NULL

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

	zp = NULL;
	zp = zfs_znode_alloc(zfsvfs, db, doi.doi_data_block_size,
	    doi.doi_bonus_type, NULL);

	if (zp == NULL) {
		err = SET_ERROR(ENOENT);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		getnewvnode_drop_reserve();
		dprintf("zget returning %d\n", err);
		return (err);
	}
	*zpp = zp;

	if (err == 0) {
#ifndef _WIN32 /* Already associated with mount from vnode_create */
		struct vnode *vp = ZTOV(zp);

		err = insmntque(vp, zfsvfs->z_vfs);
		if (err == 0)
			VOP_UNLOCK(vp, 0);
		else {
			zp->z_vnode = NULL;
			zfs_znode_dmu_fini(zp);
			zfs_znode_free(zp);
			*zpp = NULL;
		}
#endif
	}
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
	getnewvnode_drop_reserve();

	if ((flags & ZGET_FLAG_WITHOUT_VNODE) ||
		(flags & ZGET_FLAG_WITHOUT_VNODE_GET))	{
		/* Insert it on our list of active znodes */
		//mutex_enter(&zfsvfs->z_znodes_lock);
		//list_insert_tail(&zfsvfs->z_all_znodes, zp);
		//membar_producer();
		//mutex_exit(&zfsvfs->z_znodes_lock);
		if (flags & ZGET_FLAG_WITHOUT_VNODE_GET)
			dprintf("ZFS: zget without vnode in znodealloc case\n");
	} else {
		/* Attach a vnode to our new znode */
		zfs_znode_getvnode(zp, NULL, zfsvfs); /* Assigns both vp and z_vnode */
	}

	//dprintf("zget returning %d\n", err);
	return (err);
}


int
zfs_rezget(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	dmu_object_info_t doi;
	dmu_buf_t *db;
	struct vnode *vp;
	uint64_t obj_num = zp->z_id;
	uint64_t mode, size;
	sa_bulk_attr_t bulk[8];
	int err;
	int count = 0;
	uint64_t gen;

	ZFS_OBJ_HOLD_ENTER(zfsvfs, obj_num);

	mutex_enter(&zp->z_acl_lock);
	if (zp->z_acl_cached) {
		zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = NULL;
	}
	mutex_exit(&zp->z_acl_lock);

	dprintf("rezget: %p %p %p\n", zp, &zp->z_xattr_lock,
	    zp->z_xattr_parent);

	rw_enter(&zp->z_xattr_lock, RW_WRITER);
	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}

	rw_exit(&zp->z_xattr_lock);

	ASSERT(zp->z_sa_hdl == NULL);
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
	    &zp->z_pflags,
	    sizeof (zp->z_pflags));
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
	 * XXXPJD: Not sure how is that possible, but under heavy
	 * zfs recv -F load it happens that z_gen is the same, but
	 * vnode type is different than znode type. This would mean
	 * that for example regular file was replaced with directory
	 * which has the same object number.
	 */
	vp = ZTOV(zp);
	if (vp != NULL &&
	    vnode_vtype(vp) != IFTOVT((mode_t)zp->z_mode)) {
		zfs_znode_dmu_fini(zp);
		ZFS_OBJ_HOLD_EXIT(zfsvfs, obj_num);
		return (EIO);
	}

	zp->z_blksz = doi.doi_data_block_size;
	if (vp != NULL) {
		vn_pages_remove(vp, 0, 0);
		if (zp->z_size != size)
			vnode_pager_setsize(vp, zp->z_size);
	}

	/*
	 * If the file has zero links, then it has been unlinked on the send
	 * side and it must be in the received unlinked set.
	 * We call zfs_znode_dmu_fini() now to prevent any accesses to the
	 * stale data and to prevent automatical removal of the file in
	 * zfs_zinactive().  The file will be removed either when it is removed
	 * on the send side and the next incremental stream is received or
	 * when the unlinked set gets processed.
	 */
	zp->z_unlinked = (zp->z_links == 0);
	if (zp->z_unlinked)
		zfs_znode_dmu_fini(zp);

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
		VERIFY(0 == dmu_object_free(os, acl_obj, tx));
	}
	VERIFY(0 == dmu_object_free(os, obj, tx));
	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, obj);
	zfs_znode_free(zp);
}

void
zfs_zinactive(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	uint64_t z_id = zp->z_id;
	ASSERT(zp->z_sa_hdl);

	/*
	 * Don't allow a zfs_zget() while were trying to release this znode
	 */
	ZFS_OBJ_HOLD_ENTER(zfsvfs, z_id);

	mutex_enter(&zp->z_lock);

	/* Solaris checks to see if a reference was grabbed to the vnode here
	 * which we can not easily do in XNU */
	//if (ZTOV(zp) && vnode_isinuse(ZTOV(zp), 0)) {
	//	printf("ZFS: zinactive(%p) has non-zero vp reference!\n", zp);
	//}

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
		if (!(vfs_isrdonly(zfsvfs->z_vfs)) && !zfs_unlink_suspend_progress) {
			mutex_exit(&zp->z_lock);
			ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
			zfs_rmnode(zp);
			return;
		}
	}

	mutex_exit(&zp->z_lock);
	zfs_znode_dmu_fini(zp);
	ZFS_OBJ_HOLD_EXIT(zfsvfs, z_id);
	zfs_znode_free(zp);
}

void
zfs_znode_free(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	mutex_enter(&zfsvfs->z_znodes_lock);
	zp->z_vnode = NULL;
	POINTER_INVALIDATE(&zp->z_zfsvfs);
	list_remove(&zfsvfs->z_all_znodes, zp); /* XXX */
	mutex_exit(&zfsvfs->z_znodes_lock);

	if (zp->z_acl_cached) {
		zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = NULL;
	}

	if (zp->z_xattr_cached) {
		nvlist_free(zp->z_xattr_cached);
		zp->z_xattr_cached = NULL;
	}

	if (zp->z_name_cache) {
		kmem_free(zp->z_name_cache, zp->z_name_len);
		zp->z_name_cache = NULL;
	}

	kmem_cache_free(znode_cache, zp);

	VFS_RELE(zfsvfs->z_vfs);
}


/*
 * Prepare to update znode time stamps.
 *
 *	IN:	zp	- znode requiring timestamp update
 *		flag	- AT_MTIME, AT_CTIME, AT_ATIME flags
 *		have_tx	- true of caller is creating a new txg
 *
 *	OUT:	zp	- new atime (via underlying inode's i_atime)
 *		mtime	- new mtime
 *		ctime	- new ctime
 *
 * NOTE: The arguments are somewhat redundant.  The following condition
 * is always true:
 *
 *		have_tx == !(flag & AT_ATIME)
 */
void
zfs_tstamp_update_setup(znode_t *zp, uint_t flag, uint64_t mtime[2],
			    uint64_t ctime[2], boolean_t have_tx)
{
	timestruc_t	now;

	ASSERT(have_tx == !(flag & AT_ATIME));
	gethrestime(&now);

	/*
	 * NOTE: The following test intentionally does not update z_atime_dirty
	 * in the case where an ATIME update has been requested but for which
	 * the update is omitted due to relatime logic.  The rationale being
	 * that if the flag was set somewhere else, we should leave it alone
	 * here.
	 */
	if (flag & AT_ATIME) {
		ZFS_TIME_ENCODE(&now, zp->z_atime);
#ifdef LINUX
		ZTOI(zp)->i_atime.tv_sec = zp->z_atime[0];
		ZTOI(zp)->i_atime.tv_nsec = zp->z_atime[1];
#endif
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

	error = dmu_object_set_blocksize(zp->z_zfsvfs->z_os,
	    zp->z_id,
	    size, 0, tx);

	if (error == ENOTSUP)
		return;
	ASSERT(error == 0);

	/* What blocksize did we actually get? */
	dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &zp->z_blksz, &dummy);
}

#ifdef sun
/*
 * This is a dummy interface used when pvn_vplist_dirty() should *not*
 * be calling back into the fs for a putpage().  E.g.: when truncating
 * a file, the pages being "thrown away* don't need to be written out.
 */
/* ARGSUSED */
static int
zfs_no_putpage(struct vnode *vp, page_t *pp, u_offset_t *offp, size_t *lenp,
			    int flags, cred_t *cr)
{
	ASSERT(0);
	return (0);
}
#endif	/* sun */

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
	locked_range_t *lr;
	uint64_t newblksz;
	int error;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	lr = rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end <= zp->z_size) {
		rangelock_exit(lr);
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
			newblksz = MIN(end, 1ULL << highbit64(zp->z_blksz));
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
		rangelock_exit(lr);
		return (error);
	}

	if (newblksz)
		zfs_grow_blocksize(zp, newblksz, tx);

	zp->z_size = end;

	VERIFY(0 == sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zp->z_zfsvfs),
	    &zp->z_size,
	    sizeof (zp->z_size), tx));

	vnode_pager_setsize(ZTOV(zp), end);

	rangelock_exit(lr);

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
	locked_range_t *lr;
	int error;

	/*
	 * Lock the range being freed.
	 */
	lr = rangelock_enter(&zp->z_rangelock, off, len, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (off >= zp->z_size) {
		rangelock_exit(lr);
		return (0);
	}

	if (off + len > zp->z_size)
		len = zp->z_size - off;

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, off, len);

	if (error == 0) {
		/*
		 * In FreeBSD we cannot free block in the middle of a file,
		 * but only at the end of a file, so this code path should
		 * never happen.
		 */
		vnode_pager_setsize(ZTOV(zp), off);
	}

#ifdef _LINUX
	/*
	 * Zero partial page cache entries.  This must be done under a
	 * range lock in order to keep the ARC and page cache in sync.
	 */
	if (zp->z_is_mapped) {
		loff_t first_page, last_page, page_len;
		loff_t first_page_offset, last_page_offset;

		/* first possible full page in hole */
		first_page = (off + PAGE_CACHE_SIZE - 1) >> PAGE_CACHE_SHIFT;
		/* last page of hole */
		last_page = (off + len) >> PAGE_CACHE_SHIFT;

		/* offset of first_page */
		first_page_offset = first_page << PAGE_CACHE_SHIFT;
		/* offset of last_page */
		last_page_offset = last_page << PAGE_CACHE_SHIFT;

		/* truncate whole pages */
		if (last_page_offset > first_page_offset) {
			truncate_inode_pages_range(ZTOI(zp)->i_mapping,
			    first_page_offset, last_page_offset - 1);
		}

		/* truncate sub-page ranges */
		if (first_page > last_page) {
			/* entire punched area within a single page */
			zfs_zero_partial_page(zp, off, len);
		} else {
			/* beginning of punched area at the end of a page */
			page_len  = first_page_offset - off;
			if (page_len > 0)
				zfs_zero_partial_page(zp, off, page_len);

			/* end of punched area at the beginning of a page */
			page_len = off + len - last_page_offset;
			if (page_len > 0)
				zfs_zero_partial_page(zp, last_page_offset,
				    page_len);
		}
	}
#endif
	rangelock_exit(lr);

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
	struct vnode *vp = ZTOV(zp);
	dmu_tx_t *tx;
	locked_range_t *lr;
	int error;
	sa_bulk_attr_t bulk[2];
	int count = 0;
	/*
	 * We will change zp_size, lock the whole file.
	 */
	lr = rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end >= zp->z_size) {
		rangelock_exit(lr);
		return (0);
	}

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, end,
	    DMU_OBJECT_END);
	if (error) {
		rangelock_exit(lr);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		rangelock_exit(lr);
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
	VERIFY(sa_bulk_update(zp->z_sa_hdl, bulk, count, tx) == 0);

	dmu_tx_commit(tx);

	/*
	 * Clear any mapped pages in the truncated region.  This has to
	 * happen outside of the transaction to avoid the possibility of
	 * a deadlock with someone trying to push a page that we are
	 * about to invalidate.
	 */
	vnode_pager_setsize(vp, end);

	rangelock_exit(lr);

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
	struct vnode *vp = ZTOV(zp);
	dmu_tx_t *tx;
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	zilog_t *zilog = zfsvfs->z_log;
	uint64_t mode;
	uint64_t mtime[2], ctime[2];
	sa_bulk_attr_t bulk[3];
	int count = 0;
	int error;

	if (vnode_isfifo(ZTOV(zp)))
		return 0;

	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs), &mode,
	    sizeof (mode))) != 0)
		return (error);

	if (off > zp->z_size) {
		error =  zfs_extend(zp, off+len);
		if (error == 0 && log)
			goto log;
		goto out;
	}

	/*
	 * Check for any locks in the region to be freed.
	 */

	if (MANDLOCK(vp, (mode_t)mode)) {
		uint64_t length = (len ? len : zp->z_size - off);
		if ((error = chklock(vp, FWRITE, off, length, flag, NULL)))
			return (SET_ERROR(EAGAIN));
	}

	if (len == 0) {
		error = zfs_trunc(zp, off);
	} else {
		if ((error = zfs_free_range(zp, off, len)) == 0 &&
		    off + len > zp->z_size)
			error = zfs_extend(zp, off+len);
	}
	if (error || !log)
		goto out;
log:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &zp->z_pflags, 8);
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime, B_TRUE);
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT(error == 0);

	zfs_log_truncate(zilog, tx, TX_TRUNCATE, zp, off, len);

	dmu_tx_commit(tx);

	error = 0;

out:

	return (error);
}

void
zfs_create_fs(objset_t *os, cred_t *cr, nvlist_t *zplprops, dmu_tx_t *tx)
{
	zfsvfs_t	*zfsvfs;
	uint64_t	moid, obj, sa_obj, version;
	uint64_t	sense = ZFS_CASE_SENSITIVE;
	uint64_t	norm = 0;
	nvpair_t	*elem;
	int		error;
	int		i;
	znode_t		*rootzp = NULL;
#ifndef _WIN32
	struct vnode		vnode;
#endif
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
	ASSERT(error == 0);

	/*
	 * Set starting attributes.
	 */
	version = zfs_zpl_version_map(spa_version(dmu_objset_spa(os)));
	elem = NULL;
	while ((elem = nvlist_next_nvpair(zplprops, elem)) != NULL) {
		/* For the moment we expect all zpl props to be uint64_ts */
		uint64_t val;
		char *name;

		ASSERT(nvpair_type(elem) == DATA_TYPE_UINT64);
		VERIFY(nvpair_value_uint64(elem, &val) == 0);
		name = nvpair_name(elem);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_VERSION)) == 0) {
			if (val < version)
				version = val;
		} else {
			error = zap_update(os, moid, name, 8, 1, &val, tx);
		}
		ASSERT(error == 0);
		if (strcmp(name, zfs_prop_to_name(ZFS_PROP_NORMALIZE)) == 0)
			norm = val;
		else if (strcmp(name, zfs_prop_to_name(ZFS_PROP_CASE)) == 0)
			sense = val;
	}
	ASSERT(version != 0);
	error = zap_update(os, moid, ZPL_VERSION_STR, 8, 1, &version, tx);

	/*
	 * Create zap object used for SA attribute registration
	 */

	if (version >= ZPL_VERSION_SA) {
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);
		error = zap_add(os, moid, ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		ASSERT(error == 0);
	} else {
		sa_obj = 0;
	}
	/*
	 * Create a delete queue.
	 */
	obj = zap_create(os, DMU_OT_UNLINKED_SET, DMU_OT_NONE, 0, tx);

	error = zap_add(os, moid, ZFS_UNLINKED_SET, 8, 1, &obj, tx);
	ASSERT(error == 0);

	/*
	 * Create root znode.  Create minimal znode/vnode/zfsvfs
	 * to allow zfs_mknode to work.
	 */
	VATTR_NULL(&vattr);
	vattr.va_mask = AT_MODE|AT_UID|AT_GID|AT_TYPE;
	vattr.va_type = VDIR;
#ifdef _WIN32
	vattr.va_mode = S_IFDIR|0777;
#else
	vattr.va_mode = S_IFDIR|0755;
#endif
	vattr.va_uid = crgetuid(cr);
	vattr.va_gid = crgetgid(cr);

	rootzp = kmem_cache_alloc(znode_cache, KM_SLEEP);
	//zfs_znode_cache_constructor(rootzp, NULL, 0);
	ASSERT(!POINTER_IS_VALID(rootzp->z_zfsvfs));
	rootzp->z_moved = 0;
	rootzp->z_unlinked = 0;
	rootzp->z_atime_dirty = 0;
	rootzp->z_is_sa = USE_SA(version, os);
	rootzp->z_vnode = NULL;
#ifndef _WIN32
	vnode.v_type = VDIR;
	vnode.v_data = rootzp;
	rootzp->z_vnode = &vnode;
#endif

	zfsvfs = kmem_alloc(sizeof (zfsvfs_t), KM_SLEEP);
#ifdef _WIN32
	bzero(zfsvfs, sizeof (zfsvfs_t));
#endif
	zfsvfs->z_os = os;
	zfsvfs->z_parent = zfsvfs;
	zfsvfs->z_version = version;
	zfsvfs->z_use_fuids = USE_FUIDS(version, os);
	zfsvfs->z_use_sa = USE_SA(version, os);
	zfsvfs->z_norm = norm;

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);

	ASSERT(error == 0);

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
	VERIFY(0 == zfs_acl_ids_create(rootzp, IS_ROOT_NODE, &vattr,
	    cr, NULL, &acl_ids));
	zfs_mknode(rootzp, &vattr, tx, cr, IS_ROOT_NODE, &zp, &acl_ids);
	ASSERT3P(zp, ==, rootzp);
	error = zap_add(os, moid, ZFS_ROOT_OBJ, 8, 1, &rootzp->z_id, tx);
	ASSERT(error == 0);
	zfs_acl_ids_free(&acl_ids);
	POINTER_INVALIDATE(&rootzp->z_zfsvfs);

	sa_handle_destroy(rootzp->z_sa_hdl);
	rootzp->z_vnode = NULL;
	kmem_cache_free(znode_cache, rootzp);

	/*
	 * Create shares directory
	 */

	error = zfs_create_share_dir(zfsvfs, tx);

	ASSERT(error == 0);

	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);

	list_destroy(&zfsvfs->z_all_znodes);
	mutex_destroy(&zfsvfs->z_znodes_lock);

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
	if (((doi.doi_bonus_type != DMU_OT_SA) &&
	    (doi.doi_bonus_type != DMU_OT_ZNODE)) ||
	    ((doi.doi_bonus_type == DMU_OT_ZNODE) &&
	    (doi.doi_bonus_size < sizeof (znode_phys_t)))) {
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

void
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
		return ((EINVAL));

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
		uint64_t pobj = 0;
		char component[MAXNAMELEN + 2];
		size_t complen;
		int is_xattrdir = 0;

		if (prevdb)
			zfs_release_sa_handle(prevhdl, prevdb, FTAG);

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
			(void) snprintf(component + 1, MAXNAMELEN+1,
			    "<xattrdir>");
		} else {
			error = zap_value_search(osp, pobj, obj,
			    ZFS_DIRENT_OBJ(-1ULL),
			    component + 1);
			if (error != 0)
				break;
		}

		complen = strlen(component);
		path -= complen;
		ASSERT(path >= buf);
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
		ASSERT(sa_db != NULL);
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
