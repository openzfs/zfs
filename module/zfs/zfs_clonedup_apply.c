// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2026, Patrick Domack. All rights reserved.
 */

/*
 * Destination side of "zpool clonedup".  A destination is one object
 * in one dataset, reached one of two ways: a file in a mounted
 * filesystem through its znode, or any object of a dataset nobody
 * else has open by owning the dataset.  The first excludes concurrent
 * writers with the object's range lock; ownership excludes them
 * outright, since mount and receive need ownership too.  Everything
 * that needs a znode lives here; dsl_clonedup.c stays at the DMU
 * level.
 */

#include <sys/stat.h>
#include <sys/dsl_clonedup.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/sa.h>
#include <sys/txg.h>
#include <sys/zil.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_quota.h>
#include <sys/zfs_vnops.h>
#include <sys/blkptr.h>

/*
 * Ownership of a dataset is exclusive, so the handles on one
 * owned dataset share it, and the last of them to close disowns.
 */
typedef struct zfs_clonedup_own {
	objset_t	*zco_os;
	zfsvfs_t	*zco_zfsvfs;	/* filesystem: quotas */
	uint64_t	zco_txg;	/* last txg written */
	uint_t		zco_refs;
} zfs_clonedup_own_t;

/*
 * zfs_enter() for the file handles of one batch, taken by the
 * first and left by the last to close, the way zfs_enter_two()
 * enters a filesystem once.  On FreeBSD it is rms_rlock(), and a
 * thread that holds the read cannot take it again while a
 * teardown writer waits.
 */
typedef struct zfs_clonedup_enter {
	uint_t		zce_refs;
} zfs_clonedup_enter_t;

struct zfs_clonedup_dst {
	uint64_t	zcd_object;
	zfsvfs_t	*zcd_zfsvfs;	/* file: zfs_vfs_ref() pin */
	zfs_clonedup_enter_t *zcd_enter; /* file: the zfs_enter() */
	znode_t		*zcd_zp;
	objset_t	*zcd_os;	/* owned dataset */
	dnode_t		*zcd_dn;	/* owned: the object */
	zfs_clonedup_own_t *zcd_own;	/* owned: the ownership */
	uint64_t	zcd_pflags;	/* owned file: from its SA */
	uint64_t	zcd_uid;
	uint64_t	zcd_gid;
	uint64_t	zcd_projid;
};

/* zfs_enter() and zfs_exit() must see the same tag */
static char zfs_clonedup_tag;

/*
 * Hold the dataset just long enough to learn its type.  Snapshots
 * are never destinations.
 */
