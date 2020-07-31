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
 *
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (C) 2011 Lawrence Livermore National Security, LLC.
 * Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 * LLNL-CODE-403049.
 * Rewritten for Linux by:
 *   Rohan Puri <rohan.puri15@gmail.com>
 *   Brian Behlendorf <behlendorf1@llnl.gov>
 * Copyright (c) 2013 by Delphix. All rights reserved.
 * Copyright 2015, OmniTI Computer Consulting, Inc. All rights reserved.
 * Copyright (c) 2018 George Melikov. All Rights Reserved.
 * Copyright (c) 2019 Datto, Inc. All rights reserved.
 * Copyright (c) 2020 Jorgen Lundman. All rights reserved.
 * Portions Copyright 2022 Andrew Innes <andrew.c12@gmail.com>
 */

/*
 * ZFS control directory (a.k.a. ".zfs")
 *
 * This directory provides a common location for all ZFS meta-objects.
 * Currently, this is only the 'snapshot' and 'shares' directory, but this may
 * expand in the future.  The elements are built dynamically, as the hierarchy
 * does not actually exist on disk.
 *
 * For 'snapshot', we don't want to have all snapshots always mounted, because
 * this would take up a huge amount of space in /etc/mnttab.  We have three
 * types of objects:
 *
 *	ctldir ------> snapshotdir -------> snapshot
 *                                             |
 *                                             |
 *                                             V
 *                                         mounted fs
 *
 * The 'snapshot' node contains just enough information to lookup '..' and act
 * as a mountpoint for the snapshot.  Whenever we lookup a specific snapshot, we
 * perform an automount of the underlying filesystem and return the
 * corresponding vnode.
 *
 * All mounts are handled automatically by an user mode helper which invokes
 * the mount procedure.  Unmounts are handled by allowing the mount
 * point to expire so the kernel may automatically unmount it.
 *
 * The '.zfs', '.zfs/snapshot', and all directories created under
 * '.zfs/snapshot' (ie: '.zfs/snapshot/<snapname>') all share the same
 * zfsvfs_t as the head filesystem (what '.zfs' lives under).
 *
 * File systems mounted on top of the '.zfs/snapshot/<snapname>' paths
 * (ie: snapshots) are complete ZFS filesystems and have their own unique
 * zfsvfs_t.  However, the fsid reported by these mounts will be the same
 * as that used by the parent zfsvfs_t to make NFS happy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/dirent.h>
#include <sys/sysmacros.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_deleg.h>
#include <sys/zpl.h>
#include <sys/mntent.h>
#include <sys/fm/fs/zfs.h>
#include "zfs_namecheck.h"

extern kmem_cache_t *znode_cache;
extern uint64_t vnop_num_vnodes;

/*
 * Windows differences;
 *
 * We don't have 'shares' directory, so only 'snapshot' is relevant.
 *
 * - see zfs_ctldir_snapdir.c
 *
 * All vnodes point to znode_t, no special case nodes.
 */



/* List of zfsctl mounts waiting to be mounted */
static kmutex_t zfsctl_mounts_lock;
static list_t zfsctl_mounts_list;
struct zfsctl_mounts_waiting {
	kmutex_t zcm_lock;
	kcondvar_t zcm_cv;
	list_node_t zcm_node;
	char zcm_name[ZFS_MAX_DATASET_NAME_LEN];
};
typedef struct zfsctl_mounts_waiting zfsctl_mounts_waiting_t;


/*
 * Control Directory Tunables (.zfs)
 */
int zfs_expire_snapshot = ZFSCTL_EXPIRE_SNAPSHOT;
int zfs_admin_snapshot = 1;
int zfs_auto_snapshot = 1;

static kmutex_t		zfsctl_unmount_lock;
static kcondvar_t	zfsctl_unmount_cv;
static boolean_t	zfsctl_unmount_thread_exit;

static kmutex_t zfsctl_unmount_list_lock;
static list_t zfsctl_unmount_list;

struct zfsctl_unmount_delay {
	char		*se_name;	/* full snapshot name */
	spa_t		*se_spa;	/* pool spa */
	struct vnode	*se_vnode;
	uint64_t	se_objsetid;	/* snapshot objset id */
	time_t		se_time;
	list_node_t	se_nodelink;
};
typedef struct zfsctl_unmount_delay zfsctl_unmount_delay_t;

static struct vnode *
zfsctl_vnode_lookup(zfsvfs_t *zfsvfs, uint64_t id,
    char *name);

/*
 * Check if the given vnode is a part of the virtual .zfs directory.
 */
boolean_t
zfsctl_is_node(znode_t *zp)
{
	return (zp->z_is_ctldir);
}

/*
 * /.zfs/snapshot/nameofsnapshot/
 * -------------------^
 */
boolean_t
zfsctl_is_leafnode(znode_t *zp)
{
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	return (zp->z_is_ctldir && zp->z_id != ZFSCTL_INO_ROOT &&
	    zp->z_id != ZFSCTL_INO_SNAPDIR &&
	    (zp->z_id >= zfsvfs->z_ctldir_startid) &&
	    (zp->z_id <= ZFSCTL_INO_SNAPDIRS));
}

typedef int (**vnode_operations)(void *);


/*
 * Allocate a new vnode with the passed id and ops.
 */
