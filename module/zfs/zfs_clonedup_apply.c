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
 * in one dataset, reached one of three ways: a file in a mounted
 * filesystem through its znode, a block of an open volume through
 * its zvol_state_t, or any object of a dataset nobody else has open
 * by owning the dataset.  The first two exclude concurrent writers
 * with the object's range lock; ownership excludes them outright,
 * since mount, volume open and receive all need ownership too.
 * Everything that needs a znode or a zvol_state_t lives here;
 * dsl_clonedup.c stays at the DMU level.
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
#include <sys/zap.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>

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
	zvol_state_t	*zcd_zv;	/* volume: one open count */
	uint64_t	zcd_txg;	/* volume: last txg written */
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
 * Hold the dataset just long enough to learn its type and, for a
 * volume, its name, which the caller frees.  Snapshots are never
 * destinations.
 */
static int
zfs_clonedup_dst_lookup(dsl_pool_t *dp, uint64_t dsobj,
    dmu_objset_type_t *typep, char **namep)
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
		if (err == 0 && *typep == DMU_OST_ZVOL) {
			*namep = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN,
			    KM_SLEEP);
			dsl_dataset_name(ds, *namep);
		}
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
 * An open volume is held the way a block device opener holds it: one
 * count in zv_open_count under zv_state_lock, with zv_suspend_lock as
 * reader for as long as the count is ours.  The count is what keeps
 * zv_objset and zv_dn alive; a last close by the real opener would
 * otherwise disown the objset under us.  A closed volume, or one
 * without a minor, is EBUSY here and is written through ownership.
 * A worker may hold another volume's zv_suspend_lock, so the lookup
 * only tries zvol_state_lock and zv_suspend_lock, and a volume whose
 * locks are not free at once is EBUSY too.
 */
static int
zfs_clonedup_zvol_open(const char *name, uint64_t dsobj,
    uint64_t object, zfs_clonedup_dst_t **dstp)
{
	zvol_state_t *zv;
	zfs_clonedup_dst_t *dst;
	int err = 0;

	if (object != ZVOL_OBJ)
		return (SET_ERROR(ENOENT));

	zv = zvol_tryfind_by_name_hash(name, zvol_name_hash(name));
	if (zv == NULL)
		return (SET_ERROR(EBUSY));
	if (zv->zv_dsobj != dsobj ||
	    !rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
		mutex_exit(&zv->zv_state_lock);
		return (SET_ERROR(EBUSY));
	}

	/*
	 * Only FreeBSD sets ZVOL_EXCL, for an O_EXCL open.  Its opens
	 * refuse every other opener while the flag is set, and its
	 * closes assert that the exclusive opener holds the only
	 * count, so the apply takes no count either.  The open volume
	 * owns its objset, so the fallback to ownership is refused
	 * too, and such a volume is never deduped.
	 */
	if (zv->zv_open_count == 0 ||
	    (zv->zv_flags & (ZVOL_REMOVING | ZVOL_EXCL)) != 0)
		err = SET_ERROR(EBUSY);
	else if (zv->zv_flags & ZVOL_RDONLY)
		err = SET_ERROR(EROFS);
	else
		zv->zv_open_count++;
	mutex_exit(&zv->zv_state_lock);
	if (err != 0) {
		rw_exit(&zv->zv_suspend_lock);
		return (err);
	}

	dst = kmem_zalloc(sizeof (*dst), KM_SLEEP);
	dst->zcd_object = object;
	dst->zcd_zv = zv;
	*dstp = dst;
	return (0);
}

