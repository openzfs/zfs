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

#include <sys/param.h>
#include <sys/buf.h>
#include <sys/cmn_err.h>
#include <sys/conf.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_send.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_crypt.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_deleg.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_userhold.h>
#include <sys/errno.h>
#include <sys/eventhandler.h>
#include <sys/file.h>
#include <sys/fm/util.h>
#include <sys/fs/zfs.h>
#include <sys/kernel.h>
#include <sys/kmem.h>
#include <sys/lock.h>
#include <sys/malloc.h>
#include <sys/mount.h>
#include <sys/mutex.h>
#include <sys/nvpair.h>
#include <sys/policy.h>
#include <sys/proc.h>
#include <sys/sdt.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/stat.h>
#include <sys/sunddi.h>
#include <sys/sysctl.h>
#include <sys/systm.h>
#include <sys/taskqueue.h>
#include <sys/uio.h>
#include <sys/vdev.h>
#include <sys/vdev_removal.h>
#include <sys/zap.h>
#include <sys/zcp.h>
#include <sys/zfeature.h>
#include <sys/zfs_context.h>
#include <sys/zfs_ctldir.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_compat.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_onexit.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_znode.h>
#include <sys/zio_checksum.h>
#include <sys/zone.h>
#include <sys/zvol.h>

#include "zfs_comutil.h"
#include "zfs_deleg.h"
#include "zfs_namecheck.h"
#include "zfs_prop.h"

SYSCTL_DECL(_vfs_zfs);
SYSCTL_DECL(_vfs_zfs_vdev);

extern uint_t rrw_tsd_key;
static int zfs_version_ioctl = ZFS_IOCVER_OZFS;
SYSCTL_DECL(_vfs_zfs_version);
SYSCTL_INT(_vfs_zfs_version, OID_AUTO, ioctl, CTLFLAG_RD, &zfs_version_ioctl,
    0, "ZFS_IOCTL_VERSION");

static struct cdev *zfsdev;

static struct root_hold_token *zfs_root_token;

extern uint_t rrw_tsd_key;
extern uint_t zfs_allow_log_key;
extern uint_t zfs_geom_probe_vdev_key;

static int zfs__init(void);
static int zfs__fini(void);
static void zfs_shutdown(void *, int);

static eventhandler_tag zfs_shutdown_event_tag;
static eventhandler_tag zfs_mountroot_event_tag;

#define	ZFS_MIN_KSTACK_PAGES 4

static int
zfsdev_ioctl(struct cdev *dev, ulong_t zcmd, caddr_t arg, int flag,
    struct thread *td)
{
	uint_t len;
	int vecnum;
	zfs_iocparm_t *zp;
	zfs_cmd_t *zc;
#ifdef ZFS_LEGACY_SUPPORT
	zfs_cmd_legacy_t *zcl;
#endif
	int rc, error;
	void *uaddr;

	len = IOCPARM_LEN(zcmd);
	vecnum = zcmd & 0xff;
	zp = (void *)arg;
	error = 0;
#ifdef ZFS_LEGACY_SUPPORT
	zcl = NULL;
#endif

	if (len != sizeof (zfs_iocparm_t))
		return (EINVAL);

	uaddr = (void *)(uintptr_t)zp->zfs_cmd;
	zc = vmem_zalloc(sizeof (zfs_cmd_t), KM_SLEEP);
#ifdef ZFS_LEGACY_SUPPORT
	/*
	 * Remap ioctl code for legacy user binaries
	 */
	if (zp->zfs_ioctl_version == ZFS_IOCVER_LEGACY) {
		vecnum = zfs_ioctl_legacy_to_ozfs(vecnum);
		if (vecnum < 0) {
			vmem_free(zc, sizeof (zfs_cmd_t));
			return (ENOTSUP);
		}
		zcl = vmem_zalloc(sizeof (zfs_cmd_legacy_t), KM_SLEEP);
		if (copyin(uaddr, zcl, sizeof (zfs_cmd_legacy_t))) {
			error = SET_ERROR(EFAULT);
			goto out;
		}
		zfs_cmd_legacy_to_ozfs(zcl, zc);
	} else
#endif
	if (copyin(uaddr, zc, sizeof (zfs_cmd_t))) {
		error = SET_ERROR(EFAULT);
		goto out;
	}
	error = zfsdev_ioctl_common(vecnum, zc, 0);
#ifdef ZFS_LEGACY_SUPPORT
	if (zcl) {
		zfs_cmd_ozfs_to_legacy(zc, zcl);
		rc = copyout(zcl, uaddr, sizeof (*zcl));
	} else
#endif
	{
		rc = copyout(zc, uaddr, sizeof (*zc));
	}
	if (error == 0 && rc != 0)
		error = SET_ERROR(EFAULT);
out:
#ifdef ZFS_LEGACY_SUPPORT
	if (zcl)
		vmem_free(zcl, sizeof (zfs_cmd_legacy_t));
#endif
	vmem_free(zc, sizeof (zfs_cmd_t));
	MPASS(tsd_get(rrw_tsd_key) == NULL);
	return (error);
}

