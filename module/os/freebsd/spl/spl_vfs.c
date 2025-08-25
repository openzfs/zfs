// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2006-2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/cred.h>
#include <sys/vfs.h>
#include <sys/priv.h>
#include <sys/libkern.h>

#include <sys/mutex.h>
#include <sys/vnode.h>
#include <sys/taskq.h>

#include <sys/ccompat.h>

MALLOC_DECLARE(M_MOUNT);

void
vfs_setmntopt(vfs_t *vfsp, const char *name, const char *arg,
    int flags __unused)
{
	struct vfsopt *opt;
	size_t namesize;
	int locked;

	if (!(locked = mtx_owned(MNT_MTX(vfsp))))
		MNT_ILOCK(vfsp);

	if (vfsp->mnt_opt == NULL) {
		void *opts;

		MNT_IUNLOCK(vfsp);
		opts = malloc(sizeof (*vfsp->mnt_opt), M_MOUNT, M_WAITOK);
		MNT_ILOCK(vfsp);
		if (vfsp->mnt_opt == NULL) {
			vfsp->mnt_opt = opts;
			TAILQ_INIT(vfsp->mnt_opt);
		} else {
			free(opts, M_MOUNT);
		}
	}

	MNT_IUNLOCK(vfsp);

	opt = malloc(sizeof (*opt), M_MOUNT, M_WAITOK);
	namesize = strlen(name) + 1;
	opt->name = malloc(namesize, M_MOUNT, M_WAITOK);
	strlcpy(opt->name, name, namesize);
	opt->pos = -1;
	opt->seen = 1;
	if (arg == NULL) {
		opt->value = NULL;
		opt->len = 0;
	} else {
		opt->len = strlen(arg) + 1;
		opt->value = malloc(opt->len, M_MOUNT, M_WAITOK);
		memcpy(opt->value, arg, opt->len);
	}

	MNT_ILOCK(vfsp);
	TAILQ_INSERT_TAIL(vfsp->mnt_opt, opt, link);
	if (!locked)
		MNT_IUNLOCK(vfsp);
}

void
vfs_clearmntopt(vfs_t *vfsp, const char *name)
{
	int locked;

	if (!(locked = mtx_owned(MNT_MTX(vfsp))))
		MNT_ILOCK(vfsp);
	vfs_deleteopt(vfsp->mnt_opt, name);
	if (!locked)
		MNT_IUNLOCK(vfsp);
}

int
vfs_optionisset(const vfs_t *vfsp, const char *opt, char **argp)
{
	struct vfsoptlist *opts = vfsp->mnt_optnew;
	int error;

	if (opts == NULL)
		return (0);
	error = vfs_getopt(opts, opt, (void **)argp, NULL);
	return (error != 0 ? 0 : 1);
}

