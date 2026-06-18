// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2020 The MathWorks, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

/*
 * ZFS control directory (a.k.a. ".zfs")
 *
 * This directory provides a common location for all ZFS meta-objects.
 * Currently, this is only the 'snapshot' and 'shares' directory, but this may
 * expand in the future.  The elements are built dynamically, as the hierarchy
 * does not actually exist on disk.
 */

/*
 * # Snapdir overview
 *
 * The bulk of this file is the "snapdir" system, that manages automatically
 * mounting snapshots when accessed through the .zfs/snapshot/<snapname>
 * virtual directories.
 *
 * This on-demand system exists so we don't have all snapshots mounted at all
 * times, which both uses memory and makes the mount table (read: `df` output)
 * enormous.
 *
 * Instead, we create some virtual inodes and directory entries in the root of
 * each dataset (subject to the `snapdir` property):
 *
 * .zfs/
 *   snapshot/
 *     snapshot_1/
 *     snapshot_2/
 *     snapshot_3/
 *     ...
 *
 * The dentries for the named snapshot nodes have a custom set of operations
 * attached, most importantly d_automount = zpl_snapdir_automount(). When the
 * kernel attempts a path walk through one of these nodes, the automount
 * function is called, which in turn calls into zfsctl_snapshot_mount(). That
 * function creates the mount and returns it, and the VFS splices it into the
 * global filesystem tree, then retries the path walk, enters the new mount and
 * continues as normal.
 *
 * When setting up the mount, we also set up the "expiry timer" task. After
 * `zfs_expire_snapshot` seconds (default: 300), the task checks the if the
 * mount has been idle for the entire interval. If it has, it is unmounted; if
 * not, the timer is reset and will try again later. This is done to keep
 * memory usage and the mount table tidy, by only keeping snapshot mounts
 * around for the time they're in use.
 *
 * This overview is good enough for basic understanding of the system, but the
 * details are rather more complex.
 *
 * ## Source location
 *
 * This description covers functions in three separate files:
 *
 * - zpl.h: zfs_snapentry_t, related enums and macros
 * - zpl_ctldir.c: inode and dentry operations and utility functions
 * - zfs_ctldir.c: mount creation, expiry task, unmount coordination
 *
 * This is fairly standard for the Linux ZPL, however the coupling between
 * the two "sides" is rather tighter than other subsystems, since almost all
 * of this is about manipulating the snapdir dentry in the right way.
 *
 * ## State: zfs_snapentry_t
 *
 * We manage the current state for each snapdir in a zfs_snapentry_t. This
 * struct is allocated in zpl_snapdir_init_snapentry() when the dentry is first
 * initialised, and destroyed in zpl_snapdir_release() when the kernel destroys
 * the dentry. The two refer to each other; the snapentry is in
 * dentry->d_fsdata, while the dentry is in se->se_dentry.
 *
 * The dentry and the snapentry have the same lifetime, and are entirely
 * managed by the kernel from the dentry side. As such, there is no separate
 * hold for the snapentry; to pin it when we need it, we use the a normal
 * dget/dput pair.
 *
 * # Mounting: d_manage and d_automount
 *
 * The dentry_operations has two functions that are wired in to the kernel's
 * mount traversal loop (__traverse_mounts()) for the automount system.
 *
 * Our d_automount is zpl_snapdir_automount(). On its own, it is almost
 * entirely what you'd expect - it calls zfsctl_snapshot_mount(), and on
 * success, passes the vfsmount back to the VFS.
 *
 * Our d_manage, zpl_snapdir_manage(), is rather more complicated. d_manage is
 * also known as MANAGE_TRANSIT. It's a place where the filesystem can hold
 * (block) callers while d_automount is in progress, since d_automount's
 * purpose is to actually prepare and return the mount. d_manage has two
 * different ways it can be called ("RCU-walk" and "REF-walk"), and a bunch of
 * different returns to signal different things back to the kernel. Ultimately
 * though we're checking if a mount or unmount is in progress by waiting for
 * the SE_BUSY flag to clear, or we're return success to allow the thread to
 * proceed into either the mount or d_automount, or we're returning some error
 * code to request a different behaviour. This is exactly what this callback is
 * for, so there's not much more to say here that isn't covered in the kernel
 * docs and the comments.
 *
 * There is however one special feature we have that needs a bit more work to
 * enable and so a bit more explanation. The `zfs_snapshot_no_setuid` tunable
 * when enabled causes automounted snapshots to receive the `nosuid` mount
 * option, preventing setuid executables on the snapshot to be run.
 *
 * The VFS unfortunately overwrites the options on the vfsmount returned by
 * d_automount with those of the parent mount, without exception. So setting
 * MNT_NOSUID on the mount has no effect, nor do superblock options like
 * SB_NOSUID that would be transferred to the mount in a conventional mount.
 *
 * To work around this, when the first mount request arrives in
 * zpl_snapdir_manage(), we note its task pointer in se_mount_task, then
 * initiate a new path walk directly into the snapdir dentry via
 * zpl_follow_down(). This arrives back in zpl_snapdir_manage(), where we
 * recognise it as the se_mount_task and immediately let it proceed into
 * zpl_snapdir_automount(). The mount happens and we return it and the VFS
 * grafts it into the tree, overwriting the mount flags. zpl_follow_down()
 * returns into zpl_snapdir_manage() with a reference to the vfsmount that
 * was grafted. The mount is live, but not yet accessed because all threads
 * are blocked in zpl_snapdir_manage(), waiting on SE_BUSY before they can be
 * released into the mount. We have unfettered access to the vfsmount _after_
 * the VFS has trampled it, and we call zfsctl_snapshot_finish_mount() to
 * apply MNT_NOSUID if necessary.
 *
 * This workaround causes another problem, which we also have to work around.
 * Normally a path walk comes with an "intent" via a set of LOOKUP_ flags
 * describing what the path walk is for. Normally, the automount will only be
 * triggered for functions that need to properly "enter" the mount. Since
 * it's not the original calling thread that is triggering the automount,
 * these flags are not honoured, resulting in even a simple stat() call on
 * the unmounted snapdir to trigger the mount. To work around this, we check
 * the lookup intent flags in zpl_snapdir_lookup() and zpl_snapdir_revalidate()
 * and set the SE_WANT_MOUNT flag if anything wants the mount, and then decide
 * whether or not to trigger it based on that flag.
 *
 * This explainer is longer than the code. I feel ok about that.
 *
 * ## Unmounting: invalidating the mountpoint
 *
 * Linux mounts are somewhat ephemeral. Technically, they're a separate
 * object that binds a "lower" dentry (the "mountpoint") to an "upper" dentry
 * (the "mount root", typically the root of a different filesystem). From
 * there, they act as a "transit" point, controlling traversal from one
 * filesystem to another, possibly applying changes to the operation along
 * the way (eg changing namespaces). Ordinarily, the mountpoint dentry holds
 * a reference to the mount, and the mount holds a reference to the root
 * dentry. When files are opened, they also take references to the mount, which
 * are released when the file is closed.
 *
 * It's these refcounts that keep the entire mount alive. The traditional
 * umount(2) checks the refcount on the mount, and if it is 1 (ie just the
 * mountpoint), it can be detached from the mountpoint, which lowers its
 * refcount to 0, which release the root dentry, triggering a cascade of
 * reference drops which tears down the entire filesystem structure. If the
 * mount refcount is >1, then the filesystem is still in use, and umount
 * fails with EBUSY.
 *
 * These refcounts are also what allow the myriad mount options. A "lazy"
 * umount (MS_DETACH) omits the refcount check, it just detaches the mount
 * from the mountpoint dentry. If there are no other references (ie its not
 * in use), the cascade happens and we get a full unmount, otherwise it will
 * remain alive until all references are released (eg files closed). This is
 * the same mechanism allows "anonymous" mounts.
 *
 * Bind (MS_BIND) mounts follow from this: they create a new mount, with an
 * existing dentry as the "root" (not even necessarily a filesytem root!).
 * MS_MOVE meanwhile is just taking an existing mount and atomically detaching
 * it from its mountpoint and attaching it to another.
 *
 * These are all fundamental features of the Linux VFS, and put us in an
 * interesting position. While we can create a mount and attach it to a known
 * dentry that we can control, we have no say in what happens after that.
 * The mount we created might be unmounted by someone else, moved away, or
 * a bind mount created. There could be a totally unrelated mount on the
 * snapdir dentry, even for a non-ZFS filesystem. Or a whole stack of mounts.
 * And, we will not know anything about them, and possibly have no way to
 * control them.
 *
 * So, we instead focus on what we can control: the snapdir dentry (mountpoint)
 * itself. Regardless of what might be "on top", we can always invalidate
 * the dentry, reducing the refcount of any mount that might be attached to it.
 * If there are no other references, then we get the reference drop cascade
 * and effectively get an "unmount". If there are, then we have done the
 * equivalent of a MS_DETACH unmount; the mount lives on "somewhere" until
 * its users are finished, but the ctldir is clear.
 *
 * In all cases we care about, this is acceptable. If the mount is the one we
 * mounted, then if its still in use, the next thing (eg dataset destruction)
 * will fail with EBUSY, but that is correct anyway; it's not the snapdir's
 * job to throw off users or things like that. If the operator has unmounted
 * or moved the snapshot mount away, invalidating the dentry will do nothing,
 * but that's fine too - the operator has done something strange, it's on them
 * to sort it out. The same is true of mounting something weird on the snapdir;
 * we don't know what's happening, but the operator has done something very
 * odd and it's not up to us to second guess that.
 *
 * ## Multiple mounts
 *
 * The same dataset can be mounted in several places at once, sharing one
 * superblock and so one control dentry per snapshot. A mount is keyed on
 * (parent vfsmount, mountpoint dentry), so each place the dataset is mounted
 * gets its own snapshot mount grafted onto that single shared control dentry.
 * This is why zpl_snapdir_manage() tests for an existing mount with
 * follow_down_one() (this parent) and not d_mountpoint() (true if _any_ parent
 * has one) - getting that wrong loops the walk into automount retries (ELOOP).
 * Conversely d_invalidate() tears down _every_ mount on the dentry regardless
 * of parent, so one invalidate cleans up all of them at once.
 *
 * ## Mount expiry
 *
 * In zfsctl_snapshot_finish_mount(), we call zfsctl_snapshot_timer_set() to
 * queue a delay task. When it fires, zfsctl_snapshot_timer_task() is called,
 * which simply calls zfsctl_snapshot_invalidate(). If it's busy, the
 * timer is re-armed and we try again next time.
 *
 * "Busy-ness" is determined by two timestmaps that are updated to the jiffy
 * clock value when certain events occur:
 *
 * - se_atime is updated in zpl_snapdir_revalidate() and in
 *   zfsctl_snapdir_vget(), which are both places where the snapdir is crossed,
 *   ie something did a lookup inside the mounted snapshot.
 *
 * - z_snap_atime is updated in zfs_exit()->zfs_exit_fs() when a data access
 *   inside the snapshot completes.
 *
 * The snapshot is considered "busy" if the most recent of these timestamps
 * is more recent than the expiry timout (zfs_expire_snapshot, 300s by
 * default). Tracking both is necessary as lookups do not imply data access
 * and vice-versa, especially for NFS which maintains direct object
 * references and may never actually do a lookup.
 *
 * As above, "expiry" means invalidating the dentry, which simply remove the
 * mount from view; if its still in use "on the inside" it will continue to
 * work, and a new mount will be created on next lookup.
 *
 * ## Unmount by name
 *
 * Unmounting is a side-effect for many ZFS ioctls eg `zfs destroy`,
 * `zfs rollback`, etc. zfsctl_snapshot_unmount() needs to find a mounted
 * snapshot entirely by name. It does this by finding the zfsvfs for the
 * containing dataset, then walking down through the control dir to find
 * the snapdir dentry, retrieve the zfs_snapentry_t from it, and attempt
 * an unmount. This is involved; see that function for details.
 *
 * ## NFS flush
 *
 * The in-kernel NFS server can pin dentries, blocking an unmount. We don't
 * care about this in the expiry case, since the NFS cache will drop unused
 * entries after a while.
 *
 * However, if we are trying to unmount a snapshot as part of some admin
 * operation, we don't want the NFS cache being the only thing holding the
 * snapshot alive, preventing the operation. We try to detect this possibilty
 * in zfsctl_snapshot_unmount() by seeing if the snapshot is still alive
 * somewhere, and in those cases call zfsctl_snapshot_unmount_nfs_flush()
 * to flush the cache in the hopes it will release the mount. See those two
 * functions for more info.
 *
 * ## Note for future spelunkers
 *
 * Much of the complexity here is due to ZFS wanting a lot more control over
 * mounts (both snapshot and the more conventional kind) than the kernel wants
 * or expects, while some of it is working around "missing" functionality that
 * the kernel doesn't provide or expose (eg direct access to mount objects).
 *
 * There are two "obvious" shortcuts that appear to make the code a lot
 * simpler but you should avoid, because they both induce use-after-frees:
 *
 * - Keeping a pointer to the snapshot's or the parent's vfsmount. You cannot
 *   mntget() either of these, as the kernel will consider a mount "busy" and
 *   not even consider releasing it if its refcount > 1 (ie the mountpoint
 *   only). Holding a snapshot ref would cause an operator unmount to EBUSY;
 *   Holding a parent ref would block the entire parent dataset's teardown.
 *   We only ever touch the mount transiently via follow_down_one(), and never
 *   store one.
 *
 * - Holding a backpointer from the snapshot's zfsvfs to the snapentry (to
 *   bump se_atime on data access). The snapshot superblock is shared across
 *   every mount of that snapshot, including container binds and manual mounts.
 *   Since it outlives the control dentry and its snapentry, its pointer would
 *   dangle. This is why se_atime is only ever bumped from the control side.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/time.h>
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
#include <sys/zfs_ioctl_impl.h>
#include <linux/fs_context.h>
#include <linux/workqueue_compat.h>
#include "zfs_namecheck.h"

/*
 * Control Directory Tunables (.zfs)
 */
