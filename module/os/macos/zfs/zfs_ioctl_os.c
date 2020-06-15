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
 * Copyright (c) 2013, 2020 Jorgen Lundman <lundman@lundman.net>
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/conf.h>
#include <miscfs/devfs/devfs.h>
#include <sys/uio.h>
#include <sys/file.h>
#include <sys/kmem.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_vfsops.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_onexit.h>
#include <sys/zvol.h>
#include <sys/fm/util.h>
#include <sys/dsl_crypt.h>

#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_ioctl_compat.h>
#include <sys/zvol_os.h>
#include <sys/kstat_osx.h>

int zfs_major			= 0;
int zfs_bmajor			= 0;
static void *zfs_devnode 	= NULL;
#define	ZFS_MAJOR		-24

boolean_t
zfs_vfs_held(zfsvfs_t *zfsvfs)
{
	return (zfsvfs->z_vfs != NULL);
}

int
zfs_vfs_ref(zfsvfs_t **zfvp)
{
	int error = 0;

	if (*zfvp == NULL || (*zfvp)->z_vfs == NULL)
		return (SET_ERROR(ESRCH));

	error = vfs_busy((*zfvp)->z_vfs, LK_NOWAIT);
	if (error != 0) {
		*zfvp = NULL;
		error = SET_ERROR(ESRCH);
	}
	return (error);
}

void
zfs_vfs_rele(zfsvfs_t *zfsvfs)
{
	vfs_unbusy(zfsvfs->z_vfs);
}

static uint_t zfsdev_private_tsd;

static int
zfsdev_state_init(dev_t dev)
{
	zfsdev_state_t *zs, *zsprev = NULL;
	minor_t minor;
	boolean_t newzs = B_FALSE;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	minor = minor(dev);
	if (minor == 0)
		return (SET_ERROR(ENXIO));

	for (zs = zfsdev_state_list; zs != NULL; zs = zs->zs_next) {
		if (zs->zs_minor == -1)
			break;
		zsprev = zs;
	}

	if (!zs) {
		zs = kmem_zalloc(sizeof (zfsdev_state_t), KM_SLEEP);
		newzs = B_TRUE;
	}

	/* Store this dev_t in tsd, so zfs_get_private() can retrieve it */
	tsd_set(zfsdev_private_tsd, (void *)(uintptr_t)dev);

	zfs_onexit_init((zfs_onexit_t **)&zs->zs_onexit);
	zfs_zevent_init((zfs_zevent_t **)&zs->zs_zevent);

	/*
	 * In order to provide for lock-free concurrent read access
	 * to the minor list in zfsdev_get_state_impl(), new entries
	 * must be completely written before linking them into the
	 * list whereas existing entries are already linked; the last
	 * operation must be updating zs_minor (from -1 to the new
	 * value).
	 */
	if (newzs) {
		zs->zs_minor = minor;
		zsprev->zs_next = zs;
	} else {
		zs->zs_minor = minor;
	}

	return (0);
}

dev_t
zfsdev_get_dev(void)
{
	return ((dev_t)tsd_get(zfsdev_private_tsd));
}

static int
zfsdev_state_destroy(dev_t dev)
{
	zfsdev_state_t *zs;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	tsd_set(zfsdev_private_tsd, NULL);

	zs = zfsdev_get_state(minor(dev), ZST_ALL);

	if (!zs) {
		printf("%s: no cleanup for minor x%x\n", __func__,
		    minor(dev));
		return (0);
	}

	ASSERT(zs != NULL);
	if (zs->zs_minor != -1) {
		zs->zs_minor = -1;
		zfs_onexit_destroy(zs->zs_onexit);
		zfs_zevent_destroy(zs->zs_zevent);
		zs->zs_onexit = NULL;
		zs->zs_zevent = NULL;
	}
	return (0);
}

static int
zfsdev_open(dev_t dev, int flags, int devtype, struct proc *p)
{
	int error;

	mutex_enter(&zfsdev_state_lock);
	if (zfsdev_get_state(minor(dev), ZST_ALL)) {
		mutex_exit(&zfsdev_state_lock);
		return (0);
	}
	error = zfsdev_state_init(dev);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static int
zfsdev_release(dev_t dev, int flags, int devtype, struct proc *p)
{
	int error;

	mutex_enter(&zfsdev_state_lock);
	error = zfsdev_state_destroy(dev);
	mutex_exit(&zfsdev_state_lock);

	return (-error);
}

static int
zfsdev_ioctl(dev_t dev, ulong_t cmd, caddr_t arg,  __unused int xflag,
    struct proc *p)
{
	uint_t len, vecnum;
	zfs_iocparm_t *zit;
	zfs_cmd_t *zc;
	int error, rc;
	user_addr_t uaddr;

	/* Translate XNU ioctl to enum table: */
	len = IOCPARM_LEN(cmd);
	vecnum = cmd - _IOWR('Z', ZFS_IOC_FIRST, zfs_iocparm_t);
	zit = (void *)arg;
	uaddr = (user_addr_t)zit->zfs_cmd;

	if (len != sizeof (zfs_iocparm_t)) {
		/*
		 * printf("len %d vecnum: %d sizeof (zfs_cmd_t) %lu\n",
		 *  len, vecnum, sizeof (zfs_cmd_t));
		 */
		/*
		 * We can get plenty raw ioctl()s here, for exaple open() will
		 * cause spec_open() to issue DKIOCGETTHROTTLEMASK.
		 */
		return (EINVAL);
	}

	zc = kmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);

	if (copyin(uaddr, zc, sizeof (zfs_cmd_t))) {
		error = SET_ERROR(EFAULT);
		goto out;
	}

	error = zfsdev_ioctl_common(vecnum, zc, 0);

	rc = copyout(zc, uaddr, sizeof (*zc));

	if (error == 0 && rc != 0)
		error = -SET_ERROR(EFAULT);

	/*
	 * OSX must return(0) or XNU doesn't copyout(). Save the real
	 * rc to userland
	 */
	zit->zfs_ioc_error = error;
	error = 0;

out:
	kmem_free(zc, sizeof (zfs_cmd_t));
	return (error);

}

