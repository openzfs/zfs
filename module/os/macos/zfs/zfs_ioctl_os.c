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

dev_t
zfsdev_get_dev(void)
{
	return ((dev_t)tsd_get(zfsdev_private_tsd));
}

/* We can't set ->private method, so this function does nothing */
void
zfsdev_private_set_state(void *priv, zfsdev_state_t *zs)
{
	zfsdev_state_t **actual_zs = (zfsdev_state_t **)priv;
	if (actual_zs != NULL)
		*actual_zs = zs;
}

/* Loop all zs looking for matching dev_t */
zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	dev_t dev = (dev_t)priv;
	zfsdev_state_t *zs;
	mutex_enter(&zfsdev_state_lock);
	zs = zfsdev_get_state(dev, ZST_ALL);
	mutex_exit(&zfsdev_state_lock);
	return (zs);
}

static int
zfsdev_open(dev_t dev, int flags, int devtype, struct proc *p)
{
	int error;
	zfsdev_state_t *actual_zs = NULL;

	mutex_enter(&zfsdev_state_lock);

	/*
	 * Check if the minor already exists, something that zfsdev_state_init()
	 * does internally, but it doesn't know of the minor we are to use.
	 * This should never happen, so use ASSERT()
	 */
	ASSERT3P(zfsdev_get_state(minor(dev), ZST_ALL), ==, NULL);

	error = zfsdev_state_init((void *)&actual_zs);
	/*
	 * We are given the minor to use, so we set it here. We can't use
	 * zfsdev_private_set_state() as it is called before zfsdev_state_init()
	 * sets the minor. Also, since zfsdev_state_init() doesn't return zs
	 * nor the minor they pick, we ab/use "priv" to return it to us.
	 * Maybe we should change zfsdev_state_init() instead of this dance,
	 * either to take 'minor' to use, or, to return zs.
	 */
	if (error == 0 && actual_zs != NULL)
		actual_zs->zs_minor = minor(dev);
	mutex_exit(&zfsdev_state_lock);

	/* Store this dev_t in tsd, so zfs_get_private() can retrieve it */
	tsd_set(zfsdev_private_tsd, (void *)(uintptr_t)dev);

	return (error);
}

static int
zfsdev_release(dev_t dev, int flags, int devtype, struct proc *p)
{
	/* zfsdev_state_destroy() doesn't check for NULL, so pre-lookup here */
	void *priv;

	priv = (void *)(uintptr_t)minor(dev);
	zfsdev_state_t *zs = zfsdev_private_get_state(priv);
	if (zs != NULL)
		zfsdev_state_destroy(priv);
	return (0);
}

/* !static : so we can dtrace */
int
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
zfs_secpolicy_os_none(zfs_cmd_t *zc, nvlist_t *innvl, cred_t *cr)
{
	return (0);
}

static const zfs_ioc_key_t zfs_keys_proxy_dataset[] = {
	{ZPOOL_CONFIG_POOL_NAME,	DATA_TYPE_STRING,	0},
};

static int
zfs_ioc_osx_proxy_dataset(const char *unused, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	int error;
	char *osname = NULL;
	char value[MAXPATHLEN * 2];

	if (nvlist_lookup_string(innvl,
	    ZPOOL_CONFIG_POOL_NAME, &osname) != 0)
		return (EINVAL);

	/* XXX Get osname */

	/* Create new virtual disk, and return /dev/disk name */
	error = zfs_osx_proxy_create(osname);

	if (error == 0)
		error = zfs_osx_proxy_get_bsdname(osname,
		    value, sizeof (value));

	if (error == 0) {

		fnvlist_add_string(outnvl, ZPOOL_CONFIG_POOL_NAME, osname);
		fnvlist_add_string(outnvl, ZPOOL_CONFIG_PATH, value);

		printf("%s: Created virtual disk '%s' for '%s'\n", __func__,
		    value, osname);
	}

	return (error);
}

static const zfs_ioc_key_t zfs_keys_proxy_remove[] = {
	{ZPOOL_CONFIG_POOL_NAME,	DATA_TYPE_STRING,	0},
};

