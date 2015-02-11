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
 * corresponding inode.
 *
 * All mounts are handled automatically by an user mode helper which invokes
 * the mount mount procedure.  Unmounts are handled by allowing the mount
 * point to expire so the kernel may automatically unmount it.
 *
 * The '.zfs', '.zfs/snapshot', and all directories created under
 * '.zfs/snapshot' (ie: '.zfs/snapshot/<snapname>') all share the same
 * share the same zfs_sb_t as the head filesystem (what '.zfs' lives under).
 *
 * File systems mounted on top of the '.zfs/snapshot/<snapname>' paths
 * (ie: snapshots) are complete ZFS filesystems and have their own unique
 * zfs_sb_t.  However, the fsid reported by these mounts will be the same
 * as that used by the parent zfs_sb_t to make NFS happy.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/pathname.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_vnops.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_deleg.h>
#include <sys/mount.h>
#include <sys/zpl.h>
#include "zfs_namecheck.h"

/*
 * Control Directory Tunables (.zfs)
 */
int zfs_expire_snapshot = ZFSCTL_EXPIRE_SNAPSHOT;

/*
 * Dedicated task queue for unmounting snapshots.
 */
static taskq_t *zfs_expire_taskq;

static zfs_snapentry_t *
zfsctl_sep_alloc(void)
{
	return (kmem_zalloc(sizeof (zfs_snapentry_t), KM_SLEEP));
}

void
zfsctl_sep_free(zfs_snapentry_t *sep)
{
	kmem_free(sep->se_name, MAXNAMELEN);
	kmem_free(sep->se_path, PATH_MAX);
	kmem_free(sep, sizeof (zfs_snapentry_t));
}

/*
 * Attempt to expire an automounted snapshot, unmounts are attempted every
 * 'zfs_expire_snapshot' seconds until they succeed.  The work request is
 * responsible for rescheduling itself and freeing the zfs_expire_snapshot_t.
 */
static void
zfsctl_expire_snapshot(void *data)
{
	zfs_snapentry_t *sep = (zfs_snapentry_t *)data;
	zfs_sb_t *zsb = ITOZSB(sep->se_inode);
	int error;

	error = zfsctl_unmount_snapshot(zsb, sep->se_name, MNT_EXPIRE);
	if (error == EBUSY)
		sep->se_taskqid = taskq_dispatch_delay(zfs_expire_taskq,
		    zfsctl_expire_snapshot, sep, TQ_SLEEP,
		    ddi_get_lbolt() + zfs_expire_snapshot * HZ);
}

int
snapentry_compare(const void *a, const void *b)
{
	const zfs_snapentry_t *sa = a;
	const zfs_snapentry_t *sb = b;
	int ret = strcmp(sa->se_name, sb->se_name);

	if (ret < 0)
		return (-1);
	else if (ret > 0)
		return (1);
	else
		return (0);
}

boolean_t
zfsctl_is_node(struct inode *ip)
{
	return (ITOZ(ip)->z_is_ctldir);
}

boolean_t
zfsctl_is_snapdir(struct inode *ip)
{
	return (zfsctl_is_node(ip) && (ip->i_ino <= ZFSCTL_INO_SNAPDIRS));
}

/*
 * Allocate a new inode with the passed id and ops.
 */