int zfs_expire_snapshot = ZFSCTL_EXPIRE_SNAPSHOT;
static int zfs_admin_snapshot = 0;
static int zfs_snapshot_no_setuid = 0;

static void zfsctl_snapshot_timer_set(zfs_snapentry_t *se, unsigned long delay);
static int zfsctl_snapshot_invalidate(zfs_snapentry_t *se,
    unsigned long *delay);

/*
 * Delayed task responsible for unmounting an expired automounted snapshot.
 */
static void
zfsctl_snapshot_timer_task(void *data)
{
	zfs_snapentry_t *se = (zfs_snapentry_t *)data;

	/*
	 * We need to protect against a tricky race here.
	 *
	 * The dentry manages the lifetime for the snapentry, and this async
	 * timer task has a pointer to the snapentry. If the dentry was to
	 * be destroyed while the timer is still queued or active, the
	 * snapentry would be destroyed out from under it.
	 *
	 * The obvious solution is to take an additional dentry reference when
	 * the timer is armed, and release it when it is canceled, but then
	 * the timer has effectively "pinned" the dentry - it can't be released
	 * until the timer runs to completion.
	 *
	 * So, in zpl_snapdir_release(), we move to cancel the timer in
	 * blocking mode, before the dentry is deallocated. This keeps the
	 * dentry (and so the snapentry) allocated.
	 *
	 * However, there is a gap. This timer task may run _after_ the last
	 * call to dput() (from anywhere) but before zpl_snapdir_release()
	 * calls in to cancel the timer. If that happens, we then end up in
	 * zfsctl_snapshot_invalidate() performing all manner of dentry
	 * tests on a dentry with zero references. We can't take a new
	 * reference at that point, the dentry is already "dead" from the
	 * perspective of any callers, and taking a reference on a dentry with
	 * zero references is invalid.
	 *
	 * To get around this, we make use of the fact that we can test if
	 * a dentry is hashed (visible & active) holding only the dentry
	 * lock, and if necessary we can take a dentry reference without
	 * dropping the lock. The dentry is unhashed by both d_invalidate()
	 * (our "unmount" stand-in) and if necessary before d_release() is
	 * called.
	 *
	 * However, while unhashed implies refcount 0, it's not true that
	 * refcount 0 implies unhashed. A still-hashed entry can have refcount
	 * 0 while also being "alive", waiting on the dcache LRU to be freed
	 * or reused. For this case, we also check if the dentry is a
	 * mountpoint. If it is, then its refcount can't be 0, and if it isn't,
	 * we don't care because we're only here to unmount things.
	 *
	 * (simply checking if its a mountpoint is not enough in the other
	 * direction; the dentry can be unhashed but still a mountpoint if
	 * we invalidated it but it is still in use, for example in
	 * zfsctl_snapdir_rename()).
	 */
	spin_lock(&se->se_dentry->d_lock);
	if (d_unhashed(se->se_dentry) || !d_mountpoint(se->se_dentry)) {
		/* No longer visible, so just "finish" the task and eject. */
		mutex_enter(&se->se_mtx);
		se->se_taskqid = TASKQID_INVALID;
		mutex_exit(&se->se_mtx);
		spin_unlock(&se->se_dentry->d_lock);
		return;
	}

	/* Take dentry hold while we do the unmount work. */
	dget_dlock(se->se_dentry);
	spin_unlock(&se->se_dentry->d_lock);

	int err = 0;
	unsigned long delay = 0;

	if (zfs_expire_snapshot <= 0)
		/* Expiry was disabled by the admin, do nothing. */
		goto out;

	err = zfsctl_snapshot_invalidate(se, &delay);

out:
	mutex_enter(&se->se_mtx);
	se->se_taskqid = TASKQID_INVALID;
	mutex_exit(&se->se_mtx);

	if (err != 0) {
		ASSERT3U(err, ==, EAGAIN);
		/*
		 * Snapdir was used within the expiry time, re-arm it for the
		 * next possible time it could expire.
		 */
		zfsctl_snapshot_timer_set(se, delay);
	}

	dput(se->se_dentry);
}

