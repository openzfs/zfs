// SPDX-License-Identifier: BSD-2-Clause

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/debug.h>
#include <sys/nvpair.h>
#include <sys/spa_impl.h>
#include <sys/mount.h>
#include <sys/lwp.h>
#include <sys/kauth.h>
#include <sys/atomic.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/taskq.h>
#include <sys/dsl_pool.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_deleg.h>
#include <sys/policy.h>

/*
 * vfs_trybusy is fstrans_start_nowait plus a ref.  fstrans_start
 * sleeps, and it ASSERT_SLEEPABLE even on the nowait path, so it
 * cannot run under os_user_ptr_lock.  The caller of this helper
 * still holds the pool config lock (dmu_objset_from_ds /
 * zfs_get_temporary_prop), so a blocking vfs_busy here can deadlock
 * a suspend that needs that lock.  EBUSY is "mounted, fstrans is
 * suspending", not "no zfsvfs".  Callers that can drop the pool
 * lock use getzfsvfs() and wait.  This helper must not.
 *
 * VFS_UNMOUNT does not wait for vfs_busy.  suspendctl returns
 * 0 and does not drain fstrans.  A plain vfs_ref is not enough:
 * unmount frees the zfsvfs.  After trybusy, look the zfsvfs up
 * again under os_user_ptr_lock and pin z_nb_holds before dropping
 * that lock.  Unmount clears the user pointer under the same
 * lock, then sets ZFS_NB_HOLD_GONE.  The last pin frees.
 * fstrans is per LWP.  vfs_unbusy on a different LWP is a no-op.
 */
int
getzfsvfs_impl(objset_t *os, zfsvfs_t **zfvp)
{
	struct mount *mp = NULL;
	zfsvfs_t *zfsvfs;
	int error;

	if (dmu_objset_type(os) != DMU_OST_ZFS)
		return (SET_ERROR(EINVAL));

	mutex_enter(&os->os_user_ptr_lock);
	zfsvfs = dmu_objset_get_user(os);
	if (zfsvfs != NULL && zfsvfs->z_vfs != NULL) {
		mp = zfsvfs->z_vfs;
		vfs_ref(mp);
	}
	mutex_exit(&os->os_user_ptr_lock);
	if (mp == NULL) {
		*zfvp = NULL;
		return (SET_ERROR(ESRCH));
	}
	error = vfs_trybusy(mp);
	vfs_rele(mp);
	if (error != 0) {
		*zfvp = NULL;
		return (SET_ERROR(ESRCH));
	}
	mutex_enter(&os->os_user_ptr_lock);
	zfsvfs = dmu_objset_get_user(os);
	if (zfsvfs == NULL || zfsvfs->z_vfs != mp) {
		mutex_exit(&os->os_user_ptr_lock);
		vfs_unbusy(mp);
		*zfvp = NULL;
		return (SET_ERROR(ESRCH));
	}
	/* Pin before drop.  Unmount clears the user pointer first. */
	atomic_inc_32(&zfsvfs->z_nb_holds);
	mutex_exit(&os->os_user_ptr_lock);
	*zfvp = zfsvfs;
	return (0);
}

/*
 * Mounted filesystems are found here.  zfsvfs_hold() only calls
 * zfsvfs_create_hold() when this returns non-zero, and that hold
 * does not decrypt: it is for an objset with no mount.  A trybusy
 * failure during fstrans suspend used to look the same as "not
 * mounted", so userquota built a second zfsvfs and asked for the
 * wrapping key.  Wait for suspend outside the pool config lock
 * instead.  Do not read the zfsvfs until vfs_busy has succeeded.
 */
int
getzfsvfs(const char *dsname, zfsvfs_t **zfvp)
{
	objset_t *os;
	struct mount *mp;
	zfsvfs_t *zfsvfs;
	int error;

	*zfvp = NULL;
	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0)
		return (error);

	if (dmu_objset_type(os) != DMU_OST_ZFS) {
		dmu_objset_rele(os, FTAG);
		return (SET_ERROR(EINVAL));
	}

	mp = NULL;
	mutex_enter(&os->os_user_ptr_lock);
	zfsvfs = dmu_objset_get_user(os);
	if (zfsvfs != NULL && zfsvfs->z_vfs != NULL) {
		mp = zfsvfs->z_vfs;
		vfs_ref(mp);
	}
	mutex_exit(&os->os_user_ptr_lock);
	dmu_objset_rele(os, FTAG);

	if (mp == NULL)
		return (SET_ERROR(ESRCH));

	error = vfs_busy(mp);
	vfs_rele(mp);
	if (error != 0)
		return (SET_ERROR(ESRCH));

	error = dmu_objset_hold(dsname, FTAG, &os);
	if (error != 0) {
		vfs_unbusy(mp);
		return (error);
	}
	mutex_enter(&os->os_user_ptr_lock);
	zfsvfs = dmu_objset_get_user(os);
	if (zfsvfs == NULL || zfsvfs->z_vfs != mp) {
		mutex_exit(&os->os_user_ptr_lock);
		dmu_objset_rele(os, FTAG);
		vfs_unbusy(mp);
		return (SET_ERROR(ESRCH));
	}
	/* Pin before drop.  Unmount clears the user pointer first. */
	atomic_inc_32(&zfsvfs->z_nb_holds);
	mutex_exit(&os->os_user_ptr_lock);
	dmu_objset_rele(os, FTAG);
	*zfvp = zfsvfs;
	return (0);
}

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_vfs != NULL);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	vfs_t *vfsp = zfsvfs->z_vfs;
	uint32_t n;

	/*
	 * vfs_busy does not keep this struct.  z_nb_holds does.
	 * The mount pointer is copied before the drop: this may
	 * be the last pin.  fhtovp reads mnt_data under
	 * mnt_updating, so null it there before the free.
	 * Unbusy after that lock is dropped.  unbusy may free
	 * the mount.
	 */
	n = atomic_dec_32_nv(&zfsvfs->z_nb_holds);
	/*
	 * Low 31 bits of all-ones means the pin count underflowed.
	 */
	ASSERT3U(n & ~ZFS_NB_HOLD_GONE, !=, ~ZFS_NB_HOLD_GONE);
	if (n == ZFS_NB_HOLD_GONE) {
		mutex_enter(vfsp->mnt_updating);
		vfsp->vfs_data = NULL;
		mutex_exit(vfsp->mnt_updating);
	}
	vfs_unbusy(vfsp);
	if (n == ZFS_NB_HOLD_GONE) {
		zfsvfs_free(zfsvfs);
		atomic_dec_32(&zfs_active_fs_count);
	}
}

void
zfs_ioctl_update_mount_cache(const char *dsname)
{
	(void) dsname;
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);
	return (ptoa((uint64_t)physmem) / 4);
}

void
zfs_ioctl_init_os(void)
{
}