static struct inode *
zfsctl_inode_alloc(zfs_sb_t *zsb, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops)
{
	struct timespec now = current_fs_time(zsb->z_sb);
	struct inode *ip;
	znode_t *zp;

	ip = new_inode(zsb->z_sb);
	if (ip == NULL)
		return (NULL);

	zp = ITOZ(ip);
	ASSERT3P(zp->z_dirlocks, ==, NULL);
	ASSERT3P(zp->z_acl_cached, ==, NULL);
	ASSERT3P(zp->z_xattr_cached, ==, NULL);
	zp->z_id = id;
	zp->z_unlinked = 0;
	zp->z_atime_dirty = 0;
	zp->z_zn_prefetch = 0;
	zp->z_moved = 0;
	zp->z_sa_hdl = NULL;
	zp->z_blksz = 0;
	zp->z_seq = 0;
	zp->z_mapcnt = 0;
	zp->z_gen = 0;
	zp->z_size = 0;
	zp->z_atime[0] = 0;
	zp->z_atime[1] = 0;
	zp->z_links = 0;
	zp->z_pflags = 0;
	zp->z_uid = 0;
	zp->z_gid = 0;
	zp->z_mode = 0;
	zp->z_sync_cnt = 0;
	zp->z_is_zvol = B_FALSE;
	zp->z_is_mapped = B_FALSE;
	zp->z_is_ctldir = B_TRUE;
	zp->z_is_sa = B_FALSE;
	zp->z_is_stale = B_FALSE;
	ip->i_ino = id;
	ip->i_mode = (S_IFDIR | S_IRUGO | S_IXUGO);
	ip->i_uid = SUID_TO_KUID(0);
	ip->i_gid = SGID_TO_KGID(0);
	ip->i_blkbits = SPA_MINBLOCKSHIFT;
	ip->i_atime = now;
	ip->i_mtime = now;
	ip->i_ctime = now;
	ip->i_fop = fops;
	ip->i_op = ops;

	if (insert_inode_locked(ip)) {
		unlock_new_inode(ip);
		iput(ip);
		return (NULL);
	}

	mutex_enter(&zsb->z_znodes_lock);
	list_insert_tail(&zsb->z_all_znodes, zp);
	zsb->z_nr_znodes++;
	membar_producer();
	mutex_exit(&zsb->z_znodes_lock);

	unlock_new_inode(ip);

	return (ip);
}

/*
 * Lookup the inode with given id, it will be allocated if needed.
 */
static struct inode *
zfsctl_inode_lookup(zfs_sb_t *zsb, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops)
{
	struct inode *ip = NULL;

	while (ip == NULL) {
		ip = ilookup(zsb->z_sb, (unsigned long)id);
		if (ip)
			break;

		/* May fail due to concurrent zfsctl_inode_alloc() */
		ip = zfsctl_inode_alloc(zsb, id, fops, ops);
	}

	return (ip);
}

/*
 * Free zfsctl inode specific structures, currently there are none.
 */
void
zfsctl_inode_destroy(struct inode *ip)
{
}

/*
 * An inode is being evicted from the cache.
 */
void
zfsctl_inode_inactive(struct inode *ip)
{
	if (zfsctl_is_snapdir(ip))
		zfsctl_snapdir_inactive(ip);
}

/*
 * Create the '.zfs' directory.  This directory is cached as part of the VFS
 * structure.  This results in a hold on the zfs_sb_t.  The code in zfs_umount()
 * therefore checks against a vfs_count of 2 instead of 1.  This reference
 * is removed when the ctldir is destroyed in the unmount.  All other entities
 * under the '.zfs' directory are created dynamically as needed.
 *
 * Because the dynamically created '.zfs' directory entries assume the use
 * of 64-bit inode numbers this support must be disabled on 32-bit systems.
 */
int
zfsctl_create(zfs_sb_t *zsb)
{
#if defined(CONFIG_64BIT)
	ASSERT(zsb->z_ctldir == NULL);

	zsb->z_ctldir = zfsctl_inode_alloc(zsb, ZFSCTL_INO_ROOT,
	    &zpl_fops_root, &zpl_ops_root);
	if (zsb->z_ctldir == NULL)
		return (SET_ERROR(ENOENT));

	return (0);
#else
	return (SET_ERROR(EOPNOTSUPP));
#endif /* CONFIG_64BIT */
}

/*
 * Destroy the '.zfs' directory.  Only called when the filesystem is unmounted.
 */
void
zfsctl_destroy(zfs_sb_t *zsb)
{
	iput(zsb->z_ctldir);
	zsb->z_ctldir = NULL;
}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
struct inode *
zfsctl_root(znode_t *zp)
{
	ASSERT(zfs_has_ctldir(zp));
	igrab(ZTOZSB(zp)->z_ctldir);
	return (ZTOZSB(zp)->z_ctldir);
}