/* Safely cancel the snapentry expiry timer. */
void
zfsctl_snapshot_timer_clear(zfs_snapentry_t *se)
{
	taskqid_t tqid;

	mutex_enter(&se->se_mtx);
	tqid = se->se_taskqid;
	mutex_exit(&se->se_mtx);

	if (tqid == TASKQID_INVALID)
		return;

	if (taskq_cancel_id(system_delay_taskq, tqid, B_TRUE) != 0) {
		/*
		 * Cancellation failed, so either the task already cleared
		 * it (and we won a race on se_mtx above), or the task is
		 * running right now and will clear it when done.
		 */
		return;
	}

	mutex_enter(&se->se_mtx);
	if (se->se_taskqid == tqid)
		se->se_taskqid = TASKQID_INVALID;
	mutex_exit(&se->se_mtx);
}

/*
 * Arm the snapentry expire timer to fire in `delay` ticks. If already armed,
 * do nothing.
 */
static void
zfsctl_snapshot_timer_set(zfs_snapentry_t *se, unsigned long delay)
{
	/* Do nothing if the expire timer has been disabled. */
	if (delay == 0)
		return;

	mutex_enter(&se->se_mtx);
	if (se->se_taskqid != TASKQID_INVALID) {
		/* Already armed, do nothing. */
		mutex_exit(&se->se_mtx);
		return;
	}

	se->se_taskqid = taskq_dispatch_delay(system_delay_taskq,
	    zfsctl_snapshot_timer_task, se, TQ_SLEEP, ddi_get_lbolt() + delay);
	mutex_exit(&se->se_mtx);
}

/*
 * Check if the given inode is a part of the virtual .zfs directory.
 */
boolean_t
zfsctl_is_node(struct inode *ip)
{
	return (ITOZ(ip)->z_is_ctldir);
}

/*
 * Check if the given inode is a .zfs/snapshots/snapname directory.
 */
boolean_t
zfsctl_is_snapdir(struct inode *ip)
{
	return (zfsctl_is_node(ip) && (ip->i_ino <= ZFSCTL_INO_SNAPDIRS));
}

/*
 * Allocate a new inode with the passed id and ops.
 */