static int
zfs_clonedup_dst_lookup(dsl_pool_t *dp, uint64_t dsobj,
    dmu_objset_type_t *typep)
{
	dsl_dataset_t *ds;
	objset_t *os = NULL;
	int err;

	dsl_pool_config_enter(dp, FTAG);
	err = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (err == 0) {
		if (ds->ds_is_snapshot)
			err = SET_ERROR(EBUSY);
		else
			err = dmu_objset_from_ds(ds, &os);
		if (err == 0)
			*typep = dmu_objset_type(os);
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_pool_config_exit(dp, FTAG);
	return (err);
}

/*
 * getzfsvfs_impl() with a reference that never sleeps: an unmount in
 * progress takes os_user_ptr_lock after its vfs_busy() holders have
 * drained, and on FreeBSD a plain vfs_busy() waits for the unmount.
 */
static int
zfs_clonedup_vfs_hold(objset_t *os, zfsvfs_t **zfvp)
{
	int err;

	if (dmu_objset_type(os) != DMU_OST_ZFS)
		return (SET_ERROR(EINVAL));
	mutex_enter(&os->os_user_ptr_lock);
	*zfvp = dmu_objset_get_user(os);
	err = zfs_vfs_ref_nowait(zfvp);
	mutex_exit(&os->os_user_ptr_lock);
	return (err);
}

/*
 * A file of a filesystem the caller has entered, held as a znode.
 * It has to be a regular file the apply may write.  zfs_zget()
 * answers EINVAL for an object that holds no znode, which is ENOENT
 * here, and any other error is returned as it is.
 */
static int
zfs_clonedup_file_get(zfsvfs_t *zfsvfs, uint64_t object,
    znode_t **zpp)
{
	znode_t *zp;
	int err;

	if (zfs_is_readonly(zfsvfs))
		return (SET_ERROR(EROFS));
	err = zfs_zget(zfsvfs, object, &zp);
	if (err == EINVAL)
		return (SET_ERROR(ENOENT));
	if (err != 0)
		return (err);
	if (zfs_verify_zp(zp) != 0 || !S_ISREG(zp->z_mode) ||
	    zp->z_unlinked) {
		zrele(zp);
		return (SET_ERROR(ENOENT));
	}
	if (zp->z_pflags & ZFS_IMMUTABLE) {
		zrele(zp);
		return (SET_ERROR(EPERM));
	}
	*zpp = zp;
	return (0);
}

/*
 * A mounted filesystem is pinned through its zfsvfs, then the file is
 * held as a znode.  EBUSY means not mounted or on its way out.
 */
static int
zfs_clonedup_file_open(spa_t *spa, uint64_t dsobj, uint64_t object,
    zfs_clonedup_dst_t **dstp)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	dsl_dataset_t *ds;
	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	znode_t *zp;
	zfs_clonedup_dst_t *dst;
	int err;

	dsl_pool_config_enter(dp, FTAG);
	err = dsl_dataset_hold_obj(dp, dsobj, FTAG, &ds);
	if (err == 0) {
		err = dmu_objset_from_ds(ds, &os);
		if (err == 0)
			err = zfs_clonedup_vfs_hold(os, &zfsvfs);
		dsl_dataset_rele(ds, FTAG);
	}
	dsl_pool_config_exit(dp, FTAG);

	if (err == ESRCH || err == EINVAL)
		err = SET_ERROR(EBUSY);
	if (err != 0)
		return (err);

	/* From here the pin keeps the zfsvfs and objset alive. */
	if (zfs_enter(zfsvfs, &zfs_clonedup_tag) != 0) {
		zfs_vfs_rele(zfsvfs);
		return (SET_ERROR(EBUSY));
	}
	err = zfs_clonedup_file_get(zfsvfs, object, &zp);
	if (err != 0) {
		zfs_exit(zfsvfs, &zfs_clonedup_tag);
		zfs_vfs_rele(zfsvfs);
		return (err);
	}

	dst = kmem_zalloc(sizeof (*dst), KM_SLEEP);
	dst->zcd_object = object;
	dst->zcd_zfsvfs = zfsvfs;
	dst->zcd_enter = kmem_alloc(sizeof (*dst->zcd_enter),
	    KM_SLEEP);
	dst->zcd_enter->zce_refs = 1;
	dst->zcd_zp = zp;
	*dstp = dst;
	return (0);
}

/*
 * The quotas of an owned filesystem, read into a zfsvfs the way
 * zfsvfs_hold() reads them for a filesystem nobody has mounted.
 * zfsvfs_create_impl() drops holds tagged with the zfsvfs when it
 * fails.  zfsvfs_create_hold() takes them by name.  Here they are
 * taken by object, and dropped again once it succeeds, since the
 * ownership keeps the objset.
 */
static int
zfs_clonedup_own_zfsvfs(objset_t *os, zfsvfs_t **zfvp)
{
	dsl_pool_t *dp = dmu_objset_pool(os);
	dsl_dataset_t *ds;
	zfsvfs_t *zfsvfs;
	int err;

	zfsvfs = kmem_zalloc(sizeof (*zfsvfs), KM_SLEEP);
	spa_open_ref(dp->dp_spa, zfsvfs);
	dsl_pool_config_enter(dp, zfsvfs);
	err = dsl_dataset_hold_obj(dp, dmu_objset_id(os), zfsvfs,
	    &ds);
	if (err != 0) {
		dsl_pool_rele(dp, zfsvfs);
		kmem_free(zfsvfs, sizeof (*zfsvfs));
		return (err);
	}
	zfsvfs->z_use_hold = B_TRUE;
	err = zfsvfs_create_impl(zfvp, zfsvfs, os);
	if (err == 0)
		dmu_objset_rele(os, zfsvfs);
	return (err);
}

/*
 * An owned file's flags, owner, mode and links, read from its SA as
 * zfs_znode_alloc() reads them.  Nothing else can change them while
 * the dataset is owned.  Anything but a regular file with a link
 * answers ENOENT, as zfs_clonedup_file_get() does for a mounted one.
 */