/* The mirror of zvol_release(): the last count out shuts the zvol. */
static void
zfs_clonedup_zvol_close(zvol_state_t *zv)
{
	ASSERT(RW_READ_HELD(&zv->zv_suspend_lock));

	mutex_enter(&zv->zv_state_lock);
	ASSERT3U(zv->zv_open_count, >, 0);
	zv->zv_open_count--;
	if (zv->zv_open_count == 0) {
		zvol_last_close(zv);
#ifdef __FreeBSD__
		wakeup(zv);
#endif
	}
	mutex_exit(&zv->zv_state_lock);
	rw_exit(&zv->zv_suspend_lock);
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
 * A dataset nobody has mounted or open is owned for the duration of
 * the hold.  Ownership is what a mount, a volume open and a receive
 * take, so no writer can appear and no range lock is needed.  EBUSY
 * means somebody owns it already, or its log has not been replayed
 * yet and the blocks on disk are not the final content.
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
	if (err == 0 && doi.doi_type != (type == DMU_OST_ZVOL ?
	    DMU_OT_ZVOL : DMU_OT_PLAIN_FILE_CONTENTS))
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
 * handle shares dst's zfs_enter().  A volume has no other object.
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
 * The mounted or open path is tried first, ownership second, and the
 * first path once more if the dataset was mounted or opened between
 * the two.  Whatever is left is EBUSY and counted as such.
 */
int
zfs_clonedup_dst_open(spa_t *spa, uint64_t dsobj, uint64_t object,
    zfs_clonedup_dst_t **dstp)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	dmu_objset_type_t type = DMU_OST_NONE;
	char *name = NULL;
	int err;

	*dstp = NULL;
	err = zfs_clonedup_dst_lookup(dp, dsobj, &type, &name);
	if (err != 0)
		goto out;

	if (type == DMU_OST_ZVOL) {
		err = zfs_clonedup_zvol_open(name, dsobj, object,
		    dstp);
		if (err == EBUSY)
			err = zfs_clonedup_own_open(spa, dsobj,
			    object, type, dstp);
		if (err == EBUSY)
			err = zfs_clonedup_zvol_open(name, dsobj,
			    object, dstp);
	} else if (type == DMU_OST_ZFS) {
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
out:
	if (name != NULL)
		kmem_free(name, ZFS_MAX_DATASET_NAME_LEN);
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
	if (dst->zcd_zv != NULL) {
		zfs_clonedup_wait_written(dst->zcd_zv->zv_objset,
		    dst->zcd_txg);
		zfs_clonedup_zvol_close(dst->zcd_zv);
	} else if (dst->zcd_os != NULL) {
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
	if (dst->zcd_zv != NULL)
		return (dst->zcd_zv->zv_objset);
	if (dst->zcd_os != NULL)
		return (dst->zcd_os);
	return (dst->zcd_zfsvfs->z_os);
}

uint64_t
zfs_clonedup_dst_object(zfs_clonedup_dst_t *dst)
{
	return (dst->zcd_object);
}

static dmu_object_type_t
zfs_clonedup_dst_type(zfs_clonedup_dst_t *dst)
{
	if (dst->zcd_zv != NULL)
		return (DMU_OT_ZVOL);
	if (dst->zcd_os != NULL)
		return (dst->zcd_dn->dn_type);
	return (DMU_OT_PLAIN_FILE_CONTENTS);
}

dsl_clonedup_kstat_id_t
zfs_clonedup_dst_kind(zfs_clonedup_dst_t *dst)
{
	if (dst->zcd_zv != NULL)
		return (DCK_DST_ZVOL);
	if (dst->zcd_os != NULL)
		return (DCK_DST_OWNED);
	return (DCK_DST_MOUNTED);
}

/*
 * True when this handle holds the dataset through dmu_objset_own(),
 * which no second worker can hold.  A mounted filesystem is pinned
 * by a reference and an open volume by a reader, and several
 * workers can hold either at once.
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
 * The three prep functions run the checks of their kind that a clone
 * and a punch share, and confirm the block still exists at that size.
 * The checks only a clone makes are zfs_clonedup_dst_clone_check()'s.
 * They return a policy errno, or 0 with *gop set when the block may
 * be written and, for files and volumes, *lrp holding its range
 * lock.  With *gop clear, *resp says why not.  With nowait set, a
 * file or a volume returns EAGAIN, holding nothing, where it would
 * otherwise wait for the range lock.  They never write out a file's
 * cached pages.  That is safe: writeback later stores a page dirtied
 * through a mapping over the clone.  With dryrun set they write
 * nothing, and the range lock is a reader's.
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
zfs_clonedup_zvol_prep(zfs_clonedup_dst_t *dst, uint64_t off,
    uint64_t blksz, boolean_t nowait, boolean_t dryrun,
    zfs_locked_range_t **lrp, boolean_t *gop,
    zfs_clonedup_result_t *resp)
{
	zvol_state_t *zv = dst->zcd_zv;

	if (zv->zv_flags & ZVOL_RDONLY)
		return (SET_ERROR(EROFS));

	*lrp = zfs_clonedup_lock(&zv->zv_rangelock, off, blksz,
	    nowait, dryrun);
	if (*lrp == NULL)
		return (SET_ERROR(EAGAIN));
	if (zv->zv_volblocksize != blksz || off >= zv->zv_volsize) {
		zfs_rangelock_exit(*lrp);
		*lrp = NULL;
		*resp = ZCR_DST_STALE;
		return (0);
	}
	*gop = B_TRUE;
	return (0);
}

/*
 * zfs set volsize takes zv_suspend_lock as a reader, as the apply
 * does, and leaves the range lock alone.  zvol_update_volsize()
 * writes the new size, waits for its txg to sync, and then frees
 * the blocks past it.  zv_volsize changes only after the free, and
 * the free never lowers the in-core dn_maxblkid, so neither shows a
 * shrink in progress.  The size in the volume's ZAP does.  The batch
 * waited for the txg before its own, so a shrink whose size this
 * read misses frees in a later txg than the clone, and frees the
 * clone too.  A shrink and a regrow that both came after the prepare
 * leave a size that covers the block, while the block itself is gone
 * or still being freed.  Reading the block pointer again catches
 * that.
 */
static int
zfs_clonedup_zvol_recheck(zfs_clonedup_dst_t *dst, uint64_t off,
    const blkptr_t *dexp)
{
	objset_t *os = dst->zcd_zv->zv_objset;
	uint64_t blksz = BP_GET_LSIZE(dexp);
	uint64_t volsize;
	blkptr_t bp;
	size_t nb = 1;
	int err;

	err = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);
	if (err == 0 && off >= volsize)
		err = SET_ERROR(ESTALE);
	if (err == 0)
		err = dmu_read_l0_bps(os, ZVOL_OBJ, off, blksz, &bp,
		    &nb);
	if (err == 0 && (nb != 1 ||
	    !zfs_dedupe_bp_same_block(&bp, dexp)))
		err = SET_ERROR(ESTALE);
	return (err);
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
 * The checks a clone makes and a punch does not.  A punch only frees
 * the block, as a hole punch through zfs_space() does, and needs
 * neither matching properties nor room under a quota.  A clone takes
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

	if (dst->zcd_zv != NULL) {
		dmu_tx_hold_clone_by_dnode(tx, dst->zcd_zv->zv_dn,
		    off, blksz, blksz);
		return;
	}
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

	if (dst->zcd_zv != NULL)
		err = zfs_clonedup_zvol_prep(dst, off, blksz, nowait,
		    dryrun, &lr, &go, resp);
	else if (dst->zcd_os != NULL)
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
    uint64_t blkid, uint64_t blksz, boolean_t punch)
{
	uint64_t off = blkid * blksz;

	if (punch)
		dmu_tx_hold_free(tx, dst->zcd_object, off, blksz);
	else
		zfs_clonedup_tx_hold_clone(tx, dst, off, blksz);
}

void
zfs_clonedup_dst_unlock(void *lock)
{
	zfs_clonedup_unlock((zfs_locked_range_t *)lock);
}

struct zfs_clonedup_spin {
	zfsvfs_t	*zsp_zfsvfs;
	zvol_state_t	*zsp_zv;
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
	if (dst->zcd_zv != NULL)
		return ((uintptr_t)dst->zcd_zv);
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

static zvol_state_t *
zfs_clonedup_zvol_pin(const char *name, uint64_t dsobj,
    uintptr_t want)
{
	zvol_state_t *zv;

	zv = zvol_tryfind_by_name_hash(name, zvol_name_hash(name));
	if (zv == NULL)
		return (NULL);
	if ((uintptr_t)zv != want || zv->zv_dsobj != dsobj ||
	    zv->zv_open_count == 0 ||
	    (zv->zv_flags & ZVOL_REMOVING) != 0 ||
	    !rw_tryenter(&zv->zv_suspend_lock, RW_READER)) {
		mutex_exit(&zv->zv_state_lock);
		return (NULL);
	}
	zv->zv_open_count++;
	mutex_exit(&zv->zv_state_lock);
	return (zv);
}

int
zfs_clonedup_src_pin(objset_t *sos, zfs_clonedup_dst_t *dst,
    struct zfs_clonedup_spin **pinp)
{
	dsl_dataset_t *ds = dmu_objset_ds(sos);
	dsl_pool_t *dp = dmu_objset_pool(sos);
	uintptr_t below = zfs_clonedup_dst_pinned(dst);
	struct zfs_clonedup_spin *pin;
	zfsvfs_t *zfsvfs = NULL;
	zvol_state_t *zv = NULL;
	uintptr_t at = 0;
	char *name;

	*pinp = NULL;
	if (ds->ds_is_snapshot ||
	    (dst != NULL && zfs_clonedup_dst_objset(dst) == sos) ||
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
	} else if (dmu_objset_type(sos) == DMU_OST_ZVOL) {
		name = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
		dsl_pool_config_enter(dp, FTAG);
		dsl_dataset_name(ds, name);
		dsl_pool_config_exit(dp, FTAG);
		zv = zvol_tryfind_by_name_hash(name,
		    zvol_name_hash(name));
		if (zv != NULL) {
			if (zv->zv_dsobj == ds->ds_object &&
			    zv->zv_open_count > 0 &&
			    (zv->zv_flags & ZVOL_REMOVING) == 0)
				at = (uintptr_t)zv;
			mutex_exit(&zv->zv_state_lock);
		}
		zv = NULL;
		if (at != 0 && at > below)
			zv = zfs_clonedup_zvol_pin(name,
			    ds->ds_object, at);
		kmem_free(name, ZFS_MAX_DATASET_NAME_LEN);
		if (at != 0 && at < below)
			return (SET_ERROR(EDEADLK));
	}
	if (zfsvfs == NULL && zv == NULL)
		return (SET_ERROR(EBUSY));
	pin = kmem_zalloc(sizeof (*pin), KM_SLEEP);
	pin->zsp_zfsvfs = zfsvfs;
	pin->zsp_zv = zv;
	*pinp = pin;
	return (0);
}

void
zfs_clonedup_src_unpin(struct zfs_clonedup_spin *pin)
{
	if (pin == NULL)
		return;
	if (pin->zsp_zv != NULL) {
		zfs_clonedup_zvol_close(pin->zsp_zv);
	} else {
		zfs_exit(pin->zsp_zfsvfs, &zfs_clonedup_tag);
		zfs_vfs_rele(pin->zsp_zfsvfs);
	}
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
		if (pin->zsp_zv != NULL)
			rl = &pin->zsp_zv->zv_rangelock;
		zfsvfs = pin->zsp_zfsvfs;
	} else if (same && dst->zcd_zv != NULL) {
		rl = &dst->zcd_zv->zv_rangelock;
	} else if (same) {
		if (dst->zcd_os == NULL)
			zfsvfs = dst->zcd_zfsvfs;
	} else if (!dmu_objset_ds(sos)->ds_is_snapshot &&
	    !zfs_clonedup_src_unowned(sos)) {
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
	else if (dst->zcd_zv != NULL)
		dst->zcd_txg = MAX(dst->zcd_txg, txg);
}

/*
 * Finish one prepared block inside a transaction the caller assigned
 * and, unless the source is a snapshot, already waited out the
 * previous txg for.  The destination range lock has been held since
 * zfs_clonedup_dst_prepare(), so only the source and a volume that
 * shrank behind the range lock still need re-checking.  A dry run
 * passes no tx.  It gets the same checks, and nothing changes.
 */
int
zfs_clonedup_dst_finish(zfs_clonedup_dst_t *dst, uint64_t blkid,
    const blkptr_t *dexp, objset_t *sos, uint64_t sobj,
    uint64_t sblkid, const blkptr_t *sexp, const blkptr_t *sval,
    boolean_t punch, dmu_tx_t *tx, zfs_clonedup_result_t *resp)
{
	objset_t *os = zfs_clonedup_dst_objset(dst);
	uint64_t object = dst->zcd_object;
	uint64_t blksz = BP_GET_LSIZE(dexp);
	uint64_t off = blkid * blksz;
	blkptr_t bp;
	dmu_buf_t *db;
	int err;

	*resp = ZCR_ERROR;

	if (punch && tx == NULL) {
		*resp = ZCR_APPLIED;
		return (0);
	}
	if (punch) {
		err = dmu_free_range(os, object, off, blksz, tx);
		zfs_clonedup_dst_wrote(dst, tx);
		*resp = (err == 0) ? ZCR_APPLIED : ZCR_ERROR;
		return (err);
	}

	if (sval != NULL) {
		bp = *sval;
	} else {
		err = zfs_clonedup_src_validate(sos, sobj, sblkid,
		    sexp, &bp, resp);
		if (err != 0 || *resp != ZCR_APPLIED)
			return (err);
	}
	err = dst->zcd_zv == NULL ? 0 :
	    zfs_clonedup_zvol_recheck(dst, off, dexp);
	if (err == EAGAIN) {
		*resp = ZCR_DST_DIRTY;
		return (0);
	}
	if (err != 0 && !dsl_clonedup_err_moved(err)) {
		*resp = ZCR_ERROR;
		return (err);
	}
	if (err != 0) {
		*resp = ZCR_DST_STALE;
		return (0);
	}
	/*
	 * A pointer's type names the object holding it, so the copy a
	 * volume takes of a file's block, or a file of a volume's,
	 * carries the destination's.  The data and checksum are one.
	 */
	BP_SET_TYPE(&bp, zfs_clonedup_dst_type(dst));
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

/*
 * Replace block blkid of the destination, expected to still be dexp,
 * with a clone of the source block, expected to still be sexp.  With
 * punch set the block is known to be all zeros and is freed instead.
 * Returns a policy errno (EPERM, EROFS, and for a clone EDQUOT or
 * EXDEV) or an I/O errno; *resp says what happened otherwise.  With
 * dryrun set it makes the same checks and writes nothing.
 *
 * The destination is protected by its range lock or by ownership.
 * The source is not locked at all: a writer may already hold a
 * transaction in the txg before ours without having dirtied the block
 * yet, and a clone of the bp it is about to free would reference
 * freed space once both txgs sync.  Waiting for the previous txg
 * after assigning ours makes every such writer visible, either as a
 * dirty record or as a changed bp.  Writers in our own txg dirty
 * after our clone, and brt_pending_apply() runs before any free of a
 * txg, so those are safe.  A writer in a later txg either dirtied the
 * block before we read its bp, which dmu_read_l0_bps() reports as
 * EAGAIN, or frees it in a txg that syncs after ours.  A punch frees
 * a block of its own dataset, so it waits only to learn whether a
 * snapshot holds that block, see dsl_clonedup_punch_held().
 */
int
zfs_clonedup_dst_apply(zfs_clonedup_dst_t *dst, uint64_t blkid,
    const blkptr_t *dexp, objset_t *sos, uint64_t sobj,
    uint64_t sblkid, const blkptr_t *sexp, boolean_t punch,
    boolean_t dryrun, zfs_clonedup_result_t *resp)
{
	objset_t *os = zfs_clonedup_dst_objset(dst);
	dsl_pool_t *dp = dmu_objset_pool(os);
	uint64_t blksz = BP_GET_LSIZE(dexp);
	boolean_t ready = B_FALSE;
	boolean_t held = B_FALSE;
	void *lock = NULL;
	dmu_tx_t *tx;
	int err;

	err = zfs_clonedup_dst_prepare(dst, blkid, dexp, B_FALSE,
	    dryrun, &ready, &lock, resp);
	if (err != 0 || !ready)
		return (err);
	if (!punch)
		err = zfs_clonedup_dst_clone_check(dst, sos);
	if (err == 0 && dryrun) {
		err = zfs_clonedup_dst_finish(dst, blkid, dexp, sos,
		    sobj, sblkid, sexp, NULL, punch, NULL, resp);
	}
	if (err != 0 || dryrun) {
		zfs_clonedup_dst_unlock(lock);
		return (err);
	}

	do {
		tx = dmu_tx_create(os);
		zfs_clonedup_dst_tx_hold(tx, dst, blkid, blksz,
		    punch);
		/* as dmu_free_long_range() marks its frees */
		if (punch)
			dmu_tx_mark_netfree(tx);
		err = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (err != 0) {
			dmu_tx_abort(tx);
			zfs_clonedup_dst_unlock(lock);
			return (err);
		}
	} while (!dsl_clonedup_apply_paced(tx, 1));

	if (punch) {
		err = dsl_clonedup_punch_held(os, dexp, tx, &held);
	} else {
		err = txg_wait_synced_flags(dp,
		    dmu_tx_get_txg(tx) - 1, TXG_WAIT_SUSPEND);
	}
	if (err != 0) {
		dmu_tx_commit(tx);
		zfs_clonedup_dst_unlock(lock);
		return (err);
	}
	if (held) {
		*resp = ZCR_SRC_STALE;
	} else {
		err = zfs_clonedup_dst_finish(dst, blkid, dexp, sos,
		    sobj, sblkid, sexp, NULL, punch, tx, resp);
	}
	dmu_tx_commit(tx);
	zfs_clonedup_dst_unlock(lock);
	return (err);
}