static struct inode *
zfsctl_inode_alloc(zfsvfs_t *zfsvfs, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops,
    uint64_t creation)
{
	struct inode *ip;
	znode_t *zp;
	inode_timespec_t now = {.tv_sec = creation};

	ip = new_inode(zfsvfs->z_sb);
	if (ip == NULL)
		return (NULL);

	if (!creation)
		now = current_time(ip);
	zp = ITOZ(ip);
	ASSERT0P(zp->z_dirlocks);
	ASSERT0P(zp->z_acl_cached);
	ASSERT0P(zp->z_xattr_cached);
	zp->z_id = id;
	zp->z_unlinked = B_FALSE;
	zp->z_atime_dirty = B_FALSE;
	zp->z_zn_prefetch = B_FALSE;
	zp->z_is_sa = B_FALSE;
	zp->z_is_ctldir = B_TRUE;
	zp->z_xattr_dir_absent = B_FALSE;
	zp->z_sa_hdl = NULL;
	zp->z_blksz = 0;
	zp->z_seq = 0;
	zp->z_mapcnt = 0;
	zp->z_size = 0;
	zp->z_pflags = 0;
	zp->z_mode = 0;
	zp->z_sync_cnt = 0;
	ip->i_generation = 0;
	ip->i_ino = id;
	ip->i_mode = (S_IFDIR | S_IRWXUGO);
	ip->i_uid = SUID_TO_KUID(0);
	ip->i_gid = SGID_TO_KGID(0);
	ip->i_blkbits = SPA_MINBLOCKSHIFT;
	zpl_inode_set_atime_to_ts(ip, now);
	zpl_inode_set_mtime_to_ts(ip, now);
	zpl_inode_set_ctime_to_ts(ip, now);
	ip->i_fop = fops;
	ip->i_op = ops;
#if defined(IOP_XATTR)
	ip->i_opflags &= ~IOP_XATTR;
#endif

	if (insert_inode_locked(ip)) {
		unlock_new_inode(ip);
		iput(ip);
		return (NULL);
	}

	mutex_enter(&zfsvfs->z_znodes_lock);
	list_insert_tail(&zfsvfs->z_all_znodes, zp);
	membar_producer();
	mutex_exit(&zfsvfs->z_znodes_lock);

	unlock_new_inode(ip);

	return (ip);
}

/*
 * Lookup the inode with given id, it will be allocated if needed.
 */
static struct inode *
zfsctl_inode_lookup(zfsvfs_t *zfsvfs, uint64_t id,
    const struct file_operations *fops, const struct inode_operations *ops)
{
	struct inode *ip = NULL;
	uint64_t creation = 0;
	dsl_dataset_t *snap_ds;
	dsl_pool_t *pool;

	while (ip == NULL) {
		ip = ilookup(zfsvfs->z_sb, (unsigned long)id);
		if (ip)
			break;

		if (id <= ZFSCTL_INO_SNAPDIRS && !creation) {
			pool = dmu_objset_pool(zfsvfs->z_os);
			dsl_pool_config_enter(pool, FTAG);
			if (!dsl_dataset_hold_obj(pool,
			    ZFSCTL_INO_SNAPDIRS - id, FTAG, &snap_ds)) {
				creation = dsl_get_creation(snap_ds);
				dsl_dataset_rele(snap_ds, FTAG);
			}
			dsl_pool_config_exit(pool, FTAG);
		}

		/* May fail due to concurrent zfsctl_inode_alloc() */
		ip = zfsctl_inode_alloc(zfsvfs, id, fops, ops, creation);
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
 * of 64-bit inode numbers this support must be disabled on 32-bit systems.
 */
int
zfsctl_create(zfsvfs_t *zfsvfs)
{
	ASSERT0P(zfsvfs->z_ctldir);

	zfsvfs->z_ctldir = zfsctl_inode_alloc(zfsvfs, ZFSCTL_INO_ROOT,
	    &zpl_fops_root, &zpl_ops_root, 0);
	if (zfsvfs->z_ctldir == NULL)
		return (SET_ERROR(ENOENT));

	return (0);
}

/*
 * Destroy the '.zfs' directory or remove a snapshot from zfs_snapshots_by_name.
 * Only called when the filesystem is unmounted.
 */
void
zfsctl_destroy(zfsvfs_t *zfsvfs)
{
	if (zfsvfs->z_ctldir) {
		iput(zfsvfs->z_ctldir);
		zfsvfs->z_ctldir = NULL;
	}
}

/*
 * Given a root znode, retrieve the associated .zfs directory.
 * Add a hold to the vnode and return it.
 */
struct inode *
zfsctl_root(znode_t *zp)
{
	ASSERT(zfs_has_ctldir(zp));
	/* Must have an existing ref, so igrab() cannot return NULL */
	VERIFY3P(igrab(ZTOZSB(zp)->z_ctldir), !=, NULL);
	return (ZTOZSB(zp)->z_ctldir);
}

/*
 * Generate a long fid to indicate a snapdir. We encode whether snapdir is
 * already mounted in gen field. We do this because nfsd lookup will not
 * trigger automount. Next time the nfsd does fh_to_dentry, we will notice
 * this and do automount and return ESTALE to force nfsd revalidate and follow
 * mount.
 */
static int
zfsctl_snapdir_fid(struct inode *ip, fid_t *fidp)
{
	zfid_short_t *zfid = (zfid_short_t *)fidp;
	zfid_long_t *zlfid = (zfid_long_t *)fidp;
	uint32_t gen = 0;
	uint64_t object;
	uint64_t objsetid;
	int i;
	struct dentry *dentry;

	if (fidp->fid_len < LONG_FID_LEN) {
		fidp->fid_len = LONG_FID_LEN;
		return (SET_ERROR(ENOSPC));
	}

	object = ip->i_ino;
	objsetid = ZFSCTL_INO_SNAPDIRS - ip->i_ino;
	zfid->zf_len = LONG_FID_LEN;

	dentry = d_obtain_alias(igrab(ip));
	if (!IS_ERR(dentry)) {
		gen = !!d_mountpoint(dentry);
		dput(dentry);
	}

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(gen >> (8 * i));

	for (i = 0; i < sizeof (zlfid->zf_setid); i++)
		zlfid->zf_setid[i] = (uint8_t)(objsetid >> (8 * i));

	for (i = 0; i < sizeof (zlfid->zf_setgen); i++)
		zlfid->zf_setgen[i] = 0;

	return (0);
}

/*
 * Generate an appropriate fid for an entry in the .zfs directory.
 */
int
zfsctl_fid(struct inode *ip, fid_t *fidp)
{
	znode_t		*zp = ITOZ(ip);
	zfsvfs_t	*zfsvfs = ITOZSB(ip);
	uint64_t	object = zp->z_id;
	zfid_short_t	*zfid;
	int		i;
	int		error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsctl_is_snapdir(ip)) {
		zfs_exit(zfsvfs, FTAG);
		return (zfsctl_snapdir_fid(ip, fidp));
	}

	if (fidp->fid_len < SHORT_FID_LEN) {
		fidp->fid_len = SHORT_FID_LEN;
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOSPC));
	}

	zfid = (zfid_short_t *)fidp;

	zfid->zf_len = SHORT_FID_LEN;

	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(object >> (8 * i));

	/* .zfs znodes always have a generation number of 0 */
	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = 0;

	zfs_exit(zfsvfs, FTAG);
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