/*ARGSUSED*/
int
zfsctl_fid(struct inode *ip, fid_t *fidp)
{
	znode_t		*zp = ITOZ(ip);
	zfs_sb_t	*zsb = ITOZSB(ip);
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		i;

	ZFS_ENTER(zsb);

	if (fidp->fid_len < SHORT_FID_LEN) {
		fidp->fid_len = SHORT_FID_LEN;
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENOSPC));
	}

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = SHORT_FID_LEN;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* .zfs znodes always have a generation number of 0 */
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;

	ZFS_EXIT(zsb);
	return (0);
}

static int
zfsctl_snapshot_zname(struct inode *ip, const char *name, int len, char *zname)
{
	objset_t *os = ITOZSB(ip)->z_os;

	if (zfs_component_namecheck(name, NULL, NULL) != 0)
		return (SET_ERROR(EILSEQ));

	dmu_objset_name(os, zname);
	if ((strlen(zname) + 1 + strlen(name)) >= len)
		return (SET_ERROR(ENAMETOOLONG));

	(void) strcat(zname, "@");
	(void) strcat(zname, name);

	return (0);
}

/*
 * Gets the full dataset name that corresponds to the given snapshot name
 * Example:
 * 	zfsctl_snapshot_zname("snap1") -> "mypool/myfs@snap1"
 */
static int
zfsctl_snapshot_zpath(struct path *path, int len, char *zpath)
{
	char *path_buffer, *path_ptr;
	int path_len, error = 0;

	path_buffer = kmem_alloc(len, KM_SLEEP);

	path_ptr = d_path(path, path_buffer, len);
	if (IS_ERR(path_ptr)) {
		error = -PTR_ERR(path_ptr);
		goto out;
	}

	path_len = path_buffer + len - 1 - path_ptr;
	if (path_len > len) {
		error = SET_ERROR(EFAULT);
		goto out;
	}

	memcpy(zpath, path_ptr, path_len);
	zpath[path_len] = '\0';
out:
	kmem_free(path_buffer, len);

	return (error);
}

/*
 * Special case the handling of "..".
 */
/* ARGSUSED */
int
zfsctl_root_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	int error = 0;

	ZFS_ENTER(zsb);

	if (strcmp(name, "..") == 0) {
		*ipp = dip->i_sb->s_root->d_inode;
	} else if (strcmp(name, ZFS_SNAPDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zsb, ZFSCTL_INO_SNAPDIR,
		    &zpl_fops_snapdir, &zpl_ops_snapdir);
	} else if (strcmp(name, ZFS_SHAREDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zsb, ZFSCTL_INO_SHARES,
		    &zpl_fops_shares, &zpl_ops_shares);
	} else {
		*ipp = NULL;
	}

	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Lookup entry point for the 'snapshot' directory.  Try to open the
 * snapshot if it exist, creating the pseudo filesystem inode as necessary.
 * Perform a mount of the associated dataset on top of the inode.
 */
/* ARGSUSED */
int
zfsctl_snapdir_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	uint64_t id;
	int error;

	ZFS_ENTER(zsb);

	error = dmu_snapshot_lookup(zsb->z_os, name, &id);
	if (error) {
		ZFS_EXIT(zsb);
		return (error);
	}

	*ipp = zfsctl_inode_lookup(zsb, ZFSCTL_INO_SNAPDIRS - id,
	    &simple_dir_operations, &simple_dir_inode_operations);
	if (*ipp) {
#ifdef HAVE_AUTOMOUNT
		(*ipp)->i_flags |= S_AUTOMOUNT;
#endif /* HAVE_AUTOMOUNT */
	} else {
		error = SET_ERROR(ENOENT);
	}

	ZFS_EXIT(zsb);

	return (error);
}

static void
zfsctl_rename_snap(zfs_sb_t *zsb, zfs_snapentry_t *sep, const char *name)
{
	avl_index_t where;

	ASSERT(MUTEX_HELD(&zsb->z_ctldir_lock));
	ASSERT(sep != NULL);

	/*
	 * Change the name in the AVL tree.
	 */
	avl_remove(&zsb->z_ctldir_snaps, sep);
	(void) strcpy(sep->se_name, name);
	VERIFY(avl_find(&zsb->z_ctldir_snaps, sep, &where) == NULL);
	avl_insert(&zsb->z_ctldir_snaps, sep, where);
}