static int
zfs_clonedup_own_attrs(zfs_clonedup_dst_t *dst,
    const dmu_object_info_t *doi)
{
	zfsvfs_t *zfsvfs = dst->zcd_own->zco_zfsvfs;
	sa_bulk_attr_t bulk[5];
	sa_handle_t *hdl;
	uint64_t mode = 0, links = 0;
	int count = 0;
	int err;

	if (doi->doi_bonus_type != DMU_OT_SA &&
	    (doi->doi_bonus_type != DMU_OT_ZNODE ||
	    doi->doi_bonus_size < sizeof (znode_phys_t)))
		return (SET_ERROR(ENOENT));
	err = sa_handle_get(dst->zcd_os, dst->zcd_object, NULL,
	    SA_HDL_PRIVATE, &hdl);
	if (err != 0)
		return (err);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &dst->zcd_pflags, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
	    &dst->zcd_uid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
	    &dst->zcd_gid, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
	    &mode, 8);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &links, 8);
	err = sa_bulk_lookup(hdl, bulk, count);
	if (err == 0 && (!S_ISREG(mode) || links == 0))
		err = SET_ERROR(ENOENT);
	dst->zcd_projid = ZFS_DEFAULT_PROJID;
	/*
	 * A project quota check on an objset that keeps no project
	 * usage yet starts the upgrade that adds it, which dirties
	 * every object in the dataset.  The apply must not rewrite a
	 * dataset that way, and until the upgrade runs there is no
	 * project quota to enforce.
	 */
	if (err == 0 && (dst->zcd_pflags & ZFS_PROJID) &&
	    dmu_objset_projectquota_present(dst->zcd_os))
		err = sa_lookup(hdl, SA_ZPL_PROJID(zfsvfs),
		    &dst->zcd_projid, 8);
	sa_handle_destroy(hdl);
	return (err);
}

/*
 * A file system nobody has mounted is owned for the duration of the
 * hold.  Ownership is what a mount and a receive take, so no writer
 * can appear and no range lock is needed.  EBUSY means somebody owns
 * it already, or its log has not been replayed yet and the blocks on
 * disk are not the final content.
 */
static int
zfs_clonedup_own_open(spa_t *spa, uint64_t dsobj, uint64_t object,
    dmu_objset_type_t type, zfs_clonedup_dst_t **dstp)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	dnode_t *dn = NULL;
	dmu_object_info_t doi;
	zfs_clonedup_dst_t *dst;
	uint64_t ro = 1;
	int err;

	dsl_pool_config_enter(dp, FTAG);
	err = dmu_objset_own_obj(dp, dsobj, type, B_FALSE, B_FALSE,
	    &zfs_clonedup_tag, &os);
	if (err == 0) {
		(void) dsl_prop_get_int_ds(dmu_objset_ds(os),
		    zfs_prop_to_name(ZFS_PROP_READONLY), &ro);
	}
	dsl_pool_config_exit(dp, FTAG);
	if (err != 0)
		return (err);

	if (os->os_zil_header.zh_flags & ZIL_REPLAY_NEEDED) {
		err = SET_ERROR(EBUSY);
		goto fail;
	}
	if (ro != 0) {
		err = SET_ERROR(EROFS);
		goto fail;
	}
	err = dmu_object_info(os, object, &doi);
	if (err == 0 && doi.doi_type != DMU_OT_PLAIN_FILE_CONTENTS)
		err = SET_ERROR(ENOENT);
	if (err != 0)
		goto fail;
	if (type == DMU_OST_ZFS) {
		err = zfs_clonedup_own_zfsvfs(os, &zfsvfs);
		if (err != 0)
			goto fail;
	}
	err = dnode_hold(os, object, &zfs_clonedup_tag, &dn);
	if (err != 0)
		goto fail;

	dst = kmem_zalloc(sizeof (*dst), KM_SLEEP);
	dst->zcd_object = object;
	dst->zcd_os = os;
	dst->zcd_dn = dn;
	dst->zcd_own = kmem_alloc(sizeof (*dst->zcd_own), KM_SLEEP);
	dst->zcd_own->zco_os = os;
	dst->zcd_own->zco_zfsvfs = zfsvfs;
	dst->zcd_own->zco_txg = 0;
	dst->zcd_own->zco_refs = 1;
	if (zfsvfs != NULL) {
		err = zfs_clonedup_own_attrs(dst, &doi);
		if (err != 0) {
			zfs_clonedup_dst_close(dst);
			return (err);
		}
	}
	*dstp = dst;
	return (0);

fail:
	if (zfsvfs != NULL)
		zfsvfs_free(zfsvfs);
	dmu_objset_disown(os, B_FALSE, &zfs_clonedup_tag);
	return (err);
}

/*
 * Another file of the filesystem dst has entered.  The new handle
 * shares dst's zfs_enter() and takes a pin of its own, which an
 * unmount in progress refuses as it would refuse a first open.
 */
static int
zfs_clonedup_file_share(zfs_clonedup_dst_t *dst, uint64_t object,
    zfs_clonedup_dst_t **dstp)
{
	zfsvfs_t *zfsvfs = dst->zcd_zfsvfs;
	zfs_clonedup_dst_t *sh;
	znode_t *zp;
	int err;

	if (zfs_vfs_ref_nowait(&zfsvfs) != 0)
		return (SET_ERROR(EBUSY));
	err = zfs_clonedup_file_get(zfsvfs, object, &zp);
	if (err != 0) {
		zfs_vfs_rele(zfsvfs);
		return (err);
	}
	sh = kmem_zalloc(sizeof (*sh), KM_SLEEP);
	sh->zcd_object = object;
	sh->zcd_zfsvfs = zfsvfs;
	sh->zcd_enter = dst->zcd_enter;
	sh->zcd_enter->zce_refs++;
	sh->zcd_zp = zp;
	*dstp = sh;
	return (0);
}

