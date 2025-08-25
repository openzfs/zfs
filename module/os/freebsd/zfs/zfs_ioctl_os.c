// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2020 iXsystems, Inc.
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
 *
 */

#include <sys/types.h>
#include <sys/errno.h>
#include <sys/nvpair.h>
#include <sys/spa_impl.h>
#include <sys/vdev_os.h>
#include <sys/zfs_vfsops.h>
#include <sys/zone.h>
#include <vm/vm_pageout.h>

#include <sys/zfs_ioctl_impl.h>

int
zfs_vfs_ref(zfsvfs_t **zfvp)
{
	int error = 0;

	if (*zfvp == NULL)
		return (SET_ERROR(ESRCH));

	error = vfs_busy((*zfvp)->z_vfs, 0);
	if (error != 0) {
		*zfvp = NULL;
		error = SET_ERROR(ESRCH);
	}
	return (error);
}

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_vfs != NULL);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	vfs_unbusy(zfsvfs->z_vfs);
}

static const zfs_ioc_key_t zfs_keys_nextboot[] = {
	{"command",		DATA_TYPE_STRING,	0},
	{ ZPOOL_CONFIG_POOL_GUID,		DATA_TYPE_UINT64,	0},
	{ ZPOOL_CONFIG_GUID,		DATA_TYPE_UINT64,	0}
};

static int
zfs_ioc_jail(zfs_cmd_t *zc)
{

	return (zone_dataset_attach(curthread->td_ucred, zc->zc_name,
	    (int)zc->zc_zoneid));
}

static int
zfs_ioc_unjail(zfs_cmd_t *zc)
{

	return (zone_dataset_detach(curthread->td_ucred, zc->zc_name,
	    (int)zc->zc_zoneid));
}

static int
zfs_ioc_nextboot(const char *unused, nvlist_t *innvl, nvlist_t *outnvl)
{
	char name[MAXNAMELEN];
	spa_t *spa;
	vdev_t *vd;
	const char *command;
	uint64_t pool_guid;
	uint64_t vdev_guid;
	int error;

	if (nvlist_lookup_uint64(innvl,
	    ZPOOL_CONFIG_POOL_GUID, &pool_guid) != 0)
		return (EINVAL);
	if (nvlist_lookup_uint64(innvl,
	    ZPOOL_CONFIG_GUID, &vdev_guid) != 0)
		return (EINVAL);
	if (nvlist_lookup_string(innvl,
	    "command", &command) != 0)
		return (EINVAL);

	mutex_enter(&spa_namespace_lock);
	spa = spa_by_guid(pool_guid, vdev_guid);
	if (spa != NULL)
		strcpy(name, spa_name(spa));
	mutex_exit(&spa_namespace_lock);
	if (spa == NULL)
		return (ENOENT);

	if ((error = spa_open(name, &spa, FTAG)) != 0)
		return (error);
	spa_vdev_state_enter(spa, SCL_ALL);
	vd = spa_lookup_by_guid(spa, vdev_guid, B_TRUE);
	if (vd == NULL) {
		(void) spa_vdev_state_exit(spa, NULL, ENXIO);
		spa_close(spa, FTAG);
		return (ENODEV);
	}
	error = vdev_label_write_pad2(vd, command, strlen(command));
	(void) spa_vdev_state_exit(spa, NULL, 0);
	txg_wait_synced(spa->spa_dsl_pool, 0);
	spa_close(spa, FTAG);
	return (error);
}

/* Update the VFS's cache of mountpoint properties */
void
zfs_ioctl_update_mount_cache(const char *dsname)
{
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(dsname, &zfsvfs) == 0) {
		struct mount *mp = zfsvfs->z_vfs;
		VFS_STATFS(mp, &mp->mnt_stat);
		zfs_vfs_rele(zfsvfs);
	}
	/*
	 * Ignore errors; we can't do anything useful if either getzfsvfs or
	 * VFS_STATFS fails.
	 */
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);

	return (ptob(vm_page_max_user_wired) / 4);
}

void
zfs_ioctl_init_os(void)
{
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_JAIL, zfs_ioc_jail,
	    zfs_secpolicy_config, POOL_CHECK_NONE);
	zfs_ioctl_register_dataset_nolog(ZFS_IOC_UNJAIL, zfs_ioc_unjail,
	    zfs_secpolicy_config, POOL_CHECK_NONE);
	zfs_ioctl_register("fbsd_nextboot", ZFS_IOC_NEXTBOOT,
	    zfs_ioc_nextboot, zfs_secpolicy_config, NO_NAME,
	    POOL_CHECK_NONE, B_FALSE, B_FALSE, zfs_keys_nextboot, 3);

}