/*
 * Renaming a directory under '.zfs/snapshot' will automatically trigger
 * a rename of the snapshot to the new given name.  The rename is confined
 * to the '.zfs/snapshot' directory snapshots cannot be moved elsewhere.
 */
/*ARGSUSED*/
int
zfsctl_snapdir_rename(struct inode *sdip, char *snm,
    struct inode *tdip, char *tnm, cred_t *cr, int flags)
{
	zfs_sb_t *zsb = ITOZSB(sdip);
	zfs_snapentry_t search, *sep;
	avl_index_t where;
	char *to, *from, *real, *fsname;
	int error;

	ZFS_ENTER(zsb);

	to = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	from = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	real = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	fsname = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zsb->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zsb->z_os, snm, real,
		    MAXNAMELEN, NULL);
		if (error == 0) {
			snm = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	dmu_objset_name(zsb->z_os, fsname);

	error = zfsctl_snapshot_zname(sdip, snm, MAXNAMELEN, from);
	if (error == 0)
		error = zfsctl_snapshot_zname(tdip, tnm, MAXNAMELEN, to);
	if (error == 0)
		error = zfs_secpolicy_rename_perms(from, to, cr);
	if (error != 0)
		goto out;

	/*
	 * Cannot move snapshots out of the snapdir.
	 */
	if (sdip != tdip) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * No-op when names are identical.
	 */
	if (strcmp(snm, tnm) == 0) {
		error = 0;
		goto out;
	}

	mutex_enter(&zsb->z_ctldir_lock);

	error = dsl_dataset_rename_snapshot(fsname, snm, tnm, B_FALSE);
	if (error)
		goto out_unlock;

	search.se_name = (char *)snm;
	sep = avl_find(&zsb->z_ctldir_snaps, &search, &where);
	if (sep)
		zfsctl_rename_snap(zsb, sep, tnm);

out_unlock:
	mutex_exit(&zsb->z_ctldir_lock);
out:
	kmem_free(from, MAXNAMELEN);
	kmem_free(to, MAXNAMELEN);
	kmem_free(real, MAXNAMELEN);
	kmem_free(fsname, MAXNAMELEN);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Removing a directory under '.zfs/snapshot' will automatically trigger
 * the removal of the snapshot with the given name.
 */
/* ARGSUSED */
int
zfsctl_snapdir_remove(struct inode *dip, char *name, cred_t *cr, int flags)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	char *snapname, *real;
	int error;

	ZFS_ENTER(zsb);

	snapname = kmem_alloc(MAXNAMELEN, KM_SLEEP);
	real = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zsb->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zsb->z_os, name, real,
		    MAXNAMELEN, NULL);
		if (error == 0) {
			name = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	error = zfsctl_snapshot_zname(dip, name, MAXNAMELEN, snapname);
	if (error == 0)
		error = zfs_secpolicy_destroy_perms(snapname, cr);
	if (error != 0)
		goto out;

	error = zfsctl_unmount_snapshot(zsb, name, MNT_FORCE);
	if ((error == 0) || (error == ENOENT))
		error = dsl_destroy_snapshot(snapname, B_FALSE);
out:
	kmem_free(snapname, MAXNAMELEN);
	kmem_free(real, MAXNAMELEN);

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Creating a directory under '.zfs/snapshot' will automatically trigger
 * the creation of a new snapshot with the given name.
 */
/* ARGSUSED */
int
zfsctl_snapdir_mkdir(struct inode *dip, char *dirname, vattr_t *vap,
	struct inode **ipp, cred_t *cr, int flags)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	char *dsname;
	int error;

	dsname = kmem_alloc(MAXNAMELEN, KM_SLEEP);

	if (zfs_component_namecheck(dirname, NULL, NULL) != 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	dmu_objset_name(zsb->z_os, dsname);

	error = zfs_secpolicy_snapshot_perms(dsname, cr);
	if (error != 0)
		goto out;

	if (error == 0) {
		error = dmu_objset_snapshot_one(dsname, dirname);
		if (error != 0)
			goto out;

		error = zfsctl_snapdir_lookup(dip, dirname, ipp,
		    0, cr, NULL, NULL);
	}
out:
	kmem_free(dsname, MAXNAMELEN);

	return (error);
}

/*
 * When a .zfs/snapshot/<snapshot> inode is evicted they must be removed
 * from the snapshot list.  This will normally happen as part of the auto
 * unmount, however in the case of a manual snapshot unmount this will be
 * the only notification we receive.
 */
void
zfsctl_snapdir_inactive(struct inode *ip)
{
	zfs_sb_t *zsb = ITOZSB(ip);
	zfs_snapentry_t *sep, *next;

	mutex_enter(&zsb->z_ctldir_lock);

	sep = avl_first(&zsb->z_ctldir_snaps);
	while (sep != NULL) {
		next = AVL_NEXT(&zsb->z_ctldir_snaps, sep);

		if (sep->se_inode == ip) {
			avl_remove(&zsb->z_ctldir_snaps, sep);
			taskq_cancel_id(zfs_expire_taskq, sep->se_taskqid);
			zfsctl_sep_free(sep);
			break;
		}
		sep = next;
	}

	mutex_exit(&zsb->z_ctldir_lock);
}

/*
 * Attempt to unmount a snapshot by making a call to user space.
 * There is no assurance that this can or will succeed, is just a
 * best effort.  In the case where it does fail, perhaps because
 * it's in use, the unmount will fail harmlessly.
 */
#define	SET_UNMOUNT_CMD \
	"exec 0</dev/null " \
	"     1>/dev/null " \
	"     2>/dev/null; " \
	"umount -t zfs -n %s'%s'"

static int
__zfsctl_unmount_snapshot(zfs_snapentry_t *sep, int flags)
{
	char *argv[] = { "/bin/sh", "-c", NULL, NULL };
	char *envp[] = { NULL };
	int error;

	argv[2] = kmem_asprintf(SET_UNMOUNT_CMD,
	    flags & MNT_FORCE ? "-f " : "", sep->se_path);
	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	strfree(argv[2]);

	/*
	 * The umount system utility will return 256 on error.  We must
	 * assume this error is because the file system is busy so it is
	 * converted to the more sensible EBUSY.
	 */
	if (error)
		error = SET_ERROR(EBUSY);

	/*
	 * This was the result of a manual unmount, cancel the delayed work
	 * to prevent zfsctl_expire_snapshot() from attempting a unmount.
	 */
	if ((error == 0) && !(flags & MNT_EXPIRE))
		taskq_cancel_id(zfs_expire_taskq, sep->se_taskqid);


	return (error);
}

int
zfsctl_unmount_snapshot(zfs_sb_t *zsb, char *name, int flags)
{
	zfs_snapentry_t search;
	zfs_snapentry_t *sep;
	int error = 0;

	mutex_enter(&zsb->z_ctldir_lock);

	search.se_name = name;
	sep = avl_find(&zsb->z_ctldir_snaps, &search, NULL);
	if (sep) {
		avl_remove(&zsb->z_ctldir_snaps, sep);
		mutex_exit(&zsb->z_ctldir_lock);

		error = __zfsctl_unmount_snapshot(sep, flags);

		mutex_enter(&zsb->z_ctldir_lock);
		if (error == EBUSY)
			avl_add(&zsb->z_ctldir_snaps, sep);
		else
			zfsctl_sep_free(sep);
	} else {
		error = SET_ERROR(ENOENT);
	}

	mutex_exit(&zsb->z_ctldir_lock);
	ASSERT3S(error, >=, 0);

	return (error);
}

/*
 * Traverse all mounted snapshots and attempt to unmount them.  This
 * is best effort, on failure EEXIST is returned and count will be set
 * to the number of file snapshots which could not be unmounted.
 */
int
zfsctl_unmount_snapshots(zfs_sb_t *zsb, int flags, int *count)
{
	zfs_snapentry_t *sep, *next;
	int error = 0;

	*count = 0;

	ASSERT(zsb->z_ctldir != NULL);
	mutex_enter(&zsb->z_ctldir_lock);

	sep = avl_first(&zsb->z_ctldir_snaps);
	while (sep != NULL) {
		next = AVL_NEXT(&zsb->z_ctldir_snaps, sep);
		avl_remove(&zsb->z_ctldir_snaps, sep);
		mutex_exit(&zsb->z_ctldir_lock);

		error = __zfsctl_unmount_snapshot(sep, flags);

		mutex_enter(&zsb->z_ctldir_lock);
		if (error == EBUSY) {
			avl_add(&zsb->z_ctldir_snaps, sep);
			(*count)++;
		} else {
			zfsctl_sep_free(sep);
		}

		sep = next;
	}

	mutex_exit(&zsb->z_ctldir_lock);

	return ((*count > 0) ? EEXIST : 0);
}

#define	MOUNT_BUSY 0x80		/* Mount failed due to EBUSY (from mntent.h) */

#define	SET_MOUNT_CMD \
	"exec 0</dev/null " \
	"     1>/dev/null " \
	"     2>/dev/null; " \
	"mount -t zfs -n '%s' '%s'"

int
zfsctl_mount_snapshot(struct path *path, int flags)
{
	struct dentry *dentry = path->dentry;
	struct inode *ip = dentry->d_inode;
	zfs_sb_t *zsb = ITOZSB(ip);
	char *full_name, *full_path;
	zfs_snapentry_t *sep;
	zfs_snapentry_t search;
	char *argv[] = { "/bin/sh", "-c", NULL, NULL };
	char *envp[] = { NULL };
	int error;

	ZFS_ENTER(zsb);

	full_name = kmem_zalloc(MAXNAMELEN, KM_SLEEP);
	full_path = kmem_zalloc(PATH_MAX, KM_SLEEP);

	error = zfsctl_snapshot_zname(ip, dname(dentry), MAXNAMELEN, full_name);
	if (error)
		goto error;

	error = zfsctl_snapshot_zpath(path, PATH_MAX, full_path);
	if (error)
		goto error;

	/*
	 * Attempt to mount the snapshot from user space.  Normally this
	 * would be done using the vfs_kern_mount() function, however that
	 * function is marked GPL-only and cannot be used.  On error we
	 * careful to log the real error to the console and return EISDIR
	 * to safely abort the automount.  This should be very rare.
	 *
	 * If the user mode helper happens to return EBUSY, a concurrent
	 * mount is already in progress in which case the error is ignored.
	 * Take note that if the program was executed successfully the return
	 * value from call_usermodehelper() will be (exitcode << 8 + signal).
	 */
	argv[2] = kmem_asprintf(SET_MOUNT_CMD, full_name, full_path);
	error = call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
	strfree(argv[2]);
	if (error && !(error & MOUNT_BUSY << 8)) {
		printk("ZFS: Unable to automount %s at %s: %d\n",
		    full_name, full_path, error);
		error = SET_ERROR(EISDIR);
		goto error;
	}

	error = 0;
	mutex_enter(&zsb->z_ctldir_lock);

	/*
	 * Ensure a previous entry does not exist, if it does safely remove
	 * it any cancel the outstanding expiration.  This can occur when a
	 * snapshot is manually unmounted and then an automount is triggered.
	 */
	search.se_name = full_name;
	sep = avl_find(&zsb->z_ctldir_snaps, &search, NULL);
	if (sep) {
		avl_remove(&zsb->z_ctldir_snaps, sep);
		taskq_cancel_id(zfs_expire_taskq, sep->se_taskqid);
		zfsctl_sep_free(sep);
	}

	sep = zfsctl_sep_alloc();
	sep->se_name = full_name;
	sep->se_path = full_path;
	sep->se_inode = ip;
	avl_add(&zsb->z_ctldir_snaps, sep);

	sep->se_taskqid = taskq_dispatch_delay(zfs_expire_taskq,
	    zfsctl_expire_snapshot, sep, TQ_SLEEP,
	    ddi_get_lbolt() + zfs_expire_snapshot * HZ);

	mutex_exit(&zsb->z_ctldir_lock);
error:
	if (error) {
		kmem_free(full_name, MAXNAMELEN);
		kmem_free(full_path, PATH_MAX);
	}

	ZFS_EXIT(zsb);

	return (error);
}

/*
 * Check if this super block has a matching objset id.
 */
static int
zfsctl_test_super(struct super_block *sb, void *objsetidp)
{
	zfs_sb_t *zsb = sb->s_fs_info;
	uint64_t objsetid = *(uint64_t *)objsetidp;

	return (dmu_objset_id(zsb->z_os) == objsetid);
}

/*
 * Prevent a new super block from being allocated if an existing one
 * could not be located.  We only want to preform a lookup operation.
 */
static int
zfsctl_set_super(struct super_block *sb, void *objsetidp)
{
	return (-EEXIST);
}

int
zfsctl_lookup_objset(struct super_block *sb, uint64_t objsetid, zfs_sb_t **zsbp)
{
	zfs_sb_t *zsb = sb->s_fs_info;
	struct super_block *sbp;
	zfs_snapentry_t *sep;
	uint64_t id;
	int error;

	ASSERT(zsb->z_ctldir != NULL);

	mutex_enter(&zsb->z_ctldir_lock);

	/*
	 * Verify that the snapshot is mounted.
	 */
	sep = avl_first(&zsb->z_ctldir_snaps);
	while (sep != NULL) {
		error = dmu_snapshot_lookup(zsb->z_os, sep->se_name, &id);
		if (error)
			goto out;

		if (id == objsetid)
			break;

		sep = AVL_NEXT(&zsb->z_ctldir_snaps, sep);
	}

	if (sep != NULL) {
		/*
		 * Lookup the mounted root rather than the covered mount
		 * point.  This may fail if the snapshot has just been
		 * unmounted by an unrelated user space process.  This
		 * race cannot occur to an expired mount point because
		 * we hold the zsb->z_ctldir_lock to prevent the race.
		 */
		sbp = zpl_sget(&zpl_fs_type, zfsctl_test_super,
		    zfsctl_set_super, 0, &id);
		if (IS_ERR(sbp)) {
			error = -PTR_ERR(sbp);
		} else {
			*zsbp = sbp->s_fs_info;
			deactivate_super(sbp);
		}
	} else {
		error = SET_ERROR(EINVAL);
	}
out:
	mutex_exit(&zsb->z_ctldir_lock);
	ASSERT3S(error, >=, 0);

	return (error);
}

/* ARGSUSED */
int
zfsctl_shares_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfs_sb_t *zsb = ITOZSB(dip);
	struct inode *ip;
	znode_t *dzp;
	int error;

	ZFS_ENTER(zsb);

	if (zsb->z_shares_dir == 0) {
		ZFS_EXIT(zsb);
		return (SET_ERROR(ENOTSUP));
	}

	error = zfs_zget(zsb, zsb->z_shares_dir, &dzp);
	if (error) {
		ZFS_EXIT(zsb);
		return (error);
	}

	error = zfs_lookup(ZTOI(dzp), name, &ip, 0, cr, NULL, NULL);

	iput(ZTOI(dzp));
	ZFS_EXIT(zsb);

	return (error);
}


/*
 * Initialize the various pieces we'll need to create and manipulate .zfs
 * directories.  Currently this is unused but available.
 */
void
zfsctl_init(void)
{
	zfs_expire_taskq = taskq_create("z_unmount", 1, maxclsyspri,
	    1, 8, TASKQ_PREPOPULATE);
}

/*
 * Cleanup the various pieces we needed for .zfs directories.  In particular
 * ensure the expiry timer is canceled safely.
 */
void
zfsctl_fini(void)
{
	taskq_destroy(zfs_expire_taskq);
}

module_param(zfs_expire_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_expire_snapshot, "Seconds to expire .zfs/snapshot");