/*
 * Another object of a dataset that dst holds.  A second owner would
 * be refused, so a handle on an owned dataset shares dst's
 * ownership and the dataset stays owned until both close.  A file
 * handle shares dst's zfs_enter().
 */
int
zfs_clonedup_dst_share(zfs_clonedup_dst_t *dst, uint64_t object,
    zfs_clonedup_dst_t **dstp)
{
	zfs_clonedup_own_t *own = dst->zcd_own;
	dmu_object_info_t doi;
	zfs_clonedup_dst_t *sh;
	dnode_t *dn;
	int err;

	*dstp = NULL;
	if (dst->zcd_enter != NULL)
		return (zfs_clonedup_file_share(dst, object, dstp));
	if (own == NULL)
		return (SET_ERROR(ENOENT));
	err = dmu_object_info(own->zco_os, object, &doi);
	if (err == 0 && doi.doi_type != DMU_OT_PLAIN_FILE_CONTENTS)
		err = SET_ERROR(ENOENT);
	if (err != 0)
		return (err);
	err = dnode_hold(own->zco_os, object, &zfs_clonedup_tag, &dn);
	if (err != 0)
		return (err);
	sh = kmem_zalloc(sizeof (*sh), KM_SLEEP);
	sh->zcd_object = object;
	sh->zcd_os = own->zco_os;
	sh->zcd_dn = dn;
	sh->zcd_own = own;
	own->zco_refs++;
	if (own->zco_zfsvfs != NULL) {
		err = zfs_clonedup_own_attrs(sh, &doi);
		if (err != 0) {
			zfs_clonedup_dst_close(sh);
			return (err);
		}
	}
	*dstp = sh;
	return (0);
}

/*
 * The mounted path is tried first, ownership second, and the mounted
 * path once more if the file system was mounted between the two.
 * Whatever is left is EBUSY and counted as such.
 */
int
zfs_clonedup_dst_open(spa_t *spa, uint64_t dsobj, uint64_t object,
    zfs_clonedup_dst_t **dstp)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	dmu_objset_type_t type = DMU_OST_NONE;
	int err;

	*dstp = NULL;
	err = zfs_clonedup_dst_lookup(dp, dsobj, &type);
	if (err != 0)
		return (err);

	if (type == DMU_OST_ZFS) {
		err = zfs_clonedup_file_open(spa, dsobj, object,
		    dstp);
		if (err == EBUSY)
			err = zfs_clonedup_own_open(spa, dsobj,
			    object, type, dstp);
		if (err == EBUSY)
			err = zfs_clonedup_file_open(spa, dsobj,
			    object, dstp);
	} else {
		err = SET_ERROR(ENOENT);
	}
	return (err);
}

static void
zfs_clonedup_wait_written(objset_t *os, uint64_t txg)
{
	if (txg != 0) {
		(void) txg_wait_synced_flags(dmu_objset_pool(os), txg,
		    TXG_WAIT_SUSPEND);
	}
}

void
zfs_clonedup_dst_close(zfs_clonedup_dst_t *dst)
{
	if (dst->zcd_os != NULL) {
		zfs_clonedup_own_t *own = dst->zcd_own;

		dnode_rele(dst->zcd_dn, &zfs_clonedup_tag);
		if (own != NULL && --own->zco_refs == 0) {
			zfs_clonedup_wait_written(own->zco_os,
			    own->zco_txg);
			if (own->zco_zfsvfs != NULL)
				zfsvfs_free(own->zco_zfsvfs);
			dmu_objset_disown(own->zco_os, B_FALSE,
			    &zfs_clonedup_tag);
			kmem_free(own, sizeof (*own));
		}
	} else {
		zfsvfs_t *zfsvfs = dst->zcd_zfsvfs;
		zfs_clonedup_enter_t *en = dst->zcd_enter;

		zrele(dst->zcd_zp);
		if (--en->zce_refs == 0) {
			kmem_free(en, sizeof (*en));
			zfs_exit(zfsvfs, &zfs_clonedup_tag);
		}
		zfs_vfs_rele(zfsvfs);
	}
	kmem_free(dst, sizeof (*dst));
}