/*
 * Returns full path in full_path: "/pool/dataset/.zfs/snapshot/snap_name/"
 */
static int
zfsctl_snapshot_path_objset(zfsvfs_t *zfsvfs, uint64_t objsetid,
    int path_len, char *full_path)
{
	objset_t *os = zfsvfs->z_os;
	fstrans_cookie_t cookie;
	char *snapname;
	boolean_t case_conflict;
	uint64_t id, pos = 0;
	int error = 0;

	cookie = spl_fstrans_mark();
	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	while (error == 0) {
		dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
		error = dmu_snapshot_list_next(zfsvfs->z_os,
		    ZFS_MAX_DATASET_NAME_LEN, snapname, &id, &pos,
		    &case_conflict);
		dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
		if (error)
			goto out;

		if (id == objsetid)
			break;
	}

	mutex_enter(&zfsvfs->z_vfs->vfs_mntpt_lock);
	if (zfsvfs->z_vfs->vfs_mntpoint != NULL) {
		snprintf(full_path, path_len, "%s/.zfs/snapshot/%s",
		    zfsvfs->z_vfs->vfs_mntpoint, snapname);
	} else
		error = SET_ERROR(ENOENT);
	mutex_exit(&zfsvfs->z_vfs->vfs_mntpt_lock);

out:
	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	spl_fstrans_unmark(cookie);

	return (error);
}

/*
 * Special case the handling of "..".
 */
int
zfsctl_root_lookup(struct inode *dip, const char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	int error = 0;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_show_ctldir == ZFS_SNAPDIR_DISABLED) {
		*ipp = NULL;
	} else if (strcmp(name, "..") == 0) {
		*ipp = dip->i_sb->s_root->d_inode;
	} else if (strcmp(name, ZFS_SNAPDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIR,
		    &zpl_fops_snapdir, &zpl_ops_snapdir);
	} else if (strcmp(name, ZFS_SHAREDIR_NAME) == 0) {
		*ipp = zfsctl_inode_lookup(zfsvfs, ZFSCTL_INO_SHARES,
		    &zpl_fops_shares, &zpl_ops_shares);
	} else {
		*ipp = NULL;
	}

	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Lookup entry point for the 'snapshot' directory.  Try to open the
 * snapshot if it exist, creating the pseudo filesystem inode as necessary.
 */
int
zfsctl_snapdir_lookup(struct inode *dip, const char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	uint64_t id;
	int error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	error = dmu_snapshot_lookup(zfsvfs->z_os, name, &id);
	if (error) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	*ipp = zfsctl_inode_lookup(zfsvfs, ZFSCTL_INO_SNAPDIRS - id,
	    &simple_dir_operations, &simple_dir_inode_operations);
	if (*ipp == NULL)
		error = SET_ERROR(ENOENT);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Renaming a directory under '.zfs/snapshot' will automatically trigger
 * a rename of the snapshot to the new given name.  The rename is confined
 * to the '.zfs/snapshot' directory snapshots cannot be moved elsewhere.
 */
int
zfsctl_snapdir_rename(struct inode *sdip, struct dentry *sdentry,
    struct inode *tdip, struct dentry *tdentry, cred_t *cr)
{
	zfsvfs_t *zfsvfs = ITOZSB(sdip);
	const char *snm = dname(sdentry);
	const char *tnm = dname(tdentry);
	char *to, *from, *real, *fsname;
	int error;

	if (!zfs_admin_snapshot)
		return (SET_ERROR(EACCES));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	to = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	from = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	real = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	fsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zfsvfs->z_os, snm, real,
		    ZFS_MAX_DATASET_NAME_LEN, NULL);
		if (error == 0) {
			snm = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	dmu_objset_name(zfsvfs->z_os, fsname);

	error = zfsctl_snapshot_name(ITOZSB(sdip), snm,
	    ZFS_MAX_DATASET_NAME_LEN, from);
	if (error == 0)
		error = zfsctl_snapshot_name(ITOZSB(tdip), tnm,
		    ZFS_MAX_DATASET_NAME_LEN, to);
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

	zfs_snapentry_t *se = sdentry->d_fsdata;
	ASSERT3P(se, !=, NULL);

	/*
	 * Snapshots can be renamed while mounted, so we do not need the
	 * full unmount check; detaching from the control dir is enough.
	 */
	zfsctl_snapshot_invalidate(se, NULL);

	error = dsl_dataset_rename_snapshot(fsname, snm, tnm, B_FALSE);

out:
	kmem_free(from, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(to, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(real, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(fsname, ZFS_MAX_DATASET_NAME_LEN);

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

/*
 * Removing a directory under '.zfs/snapshot' will automatically trigger
 * the removal of the snapshot with the given name.
 */
int
zfsctl_snapdir_remove(struct inode *dip, struct dentry *dentry, cred_t *cr)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	const char *name = dname(dentry);
	char *snapname, *real;
	int error;

	if (!zfs_admin_snapshot)
		return (SET_ERROR(EACCES));

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	snapname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);
	real = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		error = dmu_snapshot_realname(zfsvfs->z_os, name, real,
		    ZFS_MAX_DATASET_NAME_LEN, NULL);
		if (error == 0) {
			name = real;
		} else if (error != ENOTSUP) {
			goto out;
		}
	}

	error = zfsctl_snapshot_name(ITOZSB(dip), name,
	    ZFS_MAX_DATASET_NAME_LEN, snapname);
	if (error == 0)
		error = zfs_secpolicy_destroy_perms(snapname, cr);
	if (error != 0)
		goto out;

out:
	zfs_exit(zfsvfs, FTAG);

	if (error == 0) {
		/*
		 * We have the dentry here so could pass it down to an "inner"
		 * version of zfsctl_snapshot_unmount(), but we'd still have
		 * to pass the name down for the locking there.
		 *
		 * Snapshot delete through admin snapdir operation should be
		 * rare enough that any gain isn't worth the extra code
		 * complexity, but the option is there for the future.
		 */
		zfsctl_snapshot_unmount(snapname);
		error = dsl_destroy_snapshot(snapname, B_FALSE);
	}

	kmem_free(snapname, ZFS_MAX_DATASET_NAME_LEN);
	kmem_free(real, ZFS_MAX_DATASET_NAME_LEN);

	return (error);
}

/*
 * Creating a directory under '.zfs/snapshot' will automatically trigger
 * the creation of a new snapshot with the given name.
 */
int
zfsctl_snapdir_mkdir(struct inode *dip, const char *dirname, vattr_t *vap,
    struct inode **ipp, cred_t *cr, int flags)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	char *dsname;
	int error;

	if (!zfs_admin_snapshot)
		return (SET_ERROR(EACCES));

	dsname = kmem_alloc(ZFS_MAX_DATASET_NAME_LEN, KM_SLEEP);

	if (zfs_component_namecheck(dirname, NULL, NULL) != 0) {
		error = SET_ERROR(EILSEQ);
		goto out;
	}

	dmu_objset_name(zfsvfs->z_os, dsname);

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
	kmem_free(dsname, ZFS_MAX_DATASET_NAME_LEN);

	return (error);
}

/*
 * Invalidate a snapdir
 *
 * As described elsewhere, we can't perform a true unmount; all we can do is
 * detach it from the parent and trust other mechanisms to drain it, or handle
 * it being in use. We also must be prepared for something unexpected being
 * mounted on the snapdir, or nothing at all.
 *
 * Most of this function is studying the current situation and trying to
 * decide if we do detach it, the "unmount" (ie filesystem teardown) will
 * happen immediately after.
 *
 * In the very common cases where the admin is not doing any additional mount
 * manipulation and there's nothing currently using the dataset, this will
 * usually succeed, which should be good enough.
 */
typedef struct {
	zfs_snapentry_t	*sba_snapentry;
	uint64_t	sba_atime;
} zfsctl_snapshot_sb_atime_cb_args_t;

static void
zfsctl_snapshot_sb_atime_cb(struct super_block *sb, void *sbap)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;
	if (zfsvfs == NULL)
		return;

	zfsctl_snapshot_sb_atime_cb_args_t *sba = sbap;
	if (dmu_objset_spa(zfsvfs->z_os) != sba->sba_snapentry->se_spa ||
	    dmu_objset_id(zfsvfs->z_os) != sba->sba_snapentry->se_objsetid)
		return;

	sba->sba_atime = atomic_load_64(&zfsvfs->z_snap_atime);
}