int
mount_snapshot(kthread_t *td, vnode_t **vpp, const char *fstype, char *fspath,
    char *fspec, int fsflags, vfs_t *parent_vfsp)
{
	struct vfsconf *vfsp;
	struct mount *mp;
	vnode_t *vp, *mvp;
	int error;

	ASSERT_VOP_ELOCKED(*vpp, "mount_snapshot");

	vp = *vpp;
	*vpp = NULL;
	error = 0;

	/*
	 * Be ultra-paranoid about making sure the type and fspath
	 * variables will fit in our mp buffers, including the
	 * terminating NUL.
	 */
	if (strlen(fstype) >= MFSNAMELEN || strlen(fspath) >= MNAMELEN)
		error = ENAMETOOLONG;
	if (error == 0 && (vfsp = vfs_byname_kld(fstype, td, &error)) == NULL)
		error = ENODEV;
	if (error == 0 && vp->v_type != VDIR)
		error = ENOTDIR;
	/*
	 * We need vnode lock to protect v_mountedhere and vnode interlock
	 * to protect v_iflag.
	 */
	if (error == 0) {
		VI_LOCK(vp);
		if ((vp->v_iflag & VI_MOUNT) == 0 && vp->v_mountedhere == NULL)
			vp->v_iflag |= VI_MOUNT;
		else
			error = EBUSY;
		VI_UNLOCK(vp);
	}
	if (error != 0) {
		vput(vp);
		return (error);
	}
	vn_seqc_write_begin(vp);
	VOP_UNLOCK(vp);

	/*
	 * Allocate and initialize the filesystem.
	 * We don't want regular user that triggered snapshot mount to be able
	 * to unmount it, so pass credentials of the parent mount.
	 */
	mp = vfs_mount_alloc(vp, vfsp, fspath, vp->v_mount->mnt_cred);

	mp->mnt_optnew = NULL;
	vfs_setmntopt(mp, "from", fspec, 0);
	mp->mnt_optnew = mp->mnt_opt;
	mp->mnt_opt = NULL;

	/*
	 * Set the mount level flags.
	 */
	mp->mnt_flag = fsflags & MNT_UPDATEMASK;
	/*
	 * Snapshots are always read-only.
	 */
	mp->mnt_flag |= MNT_RDONLY;
	/*
	 * We don't want snapshots to allow access to vulnerable setuid
	 * programs, so we turn off setuid when mounting snapshots.
	 */
	mp->mnt_flag |= MNT_NOSUID;
	/*
	 * We don't want snapshots to be visible in regular
	 * mount(8) and df(1) output.
	 */
	mp->mnt_flag |= MNT_IGNORE;

	error = VFS_MOUNT(mp);
	if (error != 0) {
		/*
		 * Clear VI_MOUNT and decrement the use count "atomically",
		 * under the vnode lock.  This is not strictly required,
		 * but makes it easier to reason about the life-cycle and
		 * ownership of the covered vnode.
		 */
		vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
		VI_LOCK(vp);
		vp->v_iflag &= ~VI_MOUNT;
		VI_UNLOCK(vp);
		vn_seqc_write_end(vp);
		vput(vp);
		vfs_unbusy(mp);
		vfs_freeopts(mp->mnt_optnew);
		mp->mnt_vnodecovered = NULL;
		vfs_mount_destroy(mp);
		return (error);
	}

	if (mp->mnt_opt != NULL)
		vfs_freeopts(mp->mnt_opt);
	mp->mnt_opt = mp->mnt_optnew;
	(void) VFS_STATFS(mp, &mp->mnt_stat);

#ifdef VFS_SUPPORTS_EXJAIL_CLONE
	/*
	 * Clone the mnt_exjail credentials of the parent, as required.
	 */
	vfs_exjail_clone(parent_vfsp, mp);
#endif

	/*
	 * Prevent external consumers of mount options from reading
	 * mnt_optnew.
	 */
	mp->mnt_optnew = NULL;

	vn_lock(vp, LK_EXCLUSIVE | LK_RETRY);
#ifdef FREEBSD_NAMECACHE
	cache_purge(vp);
#endif
	VI_LOCK(vp);
	vp->v_iflag &= ~VI_MOUNT;
#ifdef VIRF_MOUNTPOINT
	vn_irflag_set_locked(vp, VIRF_MOUNTPOINT);
#endif
	vp->v_mountedhere = mp;
	VI_UNLOCK(vp);
	/* Put the new filesystem on the mount list. */
	mtx_lock(&mountlist_mtx);
	TAILQ_INSERT_TAIL(&mountlist, mp, mnt_list);
	mtx_unlock(&mountlist_mtx);
	vfs_event_signal(NULL, VQ_MOUNT, 0);
	if (VFS_ROOT(mp, LK_EXCLUSIVE, &mvp))
		panic("mount: lost mount");
	vn_seqc_write_end(vp);
	VOP_UNLOCK(vp);
	vfs_op_exit(mp);
	vfs_unbusy(mp);
	*vpp = mvp;
	return (0);
}

static void
vrele_task_runner(void *vp)
{
	vrele((vnode_t *)vp);
}

/*
 * Like vn_rele() except if we are going to call VOP_INACTIVE() then do it
 * asynchronously using a taskq. This can avoid deadlocks caused by re-entering
 * the file system as a result of releasing the vnode. Note, file systems
 * already have to handle the race where the vnode is incremented before the
 * inactive routine is called and does its locking.
 *
 * Warning: Excessive use of this routine can lead to performance problems.
 * This is because taskqs throttle back allocation if too many are created.
 */
void
vn_rele_async(vnode_t *vp, taskq_t *taskq)
{
	VERIFY3U(vp->v_usecount, >, 0);
	if (refcount_release_if_not_last(&vp->v_usecount))
		return;
	VERIFY3U(taskq_dispatch((taskq_t *)taskq, vrele_task_runner, vp,
	    TQ_SLEEP), !=, 0);
}