static int
zfs_ioc_osx_proxy_remove(const char *unused, nvlist_t *innvl,
    nvlist_t *outnvl)
{
	char *osname = NULL;

	if (nvlist_lookup_string(innvl,
	    ZPOOL_CONFIG_POOL_NAME, &osname) != 0)
		return (EINVAL);

	zfs_osx_proxy_remove(osname);

	return (0);
}

void
zfs_ioctl_init_os(void)
{
	/* APPLE Specific ioctls */
	zfs_ioctl_register("proxy_dataset", ZFS_IOC_PROXY_DATASET,
	    zfs_ioc_osx_proxy_dataset, zfs_secpolicy_os_none,
	    NO_NAME, POOL_CHECK_NONE,
	    B_FALSE, B_FALSE, zfs_keys_proxy_dataset,
	    ARRAY_SIZE(zfs_keys_proxy_dataset));
	zfs_ioctl_register("proxy_remove", ZFS_IOC_PROXY_REMOVE,
	    zfs_ioc_osx_proxy_remove, zfs_secpolicy_config,
	    NO_NAME, POOL_CHECK_NONE,
	    B_FALSE, B_FALSE, zfs_keys_proxy_remove,
	    ARRAY_SIZE(zfs_keys_proxy_remove));
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

#ifdef ZFS_DEBUG
#define	ZFS_DEBUG_STR   " (DEBUG mode)"
#else
#define	ZFS_DEBUG_STR   ""
#endif

static int
openzfs_init_os(void)
{
	return (0);
}

static void
openzfs_fini_os(void)
{
}



/*
 * This is an identical copy of zfsdev_minor_alloc() except we check if
 * 'last_minor + 0' is available instead of 'last_minor + 1'. The latter
 * will cycle through minors unnecessarily, when it 'often' is available
 * again. Which gives us unattractive things like;
 * crw-rw-rw-  1 root  wheel   34, 0x0000213A May 31 14:42 /dev/zfs
 */
static minor_t
zfsdev_minor_alloc_os(void)
{
	static minor_t last_minor = 1;
	minor_t m;

	ASSERT(MUTEX_HELD(&zfsdev_state_lock));

	for (m = last_minor; m != last_minor - 1; m++) {
		if (m > ZFSDEV_MAX_MINOR)
			m = 1;
		if (zfsdev_get_state(m, ZST_ALL) == NULL) {
			last_minor = m;
			return (m);
		}
	}

	return (0);
}

/* Callback to create a unique minor for each open */
static int
zfs_devfs_clone(__unused dev_t dev, int action)
{
	static minor_t minorx;

	if (action == DEVFS_CLONE_ALLOC) {
		mutex_enter(&zfsdev_state_lock);
		minorx = zfsdev_minor_alloc_os();
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

	int err = 0;
	if ((err = zcommon_init()) != 0)
		goto zcommon_failed;
	if ((err = icp_init()) != 0)
		goto icp_failed;
	if ((err = zstd_init()) != 0)
		goto zstd_failed;
	if ((err = openzfs_init_os()) != 0)
		goto openzfs_os_failed;

	tsd_create(&zfsdev_private_tsd, NULL);

	sysctl_os_init();

	return (0);

openzfs_os_failed:
	zstd_fini();
zstd_failed:
	icp_fini();
icp_failed:
	zcommon_fini();
zcommon_failed:
	return (err);
}

void
zfsdev_detach(void)
{
	sysctl_os_fini();

	tsd_destroy(&zfsdev_private_tsd);

	openzfs_fini_os();
	zstd_fini();
	icp_fini();
	zcommon_fini();

	if (zfs_devnode) {
		devfs_remove(zfs_devnode);
		zfs_devnode = NULL;
	}
	if (zfs_major) {
		(void) cdevsw_remove(zfs_major, &zfs_cdevsw);
		zfs_major = 0;
	}
}

/* Update the VFS's cache of mountpoint properties */
void
zfs_ioctl_update_mount_cache(const char *dsname)
{
	zfsvfs_t *zfsvfs;

	if (getzfsvfs(dsname, &zfsvfs) == 0) {
		/* insert code here */
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

	return (KMALLOC_MAX_SIZE);
}