static int
zfsctl_snapshot_invalidate(zfs_snapentry_t *se, unsigned long *delay)
{
	/*
	 * Wait for any pending automount or unmount to complete before
	 * attempting unmount.
	 */
	mutex_enter(&se->se_mtx);
	while (SE_TEST(se, SE_BUSY))
		cv_wait(&se->se_cv, &se->se_mtx);
	if (d_unhashed(se->se_dentry)) {
		/* Someone else invalidated it while we were waiting. */
		mutex_exit(&se->se_mtx);
		return (0);
	}
	SE_SET(se, SE_BUSY);
	mutex_exit(&se->se_mtx);

	if (delay != NULL && d_mountpoint(se->se_dentry)) {
		/*
		 * This is the expiry task. Consider if the snapdir dentry
		 * or the snapshot data has been accessed within the expiry
		 * period and defer if so.
		 */

		unsigned long atime = atomic_load_64(&se->se_atime);

		/*
		 * Search for the superblock for the dataset we mounted, and
		 * grab its access time. We have to search because we can't
		 * hold a reference back to the zfsvfs or superblock, because
		 * that would pin it and prevent it being destroyed. We will
		 * never outlive our reference spa though, so there's no risk
		 * of the spa pointer being invalid.
		 */
		zfsctl_snapshot_sb_atime_cb_args_t sba = {
			.sba_snapentry = se,
			.sba_atime = 0,
		};
		iterate_supers_type(&zpl_fs_type,
		    zfsctl_snapshot_sb_atime_cb, &sba);

		atime = MAX(atime, sba.sba_atime);

		unsigned long expiry = atime +
		    (MAX(zfs_expire_snapshot, 0) * HZ);
		unsigned long now = jiffies;

		if (time_before(now, expiry)) {
			/*
			 * It was used more recently than the expiry time. Pass
			 * the remaining time back to the caller for rearming.
			 */
			*delay = expiry - now;

			mutex_enter(&se->se_mtx);
			SE_CLEAR(se, SE_BUSY);
			cv_broadcast(&se->se_cv);
			mutex_exit(&se->se_mtx);

			return (SET_ERROR(EAGAIN));
		}
	}

	/*
	 * Take a hold to keep the dentry+snapentry alive after invalidate
	 * so we can signal if necessary.
	 */
	dget(se->se_dentry);

	/* Detach the control dentry from the parent dataset. */
	d_invalidate(se->se_dentry);

	/* Signal any waiters in zpl_snapdir_manage(). */
	mutex_enter(&se->se_mtx);
	SE_CLEAR(se, SE_BUSY);
	cv_broadcast(&se->se_cv);
	mutex_exit(&se->se_mtx);

	/*
	 * Release our hold, which may be the last one. If so, the snapentry
	 * may be destroyed immediately, so must not be used after this.
	 */
	dput(se->se_dentry);

	return (0);
}

/*
 * Unmount snapshot by name.
 *
 * Some admin ops (eg `zfs destroy`) request an unmount before they begin
 * their work. We have no way to know at any given moment if the snapshot is
 * definitely mounted - on our snapdir or anywhere else - because some other
 * task may in the process of mounting or unmounting it. As such, this is
 * always best effort.
 */

/*
 * Check if the named snapshot is definitely unmounted. Returns true if it
 * is, false if its not or we're unsure.
 *
 * We check long holds here, rather than using getzfsvfs(), because there is
 * a significant timing gap between "filesystem unmounted" and "owning long
 * hold released". That does mean that other long holds (`zfs send`,
 * `zfs diff`, etc) can cause a false return here, but that's ok - it will
 * just mean we try some different things, and if it turns out the hold is
 * unrelated, the next operation (eg `dsl_destroy_snapshot()`) will return
 * `EBUSY` anyway, which is correct.
 */
static bool
zfsctl_snapshot_unmount_check(const char *snapname)
{
	dsl_pool_t *dp;
	if (dsl_pool_hold(snapname, FTAG, &dp) != 0)
		return (true);

	dsl_dataset_t *ds;
	if (dsl_dataset_hold(dp, snapname, FTAG, &ds) != 0) {
		dsl_pool_rele(dp, FTAG);
		return (true);
	}

	bool held = dsl_dataset_long_held(ds);
	dsl_dataset_rele(ds, FTAG);
	dsl_pool_rele(dp, FTAG);

	return (!held);
}

/*
 * Wait for the named snapshot to be unmounted. tqid is the task that is trying
 * to force the unmount. Returns true when the snapshot is unmounted, false if
 * still in use at the deadline.
 *
 * Despite setting MNT_INTERNAL, the final dput()/mntput() may still put on a
 * delay queue if there is associated teardown to be done (eg alt namespaces
 * for mount propagation). zfsctl_snapshot_unmount() is usually called from a
 * user thread (an ioctl), which uses a per-task workqueue for this purpose,
 * and empties it on return to userspace. This too late for an operation like
 * `zfs destroy` that does the unmount in preparation for the real operation.
 *
 * When the final dput()/mntput() is done from a kernel thread, then it is
 * queued onto a system workqueue instead. So, we put those possible "last put"
 * calls on system_taskq and wait for them, and then call here to wait for the
 * unmount to occur, if it is going to occur.
 *
 * Delayed work queueing runs on a jiffy timer, so the mntput() may not be on
 * the queue yet when we call zpl_flush_delay_workqueue(). So, we sleep for one
 * jiffy each iteration, and flush the queue each time, for 20 milliseconds.
 * Most of the time we'll see the task within 2-3 jiffies so this is quite a
 * generous timeout. Worst case, we pause a while, timeout, and eventually
 * return EBUSY.
 *
 * if tqid is TASKQID_INVALID, this does a single check unmount check then
 * returns, as there's no point waiting if no task was dispatched.
 */