static struct vnode *
zfsctl_vnode_alloc(zfsvfs_t *zfsvfs, uint64_t id,
    char *name)
{
	timestruc_t	now;
	struct vnode *vp = NULL;
	znode_t *zp = NULL;
	int flags = 0;

	dprintf("%s\n", __func__);

	zp = kmem_cache_alloc(znode_cache, KM_SLEEP);

	gethrestime(&now);
	ASSERT3P(zp->z_dirlocks, ==, NULL);
	ASSERT3P(zp->z_acl_cached, ==, NULL);
	ASSERT3P(zp->z_xattr_cached, ==, NULL);
	zp->z_zfsvfs = zfsvfs;
	zp->z_id = id;
	zp->z_unlinked = B_FALSE;
	zp->z_atime_dirty = B_FALSE;
	zp->z_zn_prefetch = B_FALSE;
	zp->z_is_sa = B_FALSE;
	zp->z_is_mapped = B_FALSE;
	zp->z_is_ctldir = B_TRUE;
	zp->z_sa_hdl = NULL;
	zp->z_blksz = 0;
	zp->z_seq = 0;
	zp->z_mapcnt = 0;
	zp->z_size = 0;
	zp->z_pflags = 0;
	zp->z_mode = 0;
	zp->z_sync_cnt = 0;
	zp->z_gen = 0;
	zp->z_links = 2;
	zp->z_mode = (S_IFDIR | (S_IRWXU|S_IRWXG|S_IRWXO));
	zp->z_uid = 0;
	zp->z_gid = 0;
	ZFS_TIME_ENCODE(&now, zp->z_atime);

	zp->z_snap_mount_time = 0; /* Allow automount attempt */
	zp->z_name_cache = NULL;
	zp->z_name_len = 0;

	dprintf("%s zp %p with vp %p zfsvfs %p vfs %p\n", __func__,
	    zp, vp, zfsvfs, zfsvfs->z_vfs);

	/* Tag root directory */
	if (id == ZFSCTL_INO_ROOT)
		flags |= VNODE_MARKROOT;

	/*
	 * This creates a vnode with VSYSTEM set, this is so that unmount's
	 * vflush() (called before our vfs_unmount) will pass (and not block
	 * waiting for the usercount ref to be released). We then release the
	 * VROOT vnode in zfsctl_destroy, and release the usercount ref.
	 * Because of this, we need to call vnode_recycle() ourselves in destroy
	 */

	/* We need parent */
	znode_t *parentzp = NULL;
	struct vnode *parentvp = NULL;
	int error = 0;
	if (id == ZFSCTL_INO_ROOT)
		error = zfs_zget(zfsvfs, zfsvfs->z_root, &parentzp);
	else if (id == ZFSCTL_INO_SNAPDIR)
		parentvp = zfsctl_root(zp);
	else
		parentvp = zfsctl_vnode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIR,
		    ZFS_SNAPDIR_NAME);

	if (error && !parentvp) {
		dprintf("%s: unable to get parent?", __func__);
		return (SET_ERROR(EINVAL));
	}

	if (!parentvp && parentzp)
		parentvp = ZTOV(parentzp);

	vnode_create(zfsvfs->z_vfs, parentvp,
	    zp, VDIR, flags, &vp);

	VN_RELE(parentvp);

	dprintf("Assigned zp %p with vp %p zfsvfs %p\n", zp, vp, zp->z_zfsvfs);

	zp->z_vid = vnode_vid(vp);
	zp->z_vnode = vp;

	// Update startid before buildpath.
	if (id < zfsvfs->z_ctldir_startid)
		zfsvfs->z_ctldir_startid = id;

	// Build fullpath string here, for Notifications & set_name_information
	if (zfs_build_path(zp, NULL, &zp->z_name_cache,
	    &zp->z_name_len, &zp->z_name_offset) == -1) {
		dprintf("%s: failed to build fullpath\n", __func__);
	}

	zfs_set_security(vp, NULL);

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	membar_producer();
	mutex_exit(&zfsvfs->z_znodes_lock);

	return (vp);
}

/*
 * Lookup the vnode with given id, it will be allocated if needed.
 */
static struct vnode *
zfsctl_vnode_lookup(zfsvfs_t *zfsvfs, uint64_t id,
    char *name)
{
	struct vnode *ip = NULL;
	int error = 0;

	dprintf("%s\n", __func__);

	while (ip == NULL) {

		error = zfs_vfs_vget(zfsvfs->z_vfs, id, &ip, NULL);
		if (error == 0 && ip != NULL)
			break;

		/* May fail due to concurrent zfsctl_vnode_alloc() */
		ip = zfsctl_vnode_alloc(zfsvfs, id, name);
	}

	return (ip);
}

/*
 * Create the '.zfs' directory.  This directory is cached as part of the VFS
 * structure.  This results in a hold on the zfsvfs_t.  The code in zfs_umount()
 * therefore checks against a vfs_count of 2 instead of 1.  This reference
 * is removed when the ctldir is destroyed in the unmount.  All other entities
 * under the '.zfs' directory are created dynamically as needed.
 *
 * Because the dynamically created '.zfs' directory entries assume the use
 * of 64-bit vnode numbers this support must be disabled on 32-bit systems.
 */
int
zfsctl_create(zfsvfs_t *zfsvfs)
{
	ASSERT(zfsvfs->z_ctldir == NULL);

	dprintf("%s\n", __func__);

	/* Create root node, tagged with VSYSTEM - see above */
	zfsvfs->z_ctldir = zfsctl_vnode_alloc(zfsvfs, ZFSCTL_INO_ROOT,
	    ZFS_CTLDIR_NAME);
	if (zfsvfs->z_ctldir == NULL)
		return (SET_ERROR(ENOENT));

	vnode_ref(zfsvfs->z_ctldir);
	VN_RELE(zfsvfs->z_ctldir);

	dprintf("%s: done %p\n", __func__, zfsvfs->z_ctldir);

	return (0);
}

/*
 * Destroy the '.zfs' directory or remove a snapshot from
 * zfs_snapshots_by_name. Only called when the filesystem is unmounted.
 */