objset_t *
zfs_clonedup_dst_objset(zfs_clonedup_dst_t *dst)
{
	if (dst->zcd_os != NULL)
		return (dst->zcd_os);
	return (dst->zcd_zfsvfs->z_os);
}

uint64_t
zfs_clonedup_dst_object(zfs_clonedup_dst_t *dst)
{
	return (dst->zcd_object);
}

dsl_clonedup_kstat_id_t
zfs_clonedup_dst_kind(zfs_clonedup_dst_t *dst)
{
	if (dst->zcd_os != NULL)
		return (DCK_DST_OWNED);
	return (DCK_DST_MOUNTED);
}

/*
 * True when this handle holds the dataset through dmu_objset_own(),
 * which no second worker can hold.  A mounted filesystem is pinned
 * by a reference, and several workers can hold it at once.
 */
boolean_t
zfs_clonedup_dst_exclusive(zfs_clonedup_dst_t *dst)
{
	return (dst->zcd_own != NULL);
}

/* The switch every block cloning path honours. */
boolean_t
zfs_clonedup_bclone_enabled(void)
{
	return (zfs_bclone_enabled != 0);
}

static zfs_locked_range_t *
zfs_clonedup_lock(zfs_rangelock_t *rl, uint64_t off, uint64_t len,
    boolean_t nowait, boolean_t dryrun)
{
	zfs_rangelock_type_t type = dryrun ? RL_READER : RL_WRITER;

	if (nowait)
		return (zfs_rangelock_tryenter(rl, off, len, type));
	return (zfs_rangelock_enter(rl, off, len, type));
}

/*
 * The two prep functions run the checks of their kind and confirm
 * the block still exists at that size.  The rest of the checks are
 * zfs_clonedup_dst_clone_check()'s.
 * They return a policy errno, or 0 with *gop set when the block may
 * be written and, for a file, *lrp holding its range lock.  With
 * *gop clear, *resp says why not.  With nowait set, a file returns
 * EAGAIN, holding nothing, where it would otherwise wait for the
 * range lock.  They never write out a file's cached pages.  That is
 * safe: writeback later stores a page dirtied through a mapping over
 * the clone.  With dryrun set they write nothing, and the range lock
 * is a reader's.
 */
static int
zfs_clonedup_file_prep(zfs_clonedup_dst_t *dst, uint64_t off,
    uint64_t blksz, boolean_t nowait, boolean_t dryrun,
    zfs_locked_range_t **lrp, boolean_t *gop,
    zfs_clonedup_result_t *resp)
{
	znode_t *zp = dst->zcd_zp;
	zfsvfs_t *zfsvfs = dst->zcd_zfsvfs;

	if (zfs_verify_zp(zp) != 0 || !S_ISREG(zp->z_mode)) {
		*resp = ZCR_DST_STALE;
		return (0);
	}
	if (zfs_is_readonly(zfsvfs))
		return (SET_ERROR(EROFS));
	if (zp->z_pflags & ZFS_IMMUTABLE)
		return (SET_ERROR(EPERM));

	*lrp = zfs_clonedup_lock(&zp->z_rangelock, off, blksz,
	    nowait, dryrun);
	if (*lrp == NULL)
		return (SET_ERROR(EAGAIN));
	if (zp->z_blksz != blksz || off >= zp->z_size) {
		zfs_rangelock_exit(*lrp);
		*lrp = NULL;
		*resp = ZCR_DST_STALE;
		return (0);
	}
	*gop = B_TRUE;
	return (0);
}

static int
zfs_clonedup_own_prep(zfs_clonedup_dst_t *dst, uint64_t off,
    uint64_t blksz, boolean_t *gop, zfs_clonedup_result_t *resp)
{
	dmu_object_info_t doi;

	if (dst->zcd_pflags & ZFS_IMMUTABLE)
		return (SET_ERROR(EPERM));
	dmu_object_info_from_dnode(dst->zcd_dn, &doi);
	if (doi.doi_data_block_size != blksz ||
	    off >= doi.doi_max_offset) {
		*resp = ZCR_DST_STALE;
		return (0);
	}
	*gop = B_TRUE;
	return (0);
}

/*
 * The checks the prep functions leave out.  A clone takes
 * the source's block whole.  zfs_clone_range() checks properties,
 * special_small_blocks and quotas, and this checks properties and
 * quotas.  It skips special_small_blocks on purpose.
 * dsl_clonedup_src_serves() never lets a copy off the special class
 * replace one on it, so the copy on the special class survives.
 */