static bool
zfsctl_snapshot_unmount_wait(const char *snapname, taskqid_t tqid)
{
	if (tqid == TASKQID_INVALID)
		return (zfsctl_snapshot_unmount_check(snapname));

	taskq_wait_id(system_taskq, tqid);

	unsigned long deadline = jiffies + MSEC_TO_TICK(20);

	while (!zfsctl_snapshot_unmount_check(snapname)) {
		if (time_after_eq(jiffies, deadline))
			return (false);

		schedule_timeout_idle(1);
		zpl_flush_delay_workqueue();
	}

	return (true);
}

/*
 * Invalidate task. Note that the `dput()` is what will actually trigger
 * the (delayed-)unmount, so it has to be in the task too.
 */
static void
zfsctl_snapshot_unmount_invalidate_task(void *arg)
{
	struct dentry *dentry = arg;
	zfs_snapentry_t *se = dentry->d_fsdata;

	zfsctl_snapshot_invalidate(se, NULL);
	dput(dentry);
}

/*
 * Flush the kernel's NFS export table.
 *
 * If the snapshot dir has been used over NFS, the relevant entries in the
 * svc_expkey_cache and svc_export_cache caches hold references to the snapshot
 * mount point.
 *
 * A full flush is aggressive (flushes everything), but easy to implement. The
 * alternative is to use the channel endpoints to find and evict the specific
 * paths related to the path we're unmounting, but that's a lot more involved
 * for minimal gain.
 */
static void
zfsctl_snapshot_unmount_nfs_flush(void)
{
	/*
	 * We use a userspace callout for this, as there are no kernel APIs
	 * available to do this from outside the NFS server, and simulating
	 * access to sunrpc cache file endpoints from within the kernel
	 * requires userspace-mapped data buffer, which we do not have.
	 *
	 * `exportfs -f` would do what we need here, however that flushes all
	 * nfsd caches, not just the ones with pinned dentries, but also may
	 * not be installed or on a consistent path (unlikely on a NFS-using
	 * machine, but still). The shell is guaranteed to be at /bin/sh, so
	 * we can rely on it.
	 *
	 * Userspace callouts are always called with root privileges in the
	 * init namespaces, so the only thing that prevents this from working
	 * is if /proc is not mounted. That's the most unlikely thing of all,
	 * and since this is best-effort, it will have to do.
	 */
	char *argv[] = {
	    "/bin/sh", "-c",
	    "echo 1 > /proc/net/rpc/nfsd.h/flush ; "
	    "echo 1 > /proc/net/rpc/nfsd.export/flush", NULL };
	char *envp[] = { NULL };

	call_usermodehelper(argv[0], argv, envp, UMH_WAIT_PROC);
}

/*
 * The public entry point. Try to unmount the snapshot with the given name.
 * We only consider the snapdir; additional mounts elsewhere will not be
 * touched.
 *
 * Since this is best-effort, always returns 0.
 */
int
zfsctl_snapshot_unmount(const char *snapname)
{
	taskqid_t tqid = TASKQID_INVALID;

	/* Incoming snapname is 'pool/dataset@snap'. Split on the '@' */
	char *ds = kmem_strdup(snapname);
	char *snap = strchr(ds, '@');
	if (snap == NULL) {
		kmem_strfree(ds);
		return (0);
	}
	*snap++ = '\0';

	/* Get a handle on the dataset itself, which holds the control dir. */
	zfsvfs_t *zfsvfs = NULL;
	int err = getzfsvfs(ds, &zfsvfs);
	if (err != 0) {
		ASSERT0P(zfsvfs);
		kmem_strfree(ds);
		return (0);
	}

	/* Find the virtual inode for the `.zfs/snapshot` dir. */
	struct inode *snapdir_ip = ilookup(zfsvfs->z_sb, ZFSCTL_INO_SNAPDIR);
	if (snapdir_ip == NULL) {
		zfs_vfs_rele(zfsvfs);
		kmem_strfree(ds);
		return (0);
	}

	/*
	 * And its associated dentry.
	 *
	 * Note that from this point, we always proceed into the "wait" step
	 * even if we don't find what we're looking for in the ctldir, because
	 * once the ctldir inode structure is established we might not be
	 * finding things because we're racing against setup or teardown
	 * elsewhere in the system, and so there still might be a leftover
	 * mount that we should try to force out if we can.
	 */
	struct dentry *snapdir_dentry = d_find_alias(snapdir_ip);
	iput(snapdir_ip);
	if (snapdir_dentry == NULL) {
		zfs_vfs_rele(zfsvfs);
		kmem_strfree(ds);
		goto wait;
	}

	/*
	 * Now hash the snapshot name, and use it to lookup the snapdir
	 * of the same name.
	 */
	struct qstr qname = QSTR_INIT(snap, strlen(snap));
	qname.hash = full_name_hash(snapdir_dentry, snap, qname.len);
	struct dentry *dentry = d_lookup(snapdir_dentry, &qname);

	dput(snapdir_dentry);
	kmem_strfree(ds);
	zfs_vfs_rele(zfsvfs);

	/*
	 * Sanity; we should always have a dentry with an attached snapentry,
	 * but if we don't, we can't do anything else except try to push a
	 * background expiry along.
	 */
	if (dentry == NULL)
		goto wait;
	if (dentry->d_fsdata == NULL) {
		dput(dentry);
		goto wait;
	}

	/*
	 * If there's nothing mounted on this dentry, then we don't need to
	 * invalidate it, but we should still try to wait for our snapshot
	 * to expire and try to force it along.
	 *
	 * We continue to invalidate if anything is mounted here, because
	 * the operator may have mounted an unrelated filesystem here, and
	 * this is the only way it will be detached during an admin operation.
	 */
	if (!d_mountpoint(dentry)) {
		dput(dentry);
		goto wait;
	}

	/*
	 * Do invalidate + dput on system_taskq thread to force delayed mntput
	 * (if required) onto system_wq. If dispatch fails for some reason,
	 * call it directly and we'll just have to live with a possible EBUSY
	 * down the line.
	 */
	tqid = taskq_dispatch(system_taskq,
	    zfsctl_snapshot_unmount_invalidate_task, dentry, TQ_SLEEP|TQ_FRONT);
	if (tqid == TASKQID_INVALID)
		zfsctl_snapshot_unmount_invalidate_task(dentry);

wait:
	/*
	 * Wait for unmount. We do this regardless of whether or not we
	 * found the snapdir dentry above; it might have been expired out
	 * elsewhere in the system while the mount was still in use.
	 */
	if (zfsctl_snapshot_unmount_wait(snapname, tqid))
		return (0);

	/*
	 * Still mounted. NFS caches might be pinning it. If so, then flushing
	 * them will release the mount. Note that we do this after trying our
	 * best to force the unmount in other ways, because the NFS cache
	 * flush is global, and if the dataset and the whole system is
	 * actually busy, then overdoing this is going to hurt NFS performance.
	 */
	zfsctl_snapshot_unmount_nfs_flush();

	/*
	 * No wait required after NFS flush; if it resulted in unmount, it
	 * happened on the return to userspace and so there's nothing to wait
	 * for.
	 */

	return (0);
}