static void
zfsdev_close(void *data)
{
	zfsdev_state_destroy(data);
}

void
zfsdev_private_set_state(void *priv __unused, zfsdev_state_t *zs)
{
	devfs_set_cdevpriv(zs, zfsdev_close);
}

zfsdev_state_t *
zfsdev_private_get_state(void *priv)
{
	return (priv);
}

static int
zfsdev_open(struct cdev *devp __unused, int flag __unused, int mode __unused,
    struct thread *td __unused)
{
	int error;

	mutex_enter(&zfsdev_state_lock);
	error = zfsdev_state_init(NULL);
	mutex_exit(&zfsdev_state_lock);

	return (error);
}

static struct cdevsw zfs_cdevsw = {
	.d_version =	D_VERSION,
	.d_open =	zfsdev_open,
	.d_ioctl =	zfsdev_ioctl,
	.d_name =	ZFS_DRIVER
};

int
zfsdev_attach(void)
{
	struct make_dev_args args;

	make_dev_args_init(&args);
	args.mda_flags = MAKEDEV_CHECKNAME | MAKEDEV_WAITOK;
	args.mda_devsw = &zfs_cdevsw;
	args.mda_cr = NULL;
	args.mda_uid = UID_ROOT;
	args.mda_gid = GID_OPERATOR;
	args.mda_mode = 0666;
	return (make_dev_s(&args, &zfsdev, ZFS_DRIVER));
}

void
zfsdev_detach(void)
{
	if (zfsdev != NULL)
		destroy_dev(zfsdev);
}

int
zfs__init(void)
{
	int error;

#if KSTACK_PAGES < ZFS_MIN_KSTACK_PAGES
	printf("ZFS NOTICE: KSTACK_PAGES is %d which could result in stack "
	    "overflow panic!\nPlease consider adding "
	    "'options KSTACK_PAGES=%d' to your kernel config\n", KSTACK_PAGES,
	    ZFS_MIN_KSTACK_PAGES);
#endif
	zfs_root_token = root_mount_hold("ZFS");
	if ((error = zfs_kmod_init()) != 0) {
		printf("ZFS: Failed to Load ZFS Filesystem"
		    ", rc = %d\n", error);
		root_mount_rel(zfs_root_token);
		return (error);
	}


	tsd_create(&zfs_geom_probe_vdev_key, NULL);

	printf("ZFS storage pool version: features support ("
	    SPA_VERSION_STRING ")\n");
	root_mount_rel(zfs_root_token);
	ddi_sysevent_init();
	return (0);
}

int
zfs__fini(void)
{
	if (zfs_busy() || zvol_busy() ||
	    zio_injection_enabled) {
		return (EBUSY);
	}
	zfs_kmod_fini();
	tsd_destroy(&zfs_geom_probe_vdev_key);
	return (0);
}

static void
zfs_shutdown(void *arg __unused, int howto __unused)
{

	/*
	 * ZFS fini routines can not properly work in a panic-ed system.
	 */
	if (panicstr == NULL)
		zfs__fini();
}

static int
zfs_modevent(module_t mod, int type, void *unused __unused)
{
	int err;

	switch (type) {
	case MOD_LOAD:
		err = zfs__init();
		if (err == 0) {
			zfs_shutdown_event_tag = EVENTHANDLER_REGISTER(
			    shutdown_post_sync, zfs_shutdown, NULL,
			    SHUTDOWN_PRI_FIRST);
			zfs_mountroot_event_tag = EVENTHANDLER_REGISTER(
			    mountroot, spa_boot_init, NULL,
			    SI_ORDER_ANY);
		}
		return (err);
	case MOD_UNLOAD:
		err = zfs__fini();
		if (err == 0) {
			if (zfs_shutdown_event_tag != NULL)
				EVENTHANDLER_DEREGISTER(shutdown_post_sync,
				    zfs_shutdown_event_tag);
			if (zfs_mountroot_event_tag != NULL)
				EVENTHANDLER_DEREGISTER(mountroot,
				    zfs_mountroot_event_tag);
		}
		return (err);
	case MOD_SHUTDOWN:
		return (0);
	default:
		break;
	}
	return (EOPNOTSUPP);
}

static moduledata_t zfs_mod = {
	"zfsctrl",
	zfs_modevent,
	0
};


FEATURE(zfs, "OpenZFS support");

DECLARE_MODULE(zfsctrl, zfs_mod, SI_SUB_CLOCKS, SI_ORDER_ANY);
MODULE_VERSION(zfsctrl, 1);
MODULE_DEPEND(zfsctrl, xdr, 1, 1, 1);
MODULE_DEPEND(zfsctrl, acl_nfs4, 1, 1, 1);
MODULE_DEPEND(zfsctrl, crypto, 1, 1, 1);
MODULE_DEPEND(zfsctrl, zlib, 1, 1, 1);