int
zfs_clonedup_dst_clone_check(zfs_clonedup_dst_t *dst, objset_t *sos)
{
	objset_t *os = zfs_clonedup_dst_objset(dst);
	znode_t *zp = dst->zcd_zp;
	zfsvfs_t *zfsvfs = dst->zcd_zfsvfs;
	uint64_t uid = dst->zcd_uid;
	uint64_t gid = dst->zcd_gid;
	uint64_t projid = dst->zcd_projid;

	if (!zfs_bclone_props_match(sos, os))
		return (SET_ERROR(EXDEV));
	if (zp != NULL) {
		uid = KUID_TO_SUID(ZTOUID(zp));
		gid = KGID_TO_SGID(ZTOGID(zp));
		projid = zp->z_projid;
	} else if (dst->zcd_own != NULL) {
		zfsvfs = dst->zcd_own->zco_zfsvfs;
	}
	if (zfsvfs == NULL)
		return (0);
	if (zfs_id_overblockquota(zfsvfs, DMU_USERUSED_OBJECT,
	    uid) ||
	    zfs_id_overblockquota(zfsvfs, DMU_GROUPUSED_OBJECT,
	    gid) ||
	    (projid != ZFS_DEFAULT_PROJID &&
	    zfs_id_overblockquota(zfsvfs, DMU_PROJECTUSED_OBJECT,
	    projid)))
		return (SET_ERROR(EDQUOT));
	return (0);
}

static void
zfs_clonedup_tx_hold_clone(dmu_tx_t *tx, zfs_clonedup_dst_t *dst,
    uint64_t off, uint64_t blksz)
{
	dmu_buf_impl_t *db;

	if (dst->zcd_os != NULL) {
		dmu_tx_hold_clone_by_dnode(tx, dst->zcd_dn, off,
		    blksz, blksz);
		return;
	}
	db = (dmu_buf_impl_t *)sa_get_db(dst->zcd_zp->z_sa_hdl);
	DB_DNODE_ENTER(db);
	dmu_tx_hold_clone_by_dnode(tx, DB_DNODE(db), off, blksz,
	    blksz);
	DB_DNODE_EXIT(db);
}

static void
zfs_clonedup_unlock(zfs_locked_range_t *lr)
{
	if (lr != NULL)
		zfs_rangelock_exit(lr);
}

int
zfs_clonedup_dst_prepare(zfs_clonedup_dst_t *dst, uint64_t blkid,
    const blkptr_t *dexp, boolean_t nowait, boolean_t dryrun,
    boolean_t *readyp, void **lockp, zfs_clonedup_result_t *resp)
{
	objset_t *os = zfs_clonedup_dst_objset(dst);
	uint64_t object = dst->zcd_object;
	uint64_t blksz = BP_GET_LSIZE(dexp);
	uint64_t off = blkid * blksz;
	zfs_locked_range_t *lr = NULL;
	boolean_t go = B_FALSE;
	blkptr_t bp;
	size_t nb = 1;
	int err;

	*readyp = B_FALSE;
	*lockp = NULL;
	*resp = ZCR_ERROR;

	if (dst->zcd_os != NULL)
		err = zfs_clonedup_own_prep(dst, off, blksz, &go,
		    resp);
	else
		err = zfs_clonedup_file_prep(dst, off, blksz, nowait,
		    dryrun, &lr, &go, resp);
	if (err != 0 || !go) {
		zfs_clonedup_unlock(lr);
		return (err);
	}

	err = dsl_clonedup_src_in_range(os, object, blkid, blksz);
	if (err == 0)
		err = dmu_read_l0_bps(os, object, off, blksz, &bp,
		    &nb);
	if (err == EAGAIN) {
		*resp = ZCR_DST_DIRTY;
		zfs_clonedup_unlock(lr);
		return (0);
	}
	if (err != 0 && !dsl_clonedup_err_moved(err)) {
		zfs_clonedup_unlock(lr);
		return (err);
	}
	if (err != 0 || nb != 1 ||
	    !zfs_dedupe_bp_same_block(&bp, dexp)) {
		*resp = ZCR_DST_STALE;
		zfs_clonedup_unlock(lr);
		return (0);
	}
	*readyp = B_TRUE;
	*lockp = lr;
	*resp = ZCR_QUEUED;
	return (0);
}

void
zfs_clonedup_dst_tx_hold(dmu_tx_t *tx, zfs_clonedup_dst_t *dst,
    uint64_t blkid, uint64_t blksz)
{
	zfs_clonedup_tx_hold_clone(tx, dst, blkid * blksz, blksz);
}

void
zfs_clonedup_dst_unlock(void *lock)
{
	zfs_clonedup_unlock((zfs_locked_range_t *)lock);
}

struct zfs_clonedup_spin {
	zfsvfs_t	*zsp_zfsvfs;
};

typedef struct zfs_clonedup_slock {
	zfs_locked_range_t	*zsl_lr;
	znode_t			*zsl_zp;
} zfs_clonedup_slock_t;