/* for spa_iokit_dataset_proxy_create */
#include <sys/ZFSDataset.h>
#include <sys/ZFSDatasetScheme.h>

static int
zfs_ioc_osx_proxy_dataset(zfs_cmd_t *zc)
{
	int error;
	const char *osname;

	/* XXX Get osname */
	osname = zc->zc_name;

	/* Create new virtual disk, and return /dev/disk name */
	error = zfs_osx_proxy_create(osname);

	if (error == 0)
		error = zfs_osx_proxy_get_bsdname(osname,
		    zc->zc_value, sizeof (zc->zc_value));
	if (error == 0)
		printf("%s: Created virtual disk '%s' for '%s'\n", __func__,
		    zc->zc_value, osname);

	return (error);
}

void
zfs_ioctl_init_os(void)
{
	/* APPLE Specific ioctls */
	zfs_ioctl_register_pool(ZFS_IOC_PROXY_DATASET,
	    zfs_ioc_osx_proxy_dataset, zfs_secpolicy_config,
	    B_FALSE, POOL_CHECK_NONE);
}

/* ioctl handler for block device. Relay to zvol */
static int
zfsdev_bioctl(dev_t dev, ulong_t cmd, caddr_t data,
    __unused int flag, struct proc *p)
{
	return (zvol_os_ioctl(dev, cmd, data, 1, NULL, NULL));
}

static struct bdevsw zfs_bdevsw = {
	.d_open		= zvol_os_open,
	.d_close	= zvol_os_close,
	.d_strategy	= zvol_os_strategy,
	.d_ioctl	= zfsdev_bioctl, /* block ioctl handler */
	.d_dump		= eno_dump,
	.d_psize	= zvol_os_get_volume_blocksize,
	.d_type		= D_DISK,
};

static struct cdevsw zfs_cdevsw = {
	.d_open		= zfsdev_open,
	.d_close	= zfsdev_release,
	.d_read		= zvol_os_read,
	.d_write	= zvol_os_write,
	.d_ioctl	= zfsdev_ioctl,
	.d_stop		= eno_stop,
	.d_reset	= eno_reset,
	.d_ttys		= NULL,
	.d_select	= eno_select,
	.d_mmap		= eno_mmap,
	.d_strategy	= eno_strat,
	.d_reserved_1	= eno_getc,
	.d_reserved_2	= eno_putc,
	.d_type		= D_DISK
};

/* Callback to create a unique minor for each open */
static int
zfs_devfs_clone(__unused dev_t dev, int action)
{
	static minor_t minorx;

	if (action == DEVFS_CLONE_ALLOC) {
		mutex_enter(&zfsdev_state_lock);
		minorx = zfsdev_minor_alloc();
		mutex_exit(&zfsdev_state_lock);
		return (minorx);
	}
	return (-1);
}

int
zfsdev_attach(void)
{
	dev_t dev;

	zfs_bmajor = bdevsw_add(-1, &zfs_bdevsw);
	zfs_major = cdevsw_add_with_bdev(-1, &zfs_cdevsw, zfs_bmajor);

	if (zfs_major < 0) {
		printf("ZFS: zfs_attach() failed to allocate a major number\n");
		return (-1);
	}

	dev = makedev(zfs_major, 0); /* Get the device number */
	zfs_devnode = devfs_make_node_clone(dev, DEVFS_CHAR, UID_ROOT,
	    GID_WHEEL, 0666, zfs_devfs_clone, "zfs", 0);
	if (!zfs_devnode) {
		printf("ZFS: devfs_make_node() failed\n");
		return (-1);
	}

	wrap_avl_init();
	wrap_unicode_init();
	wrap_nvpair_init();
	wrap_zcommon_init();
	wrap_icp_init();
	wrap_lua_init();

	tsd_create(&zfsdev_private_tsd, NULL);

	kstat_osx_init();
	return (0);
}

void
zfsdev_detach(void)
{
	kstat_osx_fini();

	tsd_destroy(&zfsdev_private_tsd);

	wrap_lua_fini();
	wrap_icp_fini();
	wrap_zcommon_fini();
	wrap_nvpair_fini();
	wrap_unicode_fini();
	wrap_avl_fini();

	if (zfs_devnode) {
		devfs_remove(zfs_devnode);
		zfs_devnode = NULL;
	}
	if (zfs_major) {
		(void) cdevsw_remove(zfs_major, &zfs_cdevsw);
		zfs_major = 0;
	}
}

uint64_t
zfs_max_nvlist_src_size_os(void)
{
	if (zfs_max_nvlist_src_size != 0)
		return (zfs_max_nvlist_src_size);

	return (KMALLOC_MAX_SIZE);
}