/*
 * Mount. This is the actual work behind the d_automount endpoint. On success,
 * the mount is returned in *mntp, and the manage->automount->manage ceremony
 * will get it all properly wired into the tree and any waiters released.
 */
int
zfsctl_snapshot_mount(struct path *path, struct vfsmount **mntp)
{
	struct dentry *dentry = path->dentry;
	struct inode *ip = dentry->d_inode;
	zfsvfs_t *zfsvfs;
	zfsvfs_t *snap_zfsvfs;
	zfs_snapentry_t *se;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int error;

	ASSERT3P(ip, !=, NULL);

	/*
	 * ip is the snapdir inode itself, so zfsvfs is the parent (real)
	 * dataset. We only need to take the hold in order to compute the
	 * snapshot name; the calling dentry is what's actually holding it
	 * alive.
	 */
	zfsvfs = ITOZSB(ip);
	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	error = zfsctl_snapshot_name(zfsvfs, dname(dentry),
	    sizeof (snapname), snapname);

	zfs_exit(zfsvfs, FTAG);
	if (error)
		return (error);

	/*
	 * A "submount" inherits its options, propagation group, namespaces,
	 * etc from the reference dentry.
	 */
	struct fs_context *fc =
	    fs_context_for_submount(dentry->d_sb->s_type, dentry);
	if (IS_ERR(fc))
		return (-PTR_ERR(fc));

	/* The full snapshot name is the "source" (see zpl_get_tree()) */
	error = -zpl_vfs_parse_fs_string(fc, "source", snapname);
	if (error != 0) {
		put_fs_context(fc);
		return (error);
	}

	/* Create the mount! */
	struct vfsmount *mnt = fc_mount(fc);
	put_fs_context(fc);

	if (IS_ERR(mnt))
		return (-PTR_ERR(mnt));

	snap_zfsvfs = ITOZSB(mnt->mnt_root->d_inode);
	snap_zfsvfs->z_parent = zfsvfs;

	se = dentry->d_fsdata;

	/* Clear any leftover timer from previous iteration. */
	zfsctl_snapshot_timer_clear(se);

	/* Fill out the snapentry with lookup helpers */
	se->se_spa = dmu_objset_spa(snap_zfsvfs->z_os);
	se->se_objsetid = dmu_objset_id(snap_zfsvfs->z_os);

	*mntp = mnt;

	return (0);
}

/*
 * Called after the mount is spliced into the filesystem tree but before path
 * walks are allowed to proceed into it. See zpl_ctldir.c for more info.
 */
void
zfsctl_snapshot_finish_mount(zfs_snapentry_t *se, struct vfsmount *mnt)
{
	/*
	 * MNT_INTERNAL makes the mount "internal", and so give more chance
	 * that it will unmounted when the last reference is dropped rather
	 * than being put on a delay queue. It's not foolproof (some
	 * mount-propagation configurations will still see it deferred) but
	 * when it works it reduces the work we need to do in
	 * zfsctl_snapshot_unmount(), and when it doesn't it's harmless.
	 */
	mnt->mnt_flags |= MNT_INTERNAL;

	/*
	 * Add any additional user flags which would have been swallowed if we
	 * set them when the mount was created.
	 */
	if (zfs_snapshot_no_setuid)
		mnt->mnt_flags |= MNT_NOSUID;

	/* Start the expiry timer. */
	se->se_atime = jiffies;
	zfsctl_snapshot_timer_set(se, zfs_expire_snapshot * HZ);
}

/*
 * Get the snapdir inode from fid
 */
int
zfsctl_snapdir_vget(struct super_block *sb, uint64_t objsetid, int gen,
    struct inode **ipp)
{
	zfsvfs_t *zfsvfs = sb->s_fs_info;
	int error;
	struct path path;
	char *mnt;
	struct dentry *dentry;

	mnt = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	error = zfsctl_snapshot_path_objset(zfsvfs, objsetid, MAXPATHLEN, mnt);
	if (error)
		goto out;

	/* Trigger automount */
	error = -kern_path(mnt, LOOKUP_FOLLOW|LOOKUP_DIRECTORY, &path);
	if (error)
		goto out;

	path_put(&path);
	/*
	 * Get the snapdir inode. Note, we don't want to use the above
	 * path because it contains the root of the snapshot rather
	 * than the snapdir.
	 */
	*ipp = ilookup(sb, ZFSCTL_INO_SNAPDIRS - objsetid);
	if (*ipp == NULL) {
		error = SET_ERROR(ENOENT);
		goto out;
	}

	/* check gen, see zfsctl_snapdir_fid */
	dentry = d_obtain_alias(igrab(*ipp));
	if (gen != (!IS_ERR(dentry) && d_mountpoint(dentry))) {
		iput(*ipp);
		*ipp = NULL;
		error = SET_ERROR(ENOENT);
	}
	if (!IS_ERR(dentry)) {
		/*
		 * Cold-open via NFS will have a disconnected dentry here,
		 * don't assume there's an associated snapentry here.
		 */
		if (error == 0 && dentry->d_fsdata != NULL) {
			zfs_snapentry_t *se = dentry->d_fsdata;
			atomic_store_64(&se->se_atime, jiffies);
		}
		dput(dentry);
	}

out:
	kmem_free(mnt, MAXPATHLEN);
	return (error);
}

int
zfsctl_shares_lookup(struct inode *dip, char *name, struct inode **ipp,
    int flags, cred_t *cr, int *direntflags, pathname_t *realpnp)
{
	zfsvfs_t *zfsvfs = ITOZSB(dip);
	znode_t *zp;
	znode_t *dzp;
	int error;

	if ((error = zfs_enter(zfsvfs, FTAG)) != 0)
		return (error);

	if (zfsvfs->z_shares_dir == 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENOTSUP));
	}

	if ((error = zfs_zget(zfsvfs, zfsvfs->z_shares_dir, &dzp)) == 0) {
		error = zfs_lookup(dzp, name, &zp, 0, cr, NULL, NULL);
		zrele(dzp);
	}

	zfs_exit(zfsvfs, FTAG);

	return (error);
}

module_param(zfs_admin_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_admin_snapshot, "Enable mkdir/rmdir/mv in .zfs/snapshot");

module_param(zfs_expire_snapshot, int, 0644);
MODULE_PARM_DESC(zfs_expire_snapshot, "Seconds to expire .zfs/snapshot");

module_param(zfs_snapshot_no_setuid, int, 0644);
MODULE_PARM_DESC(zfs_snapshot_no_setuid,
	"Disable setuid/setgid for automounts in .zfs/snapshot");