static uintptr_t
zfs_clonedup_dst_pinned(const zfs_clonedup_dst_t *dst)
{
	if (dst == NULL || dst->zcd_os != NULL)
		return (0);
	return ((uintptr_t)dst->zcd_zfsvfs);
}

static boolean_t
zfs_clonedup_src_unowned(objset_t *sos)
{
	dsl_dataset_t *ds = dmu_objset_ds(sos);
	boolean_t ok;

	mutex_enter(&ds->ds_lock);
	ok = ds->ds_owner == NULL ||
	    ds->ds_owner == &zfs_clonedup_tag;
	mutex_exit(&ds->ds_lock);
	return (ok);
}

int
zfs_clonedup_src_pin(objset_t *sos, zfs_clonedup_dst_t *dst,
    struct zfs_clonedup_spin **pinp)
{
	uintptr_t below = zfs_clonedup_dst_pinned(dst);
	struct zfs_clonedup_spin *pin;
	zfsvfs_t *zfsvfs = NULL;

	*pinp = NULL;
	if ((dst != NULL && zfs_clonedup_dst_objset(dst) == sos) ||
	    zfs_clonedup_src_unowned(sos))
		return (0);
	if (dmu_objset_type(sos) == DMU_OST_ZFS &&
	    zfs_clonedup_vfs_hold(sos, &zfsvfs) != 0)
		zfsvfs = NULL;
	if (zfsvfs != NULL) {
		/* address order, as zfs_enter_two() takes two */
		if ((uintptr_t)zfsvfs < below) {
			zfs_vfs_rele(zfsvfs);
			return (SET_ERROR(EDEADLK));
		}
		if (zfs_enter(zfsvfs, &zfs_clonedup_tag) != 0) {
			zfs_vfs_rele(zfsvfs);
			return (SET_ERROR(EBUSY));
		}
	}
	if (zfsvfs == NULL)
		return (SET_ERROR(EBUSY));
	pin = kmem_zalloc(sizeof (*pin), KM_SLEEP);
	pin->zsp_zfsvfs = zfsvfs;
	*pinp = pin;
	return (0);
}

void
zfs_clonedup_src_unpin(struct zfs_clonedup_spin *pin)
{
	if (pin == NULL)
		return;
	zfs_exit(pin->zsp_zfsvfs, &zfs_clonedup_tag);
	zfs_vfs_rele(pin->zsp_zfsvfs);
	kmem_free(pin, sizeof (*pin));
}

int
zfs_clonedup_src_lock(struct zfs_clonedup_spin *pin,
    zfs_clonedup_dst_t *dst, void *dlock, objset_t *sos,
    uint64_t sobj, uint64_t sblkid, const blkptr_t *sexp,
    boolean_t nowait, void **lockp, zfs_clonedup_result_t *resp)
{
	zfs_locked_range_t *dlr = dlock;
	uint64_t sblksz = BP_GET_LSIZE(sexp);
	boolean_t same = dst != NULL &&
	    zfs_clonedup_dst_objset(dst) == sos;
	zfs_clonedup_slock_t *sl;
	zfs_locked_range_t *lr = NULL;
	zfs_rangelock_t *rl = NULL;
	zfsvfs_t *zfsvfs = NULL;
	znode_t *zp = NULL;
	blkptr_t bp;
	int err;

	*lockp = NULL;
	*resp = ZCR_ERROR;
	if (pin != NULL) {
		zfsvfs = pin->zsp_zfsvfs;
	} else if (same) {
		if (dst->zcd_os == NULL)
			zfsvfs = dst->zcd_zfsvfs;
	} else if (!zfs_clonedup_src_unowned(sos)) {
		return (SET_ERROR(EBUSY));
	}
	if (zfsvfs != NULL) {
		err = zfs_zget(zfsvfs, sobj, &zp);
		if (err == ENOENT || err == EINVAL) {
			*resp = ZCR_SRC_STALE;
			return (0);
		}
		if (err != 0)
			return (err);
		rl = &zp->z_rangelock;
	}
	if (rl != NULL) {
		/* waits only in zfs_clone_range()'s address order */
		if (!nowait && (dlr == NULL ||
		    (uintptr_t)rl > (uintptr_t)dlr->lr_rangelock))
			lr = zfs_rangelock_enter(rl, sblkid * sblksz,
			    sblksz, RL_READER);
		else
			lr = zfs_rangelock_tryenter(rl,
			    sblkid * sblksz, sblksz, RL_READER);
		if (lr == NULL) {
			if (zp != NULL)
				zrele(zp);
			return (SET_ERROR(EAGAIN));
		}
	}
	err = zfs_clonedup_src_validate(sos, sobj, sblkid, sexp, &bp,
	    resp);
	if (err == 0 && *resp == ZCR_APPLIED) {
		*resp = ZCR_QUEUED;
		if (lr == NULL)
			return (0);
		sl = kmem_alloc(sizeof (*sl), KM_SLEEP);
		sl->zsl_lr = lr;
		sl->zsl_zp = zp;
		*lockp = sl;
		return (0);
	}
	zfs_clonedup_unlock(lr);
	if (zp != NULL)
		zrele(zp);
	return (err);
}