void
zfsctl_destroy(zfsvfs_t *zfsvfs)
{
	if (zfsvfs->z_ctldir) {
		if (VN_HOLD(zfsvfs->z_ctldir) == 0) {
			vnode_rele(zfsvfs->z_ctldir);
			/* Because tagged VSYSTEM, we manually call recycle */
			zfsvfs->z_ctldir->v_flags &= ~VNODE_MARKROOT; // Hack
			VN_RELE(zfsvfs->z_ctldir);
		}
		zfsvfs->z_ctldir = NULL;
	}
}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
struct vnode *
zfsctl_root(znode_t *zp)
{
	VN_HOLD(ZTOZSB(zp)->z_ctldir);
	return (ZTOZSB(zp)->z_ctldir);
}

/* Given a entry in .zfs, find its parent */
struct vnode *
zfs_root_dotdot(struct vnode *vp)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	znode_t *retzp = NULL;
	struct vnode *retvp = NULL;
	int error = 0;

	dprintf("%s: for id %llu\n", __func__, zp->z_id);

	if (zp->z_id == ZFSCTL_INO_ROOT)
		error = zfs_zget(zfsvfs, zfsvfs->z_root, &retzp);
	else if (zp->z_id == ZFSCTL_INO_SNAPDIR)
		retvp = zfsctl_root(zp);
	else
		retvp = zfsctl_vnode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIR,
		    ZFS_SNAPDIR_NAME);

	if (retzp != NULL)
		return (ZTOV(retzp));
	if (retvp != NULL)
		return (retvp);

	return (NULL);
}

/*
 * Special case the handling of "..".
 */
int
zfsctl_root_lookup(struct vnode *dvp, char *name, znode_t **zpp,
    int flags, cred_t *cr, int *direntflags, struct componentname *realpnp)
{
	znode_t *dzp = VTOZ(dvp);
	zfsvfs_t *zfsvfs = ZTOZSB(dzp);
	int error = 0;
	uint64_t id;
	struct vnode *vp;

	dprintf("%s: '%s'\n", __func__, name);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (strcmp(name, "..") == 0) {
		vp = zfs_root_dotdot(dvp);
	} else if (strcmp(name, ZFS_SNAPDIR_NAME) == 0) {
		vp = zfsctl_vnode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIR,
		    name);
	} else {
		error = dmu_snapshot_lookup(zfsvfs->z_os, name, &id);
		if (error != 0)
			goto out;
		vp = zfsctl_vnode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIRS - id,
		    name);
	}

	if (vp == NULL)
		error = SET_ERROR(ENOENT);
	else
		*zpp = VTOZ(vp);

out:
	zfs_exit(zfsvfs, FTAG);

	return (error);
}

int
zfsctl_vnop_readdir_root(vnode_t *vp, emitdir_ptr_t *ctx, cred_t *cr,
    zfs_dirlist_t *zccb, int flags)
{
	int error = 0;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	dprintf("%s\n", __func__);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	ctx->numdirent = 0;

	while (ctx->offset < 3 && error == 0) {

		switch (ctx->offset) {
		case 0: /* "." */
			error = zfs_readdir_emitdir(zfsvfs, ".",
			    ctx, zccb, ZFSCTL_INO_ROOT);
			break;

		case 1: /* ".." */
			error = zfs_readdir_emitdir(zfsvfs, "..",
			    ctx, zccb, zfsvfs->z_root);
			break;

		case 2:
			error = zfs_readdir_emitdir(zfsvfs, ZFS_SNAPDIR_NAME,
			    ctx, zccb, ZFSCTL_INO_SNAPDIR);
			break;
		}

		if (error == 0) {
			ctx->numdirent++;
		} else if (error == ENOSPC) { /* stop iterating */
			break;
		} else {
			/* other error, skip over entry */
			error = 0;
		}

		ctx->offset++;
	}

	if (error == ENOENT) {
		dprintf("end of snapshots reached\n");
	}

	if (error != 0) {
		dprintf("emit error\n");
	}

	zfs_readdir_complete(ctx);

	/* Finished without error? Set EOF */
	if (ctx->offset >= 3 && error == 0) {
		error = ENOENT;
		dprintf("Setting eof\n");
	}

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

int
zfsctl_vnop_readdir_snapdir(vnode_t *vp, emitdir_ptr_t *ctx, cred_t *cr,
    zfs_dirlist_t *zccb, int flags)
{
	int error = 0;
	boolean_t case_conflict;
	uint64_t id;
	char snapname[MAXNAMELEN];
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	dprintf("%s\n", __func__);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	while (error == 0) {

		switch (ctx->offset) {
		case 0: /* "." */
			error = zfs_readdir_emitdir(zfsvfs, ".",
			    ctx, zccb, ZFSCTL_INO_SNAPDIR);
			break;

		case 1: /* ".." */
			error = zfs_readdir_emitdir(zfsvfs, "..",
			    ctx, zccb, ZFSCTL_INO_ROOT);
			break;

		default:
			dsl_pool_config_enter(dmu_objset_pool(zfsvfs->z_os),
			    FTAG);
			error = dmu_snapshot_list_next(zfsvfs->z_os,
			    MAXNAMELEN, snapname, &id, &ctx->offset,
			    &case_conflict);
			dsl_pool_config_exit(dmu_objset_pool(zfsvfs->z_os),
			    FTAG);
			if (error)
				break;

			error = zfs_readdir_emitdir(zfsvfs, snapname,
			    ctx, zccb, ZFSCTL_INO_SNAPDIRS - id);
			break;
		}

		if (error != 0) {
			dprintf("emit error\n");
			break;
		}

		ctx->offset++;
	}

	zfs_readdir_complete(ctx);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}


/* We need to spit out a valid "." ".." entries for mount to work */
int
zfsctl_vnop_readdir_snapdirs(vnode_t *vp, emitdir_ptr_t *ctx, cred_t *cr,
    zfs_dirlist_t *zccb, int flags)
{
	int error = 0;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	while (error == 0) {

		switch (ctx->offset) {
		case 0: /* "." */
			error = zfs_readdir_emitdir(zfsvfs, ".",
			    ctx, zccb, zp->z_id);
			break;

		case 1: /* ".." */
			error = zfs_readdir_emitdir(zfsvfs, "..",
			    ctx, zccb, ZFSCTL_INO_SNAPDIR);
			break;

		default:
			error = ENOENT;
			break;
		}

		if (error != 0) {
			dprintf("emit error\n");
			break;
		}

		ctx->offset++;
	}

	zfs_exit(zfsvfs, FTAG);

	zfs_readdir_complete(ctx);

	return (error);
}

int
zfsctl_readdir(vnode_t *vp, emitdir_ptr_t *ctx, cred_t *cr,
    zfs_dirlist_t *zccb, int flags)
{
	znode_t *zp = VTOZ(vp);

	dprintf("%s\n", __func__);

	/* Which directory are we to output? */
	switch (zp->z_id) {
	case ZFSCTL_INO_ROOT:
		return (zfsctl_vnop_readdir_root(vp, ctx, cr,
		    zccb, flags));
	case ZFSCTL_INO_SNAPDIR:
		return (zfsctl_vnop_readdir_snapdir(vp, ctx, cr,
		    zccb, flags));
	default:
		return (zfsctl_vnop_readdir_snapdirs(vp, ctx, cr,
		    zccb, flags));
	break;
	}
	panic("%s: weird snapshot state\n", __func__);
	return (EINVAL);
}


int
zfsctl_vnop_getattr(struct vnop_getattr_args *ap)
#if 0
	struct vnop_getattr_args {
		struct vnode	*a_vp;
		struct vnode_vattr *a_vap;
		vfs_context_t	a_context;
	};
#endif
{
	vattr_t *vap = ap->a_vap;
	struct vnode *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	timestruc_t	now;
	int error = 0;

	dprintf("%s: active x%llx\n", __func__, vap->va_active);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	gethrestime(&now);
#if 0 // WIN32 me
	if (VATTR_IS_ACTIVE(vap, va_rdev))
		VATTR_RETURN(vap, va_rdev, zfsvfs->z_rdev);
	if (VATTR_IS_ACTIVE(vap, va_nlink))
		VATTR_RETURN(vap, va_nlink,
		    vnode_isdir(vp) ? zp->z_size : zp->z_links);
	if (VATTR_IS_ACTIVE(vap, va_total_size))
		VATTR_RETURN(vap, va_total_size, 512);
	if (VATTR_IS_ACTIVE(vap, va_total_alloc))
		VATTR_RETURN(vap, va_total_alloc, 512);
	if (VATTR_IS_ACTIVE(vap, va_data_size))
		VATTR_RETURN(vap, va_data_size, 0);
	if (VATTR_IS_ACTIVE(vap, va_data_alloc))
		VATTR_RETURN(vap, va_data_alloc, 0);
	if (VATTR_IS_ACTIVE(vap, va_iosize))
		VATTR_RETURN(vap, va_iosize, 512);
	if (VATTR_IS_ACTIVE(vap, va_uid))
		VATTR_RETURN(vap, va_uid, 0);
	if (VATTR_IS_ACTIVE(vap, va_gid))
		VATTR_RETURN(vap, va_gid, 0);
	if (VATTR_IS_ACTIVE(vap, va_mode))
		VATTR_RETURN(vap, va_mode, S_IFDIR |
		    S_IRUSR | S_IXUSR | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
	if (VATTR_IS_ACTIVE(vap, va_flags))
		VATTR_RETURN(vap, va_flags, zfs_getbsdflags(zp));

	if (VATTR_IS_ACTIVE(vap, va_acl)) {
		VATTR_RETURN(vap, va_uuuid, kauth_null_guid);
		VATTR_RETURN(vap, va_guuid, kauth_null_guid);
		VATTR_RETURN(vap, va_acl, NULL);
	}

	// crtime, atime, mtime, ctime, btime
	uint64_t timez[2];
	timez[0] = zfsvfs->z_mount_time;
	timez[1] = 0;

	if (VATTR_IS_ACTIVE(vap, va_create_time)) {
		ZFS_TIME_DECODE(&vap->va_create_time, timez);
		VATTR_SET_SUPPORTED(vap, va_create_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_access_time)) {
		ZFS_TIME_DECODE(&vap->va_access_time, timez);
		VATTR_SET_SUPPORTED(vap, va_access_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_modify_time)) {
		ZFS_TIME_DECODE(&vap->va_modify_time, timez);
		VATTR_SET_SUPPORTED(vap, va_modify_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_change_time)) {
		ZFS_TIME_DECODE(&vap->va_change_time, timez);
		VATTR_SET_SUPPORTED(vap, va_change_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_backup_time)) {
		ZFS_TIME_DECODE(&vap->va_backup_time, timez);
		VATTR_SET_SUPPORTED(vap, va_backup_time);
	}
	if (VATTR_IS_ACTIVE(vap, va_addedtime)) {
		ZFS_TIME_DECODE(&vap->va_addedtime, timez);
		VATTR_SET_SUPPORTED(vap, va_addedtime);
	}

	if (VATTR_IS_ACTIVE(vap, va_fileid))
		VATTR_RETURN(vap, va_fileid, zp->z_id);
	if (VATTR_IS_ACTIVE(vap, va_linkid))
		VATTR_RETURN(vap, va_linkid, zp->z_id);
	if (VATTR_IS_ACTIVE(vap, va_parentid)) {
		switch (zp->z_id) {
			case ZFSCTL_INO_ROOT:
				// ".zfs" parent is mount, 2 on osx
				VATTR_RETURN(vap, va_linkid, 2);
				break;
			case ZFSCTL_INO_SNAPDIR:
				// ".zfs/snapshot" parent is ".zfs"
				VATTR_RETURN(vap, va_linkid, ZFSCTL_INO_ROOT);
				break;
			default:
				// ".zfs/snapshot/$name" parent ".zfs/snapshot"
				VATTR_RETURN(vap, va_linkid,
				    ZFSCTL_INO_SNAPDIR);
				break;
		}
	}
	if (VATTR_IS_ACTIVE(vap, va_fsid))
		VATTR_RETURN(vap, va_fsid, zfsvfs->z_rdev);

	if (VATTR_IS_ACTIVE(vap, va_filerev))
		VATTR_RETURN(vap, va_filerev, 0);
	if (VATTR_IS_ACTIVE(vap, va_gen))
		VATTR_RETURN(vap, va_gen, zp->z_gen);
	if (VATTR_IS_ACTIVE(vap, va_type))
		VATTR_RETURN(vap, va_type, vnode_vtype(ZTOV(zp)));
	if (VATTR_IS_ACTIVE(vap, va_name)) {
		strlcpy(vap->va_name, zp->z_name_cache, MAXPATHLEN);
		VATTR_SET_SUPPORTED(vap, va_name);
	}

	/* Don't include '.' and '..' in the number of entries */
	if (VATTR_IS_ACTIVE(vap, va_nchildren) && vnode_isdir(vp)) {
		VATTR_RETURN(vap, va_nchildren,
		    zp->z_links > 3 ? zp->z_links-2 : 1);
	}
	if (VATTR_IS_ACTIVE(vap, va_dirlinkcount) && vnode_isdir(vp))
		VATTR_RETURN(vap, va_dirlinkcount, 1);

#ifdef VNODE_ATTR_va_fsid64
	if (VATTR_IS_ACTIVE(vap, va_fsid64)) {
		vap->va_fsid64.val[0] =
		    vfs_statfs(zfsvfs->z_vfs)->f_fsid.val[0];
		vap->va_fsid64.val[1] = vfs_typenum(zfsvfs->z_vfs);
		VATTR_SET_SUPPORTED(vap, va_fsid64);
	}
#endif
#endif

	zfs_exit(zfsvfs, FTAG);

	dprintf("%s: returned x%llx missed: x%llx\n", __func__,
		vap->va_supported, vap->va_active &= ~vap->va_supported);
	return (0);
}

int
zfsctl_vnop_access(struct vnop_access_args *ap)
{
	int accmode = ap->a_action;
	dprintf("zfsctl_access\n");

	if (accmode & VWRITE)
		return (EACCES);
	return (0);
}

int
zfsctl_vnop_open(struct vnop_open_args *ap)
{
	int flags = ap->a_mode;

	if (flags & FWRITE)
		return (EACCES);
	return (zfsctl_snapshot_mount(ap->a_vp, 0));
}

int
zfsctl_vnop_close(struct vnop_close_args *ap)
{
	dprintf("%s\n", __func__);
	return (0);
}

int
zfsctl_vnop_inactive(struct vnop_inactive_args *ap)
{
	dprintf("%s\n", __func__);
	return (0);
}

int
zfsctl_vnop_reclaim(struct vnop_reclaim_args *ap)
{
	struct vnode *vp = ap->a_vp;
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;

	dprintf("%s vp %p\n", __func__, vp);
	vnode_removefsref(vp); /* ADDREF from vnode_create */
	vnode_clearfsnode(vp); /* vp->v_data = NULL */

	mutex_enter(&zfsvfs->z_znodes_lock);
	if (list_link_active(&zp->z_link_node)) {
		list_remove(&zfsvfs->z_all_znodes, zp);
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	zp->z_vnode = NULL;
	kmem_cache_free(znode_cache, zp);

	return (0);
}

/*
 * Construct a full dataset name in full_name: "pool/dataset@snap_name"
 */
static int
zfsctl_snapshot_name(zfsvfs_t *zfsvfs, const char *snap_name, int len,
    char *full_name)
{
	objset_t *os = zfsvfs->z_os;

	if (zfs_component_namecheck(snap_name, NULL, NULL) != 0)
		return (SET_ERROR(EILSEQ));

	dmu_objset_name(os, full_name);
	if ((strlen(full_name) + 1 + strlen(snap_name)) >= len)
		return (SET_ERROR(ENAMETOOLONG));

	(void) strcat(full_name, "@");
	(void) strcat(full_name, snap_name);

	return (0);
}

int
zfsctl_snapshot_mount(struct vnode *vp, int flags)
{
	znode_t *zp = VTOZ(vp);
	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int ret = 0;
	int error = 0;
	/*
	 * If we are here for a snapdirs directory, attempt to get zed
	 * to mount the snapshot for the user. If successful, forward the
	 * vnop_open() to them (ourselves).
	 * Use a timeout in case zed is not running.
	 */

	if (zfs_auto_snapshot == 0)
		return (0);

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);
	if (((zp->z_id >= zfsvfs->z_ctldir_startid) &&
	    (zp->z_id <= ZFSCTL_INO_SNAPDIRS))) {
		hrtime_t now;
		now = gethrtime();

		/*
		 * If z_snap_mount_time is set, check if it is old enough to
		 * retry, if so, set z_snap_mount_time to zero.
		 */
		if (now - zp->z_snap_mount_time > SEC2NSEC(60))
			atomic_cas_64((uint64_t *)&zp->z_snap_mount_time,
			    (uint64_t)zp->z_snap_mount_time,
			    0ULL);

		/*
		 * Attempt mount, make sure only to issue one request, by
		 * attempting to CAS in current time in place of zero.
		 */
		if (atomic_cas_64((uint64_t *)&zp->z_snap_mount_time, 0ULL,
		    (uint64_t)now) == 0ULL) {
			char full_name[ZFS_MAX_DATASET_NAME_LEN];

			/* First! */
			ret = zfsctl_snapshot_name(zfsvfs, zp->z_name_cache,
			    ZFS_MAX_DATASET_NAME_LEN, full_name);

			if (ret == 0) {
				zfsctl_mounts_waiting_t *zcm;

				/* Create condvar to wait for mount to happen */

				zcm = kmem_alloc(
				    sizeof (zfsctl_mounts_waiting_t), KM_SLEEP);
				mutex_init(&zcm->zcm_lock, NULL, MUTEX_DEFAULT,
				    NULL);
				cv_init(&zcm->zcm_cv, NULL, CV_DEFAULT, NULL);
				strlcpy(zcm->zcm_name, full_name,
				    sizeof (zcm->zcm_name));

				dprintf("%s: requesting mount for '%s'\n",
				    __func__, full_name);

				mutex_enter(&zfsctl_mounts_lock);
				list_insert_tail(&zfsctl_mounts_list, zcm);
				mutex_exit(&zfsctl_mounts_lock);

				mutex_enter(&zcm->zcm_lock);
				zfs_ereport_snapshot_post(
				    FM_EREPORT_ZFS_SNAPSHOT_MOUNT,
				    dmu_objset_spa(zfsvfs->z_os), full_name);

				/* Now we wait hoping zed comes back to us */
				ret = cv_timedwait(&zcm->zcm_cv, &zcm->zcm_lock,
				    ddi_get_lbolt() + (hz * 3));

				dprintf("%s: finished waiting %d\n",
				    __func__, ret);

				mutex_exit(&zcm->zcm_lock);

				mutex_enter(&zfsctl_mounts_lock);
				list_remove(&zfsctl_mounts_list, zcm);
				mutex_exit(&zfsctl_mounts_lock);

				mutex_destroy(&zcm->zcm_lock);
				cv_destroy(&zcm->zcm_cv);

				kmem_free(zcm,
				    sizeof (zfsctl_mounts_waiting_t));

				/*
				 * If we mounted, make it re-open it so
				 * the process that issued the access will
				 * see the mounted content
				 */
				if (ret >= 0) {
					/* Remove the cache entry */
					cache_purge(vp);
					cache_purge_negatives(vp);
					ret = ERESTART;
				}
			}
		}
	}

	zfs_exit(zfsvfs, FTAG);

	return (ret);
}

/* Called whenever zfs_vfs_mount() is called with a snapshot */
void
zfsctl_mount_signal(zfsvfs_t *zfsvfs, char *osname, boolean_t mounting)
{
	zfsctl_mounts_waiting_t *zcm;

	dprintf("%s: looking for snapshot '%s'\n", __func__, osname);

	mutex_enter(&zfsctl_mounts_lock);
	for (zcm = list_head(&zfsctl_mounts_list);
	    zcm;
	    zcm = list_next(&zfsctl_mounts_list, zcm)) {
		if (strncmp(zcm->zcm_name, osname, sizeof (zcm->zcm_name)) == 0)
			break;
	}
	mutex_exit(&zfsctl_mounts_lock);

	/* Is there someone to wake up? */
	if (zcm != NULL) {
		mutex_enter(&zcm->zcm_lock);
		cv_signal(&zcm->zcm_cv);
		mutex_exit(&zcm->zcm_lock);
		dprintf("%s: mount waiter found and signalled\n", __func__);
	}

	zfsctl_unmount_delay_t *zcu;

	/* Add or remove mount to/from list of active mounts */

	if (mounting) {
		/* Add active mounts to the list */
		zcu = kmem_alloc(sizeof (zfsctl_unmount_delay_t), KM_SLEEP);
		zcu->se_name = kmem_strdup(osname);
		zcu->se_time = gethrestime_sec();
		list_link_init(&zcu->se_nodelink);

		mutex_enter(&zfsctl_unmount_list_lock);
		list_insert_tail(&zfsctl_unmount_list, zcu);
		mutex_exit(&zfsctl_unmount_list_lock);

	} else {
		/* Unmounting */
		mutex_enter(&zfsctl_unmount_list_lock);
		for (zcu = list_head(&zfsctl_unmount_list);
		    zcu != NULL;
		    zcu = list_next(&zfsctl_unmount_list, zcu)) {
			if (strcmp(osname, zcu->se_name) == 0)
				break;
		}
		mutex_exit(&zfsctl_unmount_list_lock);


		if (zcu != NULL) {
			list_remove(&zfsctl_unmount_list, zcu);
			kmem_strfree(zcu->se_name);
			kmem_free(zcu, sizeof (zfsctl_unmount_delay_t));
		}
	}
}

int
zfsctl_snapshot_unmount_node(struct vnode *vp, const char *full_name,
    int flags)
{
	znode_t *zp = VTOZ(vp);
	int error = 0;

	dprintf("%s\n", __func__);

	if (zp == NULL)
		return (ENOENT);
	return (0);

	zfsvfs_t *zfsvfs = zp->z_zfsvfs;
	int ret = ENOENT;
	/*
	 * If we are here for a snapdirs directory, attempt to get zed
	 * to mount the snapshot for the user. If successful, forward the
	 * vnop_open() to them (ourselves).
	 * Use a timeout in case zed is not running.
	 */

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zp->z_id == zfsvfs->z_root) {
		hrtime_t now;
		now = gethrtime();

		/*
		 * If z_snap_mount_time is set, check if it is old enough to
		 * retry, if so, set z_snap_mount_time to zero.
		 */
		if (now - zp->z_snap_mount_time > SEC2NSEC(60))
			atomic_cas_64((uint64_t *)&zp->z_snap_mount_time,
			    (uint64_t)zp->z_snap_mount_time,
			    0ULL);

		/*
		 * Attempt unmount, make sure only to issue one request, by
		 * attempting to CAS in current time in place of zero.
		 */
		if (atomic_cas_64((uint64_t *)&zp->z_snap_mount_time, 0ULL,
		    (uint64_t)now) == 0ULL) {

			ret = 0;

			/* First! */

			if (ret == 0) {
				zfsctl_mounts_waiting_t *zcm;

				/* Create condvar to wait for mount to happen */

				zcm = kmem_alloc(
				    sizeof (zfsctl_mounts_waiting_t), KM_SLEEP);
				mutex_init(&zcm->zcm_lock, NULL, MUTEX_DEFAULT,
				    NULL);
				cv_init(&zcm->zcm_cv, NULL, CV_DEFAULT, NULL);
				strlcpy(zcm->zcm_name, full_name,
				    sizeof (zcm->zcm_name));

				dprintf("%s: requesting unmount for '%s'\n",
				    __func__, full_name);

				mutex_enter(&zfsctl_mounts_lock);
				list_insert_tail(&zfsctl_mounts_list, zcm);
				mutex_exit(&zfsctl_mounts_lock);

				mutex_enter(&zcm->zcm_lock);
				zfs_ereport_snapshot_post(
				    FM_EREPORT_ZFS_SNAPSHOT_UNMOUNT,
				    dmu_objset_spa(zfsvfs->z_os), full_name);

				/* Now we wait hoping zed comes back to us */
				ret = cv_timedwait(&zcm->zcm_cv, &zcm->zcm_lock,
				    ddi_get_lbolt() + (hz * 3));

				dprintf("%s: finished waiting %d\n",
				    __func__, ret);

				mutex_exit(&zcm->zcm_lock);

				mutex_enter(&zfsctl_mounts_lock);
				list_remove(&zfsctl_mounts_list, zcm);
				mutex_exit(&zfsctl_mounts_lock);

				kmem_free(zcm,
				    sizeof (zfsctl_mounts_waiting_t));

				/* Allow mounts to happen immediately */
				zp->z_snap_mount_time = 0;

				/*
				 * If we unmounted, alert caller
				 */
				if (ret >= 0)
					ret = 0;

			}
		}
	}

	zfs_exit(zfsvfs, FTAG);

	return (ret);
}

int
zfsctl_snapshot_unmount(const char *snapname, int flags)
{
	znode_t *rootzp;
	zfsvfs_t *zfsvfs;

	dprintf("%s\n", __func__);

	if (strchr(snapname, '@') == NULL)
		return (0);

	int err = getzfsvfs(snapname, &zfsvfs);
	if (err != 0) {
		ASSERT3P(zfsvfs, ==, NULL);
		return (0);
	}
	ASSERT(!dsl_pool_config_held(dmu_objset_pool(zfsvfs->z_os)));

	err = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (err == 0) {
		zfsctl_snapshot_unmount_node(ZTOV(rootzp), snapname, flags);
		VN_RELE(ZTOV(rootzp));
	}

	vfs_unbusy(zfsvfs->z_vfs);
	return (0);
}

int
zfsctl_mkdir(znode_t *dzp, znode_t **zpp, char *dirname)
{
	int error;
	error = zfsctl_root_lookup(ZTOV(dzp), dirname, zpp, 0, NULL,
	    NULL, NULL);
	if (error == 0) {
		ZTOV(*zpp)->v_unlink = 0;
	}
	return (error);
}

int
zfsctl_set_reparse_point(znode_t *zp, REPARSE_DATA_BUFFER *rdb, size_t size)
{
	if (!zfsctl_is_leafnode(zp))
		return (STATUS_INVALID_PARAMETER);

	zp->z_pflags |= ZFS_REPARSE;
	vnode_set_reparse(ZTOV(zp), rdb, size);
	return (STATUS_SUCCESS);
}

int
zfsctl_delete_reparse_point(znode_t *zp)
{
	if (!zfsctl_is_leafnode(zp))
		return (STATUS_INVALID_PARAMETER);

	zp->z_pflags &= ~ZFS_REPARSE;
	vnode_set_reparse(ZTOV(zp), NULL, 0);

	return (STATUS_SUCCESS);
}

// make Tag vs Point call, to return just ULONG tag or copy over point
ULONG
zfsctl_get_reparse_tag(znode_t *zp)
{

	if (!zfsctl_is_leafnode(zp))
		return (0);

	if (!(zp->z_pflags & ZFS_REPARSE))
		return (0);

	return (vnode_get_reparse_tag(ZTOV(zp)));
}

int
zfsctl_get_reparse_point(znode_t *zp, REPARSE_DATA_BUFFER **buffer,
    size_t *size)
{

	if (!zfsctl_is_leafnode(zp))
		return (STATUS_NOT_A_REPARSE_POINT);

	if (!(zp->z_pflags & ZFS_REPARSE))
		return (STATUS_NOT_A_REPARSE_POINT);

	return (vnode_get_reparse_point(ZTOV(zp), buffer, size));
}

int
zfsctl_vnop_mkdir(struct vnop_mkdir_args *ap)
#if 0
	struct vnop_mkdir_args {
		struct vnode	*a_dvp;
		struct vnode	**a_vpp;
		struct componentname *a_cnp;
		struct vnode_vattr *a_vap;
		vfs_context_t	a_context;
	};
#endif
{
	cred_t *cr = NULL; // (cred_t *)vfs_context_ucred((ap)->a_context);
	znode_t *dzp = VTOZ(ap->a_dvp);
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	char *dsname;
	int error;

	if (zfs_admin_snapshot == 0)
		return (SET_ERROR(EACCES));

	dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfs_component_namecheck(ap->a_cnp->cn_nameptr, NULL, NULL) != 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	dmu_objset_name(zfsvfs->z_os, dsname);

	error = zfs_secpolicy_snapshot_perms(dsname, cr);
	if (error != 0)
		goto out;

	if (error == 0) {
		error = dmu_objset_snapshot_one(dsname, ap->a_cnp->cn_nameptr);
		if (error != 0)
			goto out;

		// error = zfsctl_root_lookup(ap->a_dvp, ap->a_cnp->cn_nameptr,
		//    ap->a_vpp, 0, cr, NULL, NULL);
	}

out:
	kmem_free(dsname, ZFS_MAX_DATASET_NAME_LEN);

	return (error);
}

int
zfsctl_vnop_rmdir(struct vnop_rmdir_args *ap)
#if 0
	struct vnop_rmdir_args {
		struct vnode	*a_dvp;
		struct vnode	*a_vp;
		struct componentname *a_cnp;
		vfs_context_t	a_context;
	};
#endif
{
	cred_t *cr = NULL; // (cred_t *)vfs_context_ucred((ap)->a_context);
	znode_t *dzp = VTOZ(ap->a_dvp);
	zfsvfs_t *zfsvfs = dzp->z_zfsvfs;
	char *snapname, *real;
	char *name = ap->a_cnp->cn_nameptr;
	int error;

	dprintf("%s: '%s'\n", __func__, name);

	if (zfs_admin_snapshot == 0)
		return (SET_ERROR(EACCES));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	real = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zfsvfs->z_os, name,
		    real, ZFS_MAX_DATASET_NAME_LEN, NULL);
		if (error == 0) {
			name = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	error = zfsctl_snapshot_name(zfsvfs, name,
	    ZFS_MAX_DATASET_NAME_LEN, snapname);
	if (error == 0)
		error = zfs_secpolicy_destroy_perms(snapname, cr);
	if (error != 0)
		goto out;

	error = zfsctl_snapshot_unmount_node(ap->a_vp, snapname, MNT_FORCE);
	if ((error == 0) || (error == ENOENT)) {
		error = dsl_destroy_snapshot(snapname, B_FALSE);

		/* Destroy the vnode */
		if (ap->a_vp != NULL) {
			dprintf("%s: releasing vp\n", __func__);
			vnode_recycle(ap->a_vp);
		}
	}

out:
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(real, ZFS_MAX_DATASET_NAME_LEN);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

static void
zfsctl_unmount_thread(void *notused)
{
	callb_cpr_t cpr;
	zfsctl_unmount_delay_t *zcu;
	time_t now;
	CALLB_CPR_INIT(&cpr, &zfsctl_unmount_lock, callb_generic_cpr, FTAG);

	dprintf("%s is alive\n", __func__);

	mutex_enter(&zfsctl_unmount_lock);
	while (!zfsctl_unmount_thread_exit) {

		CALLB_CPR_SAFE_BEGIN(&cpr);
		(void) cv_timedwait(&zfsctl_unmount_cv,
		    &zfsctl_unmount_lock, ddi_get_lbolt() + (hz<<6));
		CALLB_CPR_SAFE_END(&cpr, &zfsctl_unmount_lock);

		if (!zfsctl_unmount_thread_exit) {
			/*
			 * Loop all active mounts, if any are older
			 * than ZFSCTL_EXPIRE_SNAPSHOT, then we update
			 * their timestamp and attempt unmount.
			 */
			now = gethrestime_sec();
			mutex_enter(&zfsctl_unmount_list_lock);
			for (zcu = list_head(&zfsctl_unmount_list);
			    zcu != NULL;
			    zcu = list_next(&zfsctl_unmount_list, zcu)) {
				if ((now > zcu->se_time) &&
				    ((now - zcu->se_time) >
				    zfs_expire_snapshot)) {
					zcu->se_time = now;
					zfsctl_snapshot_unmount(zcu->se_name,
					    0);
				}
			}
			mutex_exit(&zfsctl_unmount_list_lock);
		}
	}

	zfsctl_unmount_thread_exit = FALSE;
	cv_broadcast(&zfsctl_unmount_cv);
	CALLB_CPR_EXIT(&cpr);
	dprintf("ZFS: zfsctl_unmount thread exit\n");
	thread_exit();
}

/*
 * Initialize the various pieces we'll need to create and manipulate .zfs
 * directories.  Currently this is unused but available.
 */
void
zfsctl_init(void)
{
	mutex_init(&zfsctl_mounts_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsctl_mounts_list, sizeof (zfsctl_mounts_waiting_t),
	    offsetof(zfsctl_mounts_waiting_t, zcm_node));

	mutex_init(&zfsctl_unmount_list_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsctl_unmount_list, sizeof (zfsctl_unmount_delay_t),
	    offsetof(zfsctl_unmount_delay_t, se_nodelink));

	mutex_init(&zfsctl_unmount_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&zfsctl_unmount_cv, NULL, CV_DEFAULT, NULL);
	zfsctl_unmount_thread_exit = FALSE;

	(void) thread_create(NULL, 0, zfsctl_unmount_thread, NULL, 0, &p0,
	    TS_RUN, minclsyspri);
}

/*
 * Cleanup the various pieces we needed for .zfs directories.  In particular
 * ensure the expiry timer is canceled safely.
 */
void
zfsctl_fini(void)
{
	mutex_destroy(&zfsctl_mounts_lock);
	list_destroy(&zfsctl_mounts_list);

	mutex_destroy(&zfsctl_unmount_list_lock);
	list_destroy(&zfsctl_unmount_list);

	mutex_enter(&zfsctl_unmount_lock);
	zfsctl_unmount_thread_exit = TRUE;
	while (zfsctl_unmount_thread_exit) {
		cv_signal(&zfsctl_unmount_cv);
		cv_wait(&zfsctl_unmount_cv, &zfsctl_unmount_lock);
	}
	mutex_exit(&zfsctl_unmount_lock);

	mutex_destroy(&zfsctl_unmount_lock);
	cv_destroy(&zfsctl_unmount_cv);
}

module_param(zfs_admin_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_admin_snapshot, "Enable mkdir/rmdir/mv in .zfs/snapshot");

module_param(zfs_expire_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_expire_snapshot, "Seconds to expire .zfs/snapshot");