void
zfs_clonedup_src_unlock(void *lock)
{
	zfs_clonedup_slock_t *sl = lock;

	if (sl == NULL)
		return;
	zfs_rangelock_exit(sl->zsl_lr);
	if (sl->zsl_zp != NULL)
		zrele(sl->zsl_zp);
	kmem_free(sl, sizeof (*sl));
}

/*
 * Read and check one source block pointer inside the transaction.
 * A batch calls this once for a run of destinations that share a
 * source: the apply visits them together and they are all in this
 * transaction, so nothing can change between them.  A source whose
 * object is gone or whose pointer changed is stale.  A read that
 * fails for another reason returns its error, with ZCR_ERROR.
 */
int
zfs_clonedup_src_validate(objset_t *sos, uint64_t sobj,
    uint64_t sblkid, const blkptr_t *sexp, blkptr_t *bp,
    zfs_clonedup_result_t *resp)
{
	uint64_t sblksz = BP_GET_LSIZE(sexp);
	uint64_t soff = sblkid * sblksz;
	size_t nb = 1;
	int err;

	err = dsl_clonedup_src_in_range(sos, sobj, sblkid, sblksz);
	if (err == 0)
		err = dmu_read_l0_bps(sos, sobj, soff, sblksz, bp,
		    &nb);
	if (err == EAGAIN) {
		*resp = ZCR_SRC_DIRTY;
		return (0);
	}
	if (err != 0 && !dsl_clonedup_err_moved(err)) {
		*resp = ZCR_ERROR;
		return (err);
	}
	if (err != 0 || nb != 1 ||
	    !zfs_dedupe_bp_same_block(bp, sexp)) {
		*resp = ZCR_SRC_STALE;
		return (0);
	}
	*resp = ZCR_APPLIED;
	return (0);
}

static void
zfs_clonedup_dst_wrote(zfs_clonedup_dst_t *dst, dmu_tx_t *tx)
{
	zfs_clonedup_own_t *own = dst->zcd_own;
	uint64_t txg = dmu_tx_get_txg(tx);

	if (own != NULL)
		own->zco_txg = MAX(own->zco_txg, txg);
}

/*
 * Finish one prepared block inside a transaction the caller assigned
 * and already waited out the previous txg for.  The destination range
 * lock has been held since zfs_clonedup_dst_prepare(), so only the
 * source still needs re-checking.  A dry run passes no tx.  It gets
 * the same checks, and nothing changes.
 */
int
zfs_clonedup_dst_finish(zfs_clonedup_dst_t *dst, uint64_t blkid,
    const blkptr_t *dexp, objset_t *sos, uint64_t sobj,
    uint64_t sblkid, const blkptr_t *sexp, const blkptr_t *sval,
    dmu_tx_t *tx, zfs_clonedup_result_t *resp)
{
	objset_t *os = zfs_clonedup_dst_objset(dst);
	uint64_t object = dst->zcd_object;
	uint64_t blksz = BP_GET_LSIZE(dexp);
	uint64_t off = blkid * blksz;
	blkptr_t bp;
	dmu_buf_t *db;
	int err;

	*resp = ZCR_ERROR;

	if (sval != NULL) {
		bp = *sval;
	} else {
		err = zfs_clonedup_src_validate(sos, sobj, sblkid,
		    sexp, &bp, resp);
		if (err != 0 || *resp != ZCR_APPLIED)
			return (err);
	}
	/*
	 * dmu_brt_clone() holds the destination's buffer under
	 * VERIFY0.  The indirect above it may have left the cache
	 * since the tx hold read it, while the batch waited for the
	 * previous txg.  Hold the buffer here, where a failed read is
	 * an error for this block, and keep it until the clone is in.
	 */
	err = dmu_buf_hold_noread(os, object, off, FTAG, &db);
	if (err != 0)
		return (err);
	if (tx != NULL) {
		err = dmu_brt_clone(os, object, off, blksz, tx,
		    &bp, 1);
		zfs_clonedup_dst_wrote(dst, tx);
	}
	dmu_buf_rele(db, FTAG);
	*resp = (err == 0) ? ZCR_APPLIED : ZCR_ERROR;
	return (err);
}
