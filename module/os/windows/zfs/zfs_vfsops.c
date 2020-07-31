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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Pawel Jakub Dawidek <pawel@dawidek.net>.
 * Copyright (c) 2012, 2015 by Delphix. All rights reserved.
 * Copyright 2016 Nexenta Systems, Inc. All rights reserved.
 */

/* Portions Copyright 2010 Robert Milkowski */
/* Portions Copyright 2013 Jorgen Lundman */

#include <sys/types.h>

#ifndef _WIN32
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/sysmacros.h>
#include <sys/kmem.h>
#include <sys/pathname.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/vfs_opreg.h>
#include <sys/mntent.h>
#include <sys/mount.h>
#include <sys/cmn_err.h>
#include "fs/fs_subr.h"
#include <sys/zfs_znode.h>
#endif /* !_WIN32 */

#include <sys/zfs_dir.h>

#ifndef _WIN32
#include <sys/zil.h>
#include <sys/fs/zfs.h>
#include <sys/dmu.h>
#endif /* !_WIN32 */

#include <sys/policy.h>

#include <sys/dsl_prop.h>
#include <sys/dsl_dataset.h>

#ifndef _WIN32
#include <sys/dsl_deleg.h>
#include <sys/spa.h>
#endif /* !_WIN32 */

#include <sys/zap.h>

#include <sys/sa.h>
#include <sys/sa_impl.h>
#ifndef _WIN32
#include <sys/varargs.h>
#include <sys/policy.h>
#include <sys/atomic.h>
#include <sys/mkdev.h>
#include <sys/modctl.h>
#include <sys/refstr.h>
#include <sys/zfs_ioctl.h>
#endif /* !_WIN32 */

#include <sys/zfs_ctldir.h>

#ifndef _WIN32
#include <sys/zfs_fuid.h>
#include <sys/bootconf.h>
#include <sys/sunddi.h>
#include <sys/dnlc.h>
#endif /* !_WIN32 */

#include <sys/dmu_objset.h>

#ifndef _WIN32
#include <sys/spa_boot.h>
#endif /* !_WIN32 */

#ifdef __LINUX__
#include <sys/zpl.h>
#endif /* __LINUX__ */

#include "zfs_comutil.h"

#ifdef _WIN32
#include <sys/zfs_vnops.h>
#include <sys/systeminfo.h>
#include <sys/zfs_mount.h>
#endif /* _WIN32 */

//#define dprintf kprintf
//#define dprintf printf
 
#ifdef _WIN32
unsigned int zfs_vnop_skip_unlinked_drain = 0;

//int  zfs_module_start(kmod_info_t *ki, void *data);
//int  zfs_module_stop(kmod_info_t *ki, void *data);
extern int getzfsvfs(const char *dsname, zfsvfs_t **zfvp);


/*
 * AVL tree of hardlink entries, which we need to map for Finder. The va_linkid
 * needs to be unique for each hardlink target, as well as, return the znode
 * in vget(va_linkid). Unfortunately, the va_linkid is 32bit (lost in the
 * syscall translation to userland struct). We sort the AVL tree by
 * -> directory id
 *       -> z_id
 *              -> name
 *
 */
static int hardlinks_compare(const void *arg1, const void *arg2)
{
	const hardlinks_t *node1 = arg1;
	const hardlinks_t *node2 = arg2;
	int value;
	if (node1->hl_parent > node2->hl_parent)
		return 1;
	if (node1->hl_parent < node2->hl_parent)
		return -1;
	if (node1->hl_fileid > node2->hl_fileid)
		return 1;
	if (node1->hl_fileid < node2->hl_fileid)
		return -1;

	value = strncmp(node1->hl_name, node2->hl_name, PATH_MAX);
	if (value < 0) return -1;
	if (value > 0) return 1;
	return 0;
}

/*
 * Lookup same information from linkid, to get at parentid, objid and name
 */
static int hardlinks_compare_linkid(const void *arg1, const void *arg2)
{
	const hardlinks_t *node1 = arg1;
	const hardlinks_t *node2 = arg2;
	if (node1->hl_linkid > node2->hl_linkid)
		return 1;
	if (node1->hl_linkid < node2->hl_linkid)
		return -1;
	return 0;
}




/*
 * Mac OS X needs a file system modify time
 *
 * We use the mtime of the "com.apple.system.mtime"
 * extended attribute, which is associated with the
 * file system root directory.  This attribute has
 * no associated data.
 */
#define ZFS_MTIME_XATTR		"com.apple.system.mtime"

extern int zfs_obtain_xattr(znode_t *, const char *, mode_t, cred_t *, vnode_t **, int);


/*
 * We need to keep a count of active fs's.
 * This is necessary to prevent our kext
 * from being unloaded after a umount -f
 */
uint32_t	zfs_active_fs_count = 0;

extern void zfs_ioctl_init(void);
extern void zfs_ioctl_fini(void);

#endif

static int
zfsvfs_parse_option(char *option, char *value, vfs_t *vfsp)
{
	if (!option || !*option) return 0;
	dprintf("parse '%s' '%s'\n", option?option:"",
		value?value:"");
	if (!strcasecmp(option, "readonly")) {
		if (value && *value &&
			!strcasecmp(value, "off"))
			vfs_clearflags(vfsp, (uint64_t)MNT_RDONLY);
		else
			vfs_setflags(vfsp, (uint64_t)MNT_RDONLY);
	}
	return 0;
}

/*
 * Parse the raw mntopts and return a vfs_t describing the options.
 */
static int
zfsvfs_parse_options(char *mntopts, vfs_t *vfsp)
{
	int error = 0;

	if (mntopts != NULL) {
		char *p, *t, *v;
		char *keep;

		int len = strlen(mntopts) + 1;
		keep = kmem_alloc(len, KM_SLEEP);
		t = keep;
		memcpy(t, mntopts, len);

		while(1) {
			while (t && *t == ' ') t++;

			p = strpbrk(t, ",");
			if (p) *p = 0;

			// find "="
			v = strpbrk(t, "=");
			if (v) {
				*v = 0;
				v++;
				while (*v == ' ') v++;
			}
			error = zfsvfs_parse_option(t, v, vfsp);
			if (error) break;
			if (!p) break;
			t = &p[1];
		}
		kmem_free(keep, len);
	}

	return (error);
}


/* The OS sync ignored by default, as ZFS handles internal periodic
 * syncs. (As per illumos) Unfortunately, we can not tell the difference
 * of when users run "sync" by hand. Sync is called on umount though.
 */
uint64_t zfs_vfs_sync_paranoia = 0;

int
zfs_vfs_sync(struct mount *vfsp,  int waitfor,  vfs_context_t *context)
{
    /*
     * Data integrity is job one. We don't want a compromised kernel
     * writing to the storage pool, so we never sync during panic.
     */
    if (spl_panicstr())
        return (0);

	/* Check if sysctl setting wants sync - and we are not unmounting */
	if (zfs_vfs_sync_paranoia == 0 &&
		!vfs_isunmount(vfsp))
		return (0);

    if (vfsp != NULL) {
        /*
         * Sync a specific filesystem.
         */
#if 1
        zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);
        dsl_pool_t *dp;

        ZFS_ENTER(zfsvfs);
        dp = dmu_objset_pool(zfsvfs->z_os);

        /*
         * If the system is shutting down, then skip any
         * filesystems which may exist on a suspended pool.
         */
        if (spl_system_inshutdown() && spa_suspended(dp->dp_spa)) {
            ZFS_EXIT(zfsvfs);
            return (0);
        }

        if (zfsvfs->z_log != NULL)
            zil_commit(zfsvfs->z_log, 0);

        ZFS_EXIT(zfsvfs);

#endif
    } else {
#if 1
        /*
         * Sync all ZFS filesystems. This is what happens when you
         * run sync(1M). Unlike other filesystems, ZFS honors the
         * request by waiting for all pools to commit all dirty data.
         */
        spa_sync_allpools();
#endif
    }

    return (0);

}



#ifndef _WIN32
static int
zfs_create_unique_device(dev_t *dev)
{
	major_t new_major;

	do {
		ASSERT3U(zfs_minor, <=, MAXMIN32);
		minor_t start = zfs_minor;
		do {
			mutex_enter(&zfs_dev_mtx);
			if (zfs_minor >= MAXMIN32) {
				/*
				 * If we're still using the real major
				 * keep out of /dev/zfs and /dev/zvol minor
				 * number space.  If we're using a getudev()'ed
				 * major number, we can use all of its minors.
				 */
				if (zfs_major == ddi_name_to_major(ZFS_DRIVER))
					zfs_minor = ZFS_MIN_MINOR;
				else
					zfs_minor = 0;
			} else {
				zfs_minor++;
			}
			*dev = makedevice(zfs_major, zfs_minor);
			mutex_exit(&zfs_dev_mtx);
		} while (vfs_devismounted(*dev) && zfs_minor != start);
		if (zfs_minor == start) {
			/*
			 * We are using all ~262,000 minor numbers for the
			 * current major number.  Create a new major number.
			 */
			if ((new_major = getudev()) == (major_t)-1) {
				cmn_err(CE_WARN,
				    "zfs_mount: Can't get unique major "
				    "device number.");
				return (-1);
			}
			mutex_enter(&zfs_dev_mtx);
			zfs_major = new_major;
			zfs_minor = 0;

			mutex_exit(&zfs_dev_mtx);
		} else {
			break;
		}
		/* CONSTANTCONDITION */
	} while (1);

	return (0);
}
#endif	/* !__FreeBSD__ */

static void
atime_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	if (newval == B_TRUE) {
		zfsvfs->z_atime = B_TRUE;
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOATIME);
	} else {
		zfsvfs->z_atime = B_FALSE;
        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOATIME);
	}
}

#ifdef LINUX
static void
relatime_changed_cb(void *arg, uint64_t newval)
{
	((zfs_sb_t *)arg)->z_relatime = newval;
}
#endif

static void
xattr_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	/*
	 * Apple does have MNT_NOUSERXATTR mount option, but unfortunately the VFS
	 * layer returns EACCESS if xattr access is attempted. Finder etc, will
	 * do so, even if filesystem capabilities is set without xattr, rendering
	 * the mount option useless. We no longer set it, and handle xattrs being
	 * disabled internally.
	 */

	if (newval == ZFS_XATTR_OFF) {
		zfsvfs->z_xattr = B_FALSE;
		//vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOUSERXATTR);
	} else {
		zfsvfs->z_xattr = B_TRUE;
		//vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOUSERXATTR);

		if (newval == ZFS_XATTR_SA)
			zfsvfs->z_xattr_sa = B_TRUE;
		else
			zfsvfs->z_xattr_sa = B_FALSE;
	}
}

#if 0 // unused function
static void
acltype_changed_cb(void *arg, uint64_t newval)
{
#ifdef LINUX
	switch (newval) {
	case ZFS_ACLTYPE_OFF:
		zsb->z_acl_type = ZFS_ACLTYPE_OFF;
		zsb->z_sb->s_flags &= ~MS_POSIXACL;
		break;
	case ZFS_ACLTYPE_POSIXACL:
#ifdef CONFIG_FS_POSIX_ACL
		zsb->z_acl_type = ZFS_ACLTYPE_POSIXACL;
		zsb->z_sb->s_flags |= MS_POSIXACL;
#else
		zsb->z_acl_type = ZFS_ACLTYPE_OFF;
		zsb->z_sb->s_flags &= ~MS_POSIXACL;
#endif /* CONFIG_FS_POSIX_ACL */
		break;
	default:
		break;
	}
#endif
}
#endif

static void
blksz_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	ASSERT3U(newval, <=, spa_maxblocksize(dmu_objset_spa(zfsvfs->z_os)));
	ASSERT3U(newval, >=, SPA_MINBLOCKSIZE);
	ASSERT(ISP2(newval));

	zfsvfs->z_max_blksz = newval;
	//zfsvfs->z_vfs->mnt_stat.f_iosize = newval;
}

static void
readonly_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_TRUE) {
		/* XXX locking on vfs_flag? */

        // We need to release the mtime_vp when readonly, as it will not
        // call VNOP_SYNC in RDONLY.

#if 0
        if (zfsvfs->z_mtime_vp) {
            vnode_rele(zfsvfs->z_mtime_vp);
            vnode_recycle(zfsvfs->z_mtime_vp);
            zfsvfs->z_mtime_vp = NULL;
        }
#endif
        // Flush any writes
        //vflush(mp, NULLVP, SKIPSYSTEM);

        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_RDONLY);
		zfsvfs->z_rdonly = 1;
	} else {
        // FIXME, we don't re-open mtime_vp here.
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_RDONLY);
		zfsvfs->z_rdonly = 0;
	}
}

static void
devices_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NODEV);
	} else {
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NODEV);
	}
}

static void
setuid_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOSUID);
	} else {
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOSUID);
	}
}

static void
exec_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_NOEXEC);
	} else {
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_NOEXEC);
	}
}

/*
 * The nbmand mount option can be changed at mount time.
 * We can't allow it to be toggled on live file systems or incorrect
 * behavior may be seen from cifs clients
 *
 * This property isn't registered via dsl_prop_register(), but this callback
 * will be called when a file system is first mounted
 */
#if 0 // unused function
static void
nbmand_changed_cb(void *arg, uint64_t newval)
{
#if 0
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NBMAND);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NONBMAND, NULL, 0);
	} else {
		vfs_clearmntopt(zfsvfs->z_vfs, MNTOPT_NONBMAND);
		vfs_setmntopt(zfsvfs->z_vfs, MNTOPT_NBMAND, NULL, 0);
	}
#endif
}
#endif

static void
snapdir_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	zfsvfs->z_show_ctldir = newval;
    dnlc_purge_vfsp(zfsvfs->z_vfs, 0);
}

static void
vscan_changed_cb(void *arg, uint64_t newval)
{
	//zfsvfs_t *zfsvfs = arg;

	//zfsvfs->z_vscan = newval;
}

static void
acl_mode_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_mode = newval;
}

static void
acl_inherit_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;

	zfsvfs->z_acl_inherit = newval;
}

#ifdef _WIN32
static void
finderbrowse_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_DONTBROWSE);
	} else {
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_DONTBROWSE);
	}
}
static void
ignoreowner_changed_cb(void *arg, uint64_t newval)
{
	zfsvfs_t *zfsvfs = arg;
	if (newval == B_FALSE) {
        vfs_clearflags(zfsvfs->z_vfs, (uint64_t)MNT_IGNORE_OWNERSHIP);
	} else {
        vfs_setflags(zfsvfs->z_vfs, (uint64_t)MNT_IGNORE_OWNERSHIP);
	}
}

static void
mimic_hfs_changed_cb(void *arg, uint64_t newval)
{
	// FIXME - what do we do in here?
	zfsvfs_t *zfsvfs = arg;
	struct vfsstatfs *vfsstatfs;
	vfsstatfs = vfs_statfs(zfsvfs->z_vfs);

	if(newval == 0) {
	    strlcpy(vfsstatfs->f_fstypename, "zfs", MFSTYPENAMELEN);
	} else {
	    strlcpy(vfsstatfs->f_fstypename, "hfs", MFSTYPENAMELEN);
	}
}

#endif

static int
zfs_register_callbacks(struct mount *vfsp)
{
	struct dsl_dataset *ds = NULL;

	objset_t *os = NULL;
	zfsvfs_t *zfsvfs = NULL;
	boolean_t readonly = B_FALSE;
	boolean_t do_readonly = B_FALSE;
	boolean_t setuid = B_FALSE;
	boolean_t do_setuid = B_FALSE;
	boolean_t exec = B_FALSE;
	boolean_t do_exec = B_FALSE;
	boolean_t devices = B_FALSE;
	boolean_t do_devices = B_FALSE;
	boolean_t xattr = B_FALSE;
	boolean_t do_xattr = B_FALSE;
	boolean_t atime = B_FALSE;
	boolean_t do_atime = B_FALSE;
	boolean_t finderbrowse = B_FALSE;
	boolean_t do_finderbrowse = B_FALSE;
	boolean_t ignoreowner = B_FALSE;
	boolean_t do_ignoreowner = B_FALSE;
	int error = 0;

	ASSERT(vfsp);
    zfsvfs = vfs_fsprivate(vfsp);
	ASSERT(zfsvfs);
	os = zfsvfs->z_os;

	/*
	 * This function can be called for a snapshot when we update snapshot's
	 * mount point, which isn't really supported.
	 */
	if (dmu_objset_is_snapshot(os))
		return (EOPNOTSUPP);

	/*
	 * The act of registering our callbacks will destroy any mount
	 * options we may have.  In order to enable temporary overrides
	 * of mount options, we stash away the current values and
	 * restore them after we register the callbacks.
	 */
#define vfs_optionisset(X, Y, Z) (vfs_flags(X)&(Y))

	if (vfs_optionisset(vfsp, MNT_RDONLY, NULL) ||
	    !spa_writeable(dmu_objset_spa(os))) {
		readonly = B_TRUE;
		do_readonly = B_TRUE;
#ifndef _WIN32
		/* Apple has no option to pass RW to mount, ie
		 * zfs set readonly=on D ; zfs mount -o rw D
		 */
	} else if (vfs_optionisset(vfsp, MNTOPT_RW, NULL)) {
		readonly = B_FALSE;
		do_readonly = B_TRUE;
#endif
	}
	if (vfs_optionisset(vfsp, MNT_NODEV, NULL)) {
		devices = B_FALSE;
		do_devices = B_TRUE;
#ifndef _WIN32
	} else {
        devices = B_TRUE;
        do_devices = B_TRUE;
#endif
    }
	/* xnu SETUID, not IllumOS SUID */
	if (vfs_optionisset(vfsp, MNT_NOSUID, NULL)) {
		setuid = B_FALSE;
		do_setuid = B_TRUE;
#ifndef _WIN32
	} else {
        setuid = B_TRUE;
        do_setuid = B_TRUE;
#endif
    }
	if (vfs_optionisset(vfsp, MNT_NOEXEC, NULL)) {
		exec = B_FALSE;
		do_exec = B_TRUE;
#ifndef _WIN32
	} else {
		exec = B_TRUE;
		do_exec = B_TRUE;
#endif
	}
	if (vfs_optionisset(vfsp, MNT_NOUSERXATTR, NULL)) {
		xattr = B_FALSE;
		do_xattr = B_TRUE;
#ifndef _WIN32
	} else {
		xattr = B_TRUE;
		do_xattr = B_TRUE;
#endif
	}
	if (vfs_optionisset(vfsp, MNT_NOATIME, NULL)) {
		atime = B_FALSE;
		do_atime = B_TRUE;
#ifndef _WIN32
	} else {
		atime = B_TRUE;
		do_atime = B_TRUE;
#endif
	}
	if (vfs_optionisset(vfsp, MNT_DONTBROWSE, NULL)) {
		finderbrowse = B_FALSE;
		do_finderbrowse = B_TRUE;
#ifndef _WIN32
	} else {
		finderbrowse = B_TRUE;
		do_finderbrowse = B_TRUE;
#endif
	}
	if (vfs_optionisset(vfsp, MNT_IGNORE_OWNERSHIP, NULL)) {
		ignoreowner = B_TRUE;
		do_ignoreowner = B_TRUE;
#ifndef _WIN32
	} else {
		ignoreowner = B_FALSE;
		do_ignoreowner = B_TRUE;
#endif
	}

	/*
	 * nbmand is a special property.  It can only be changed at
	 * mount time.
	 *
	 * This is weird, but it is documented to only be changeable
	 * at mount time.
	 */
#ifdef __LINUX__
	uint64_t nbmand = 0;

	if (vfs_optionisset(vfsp, MNTOPT_NONBMAND, NULL)) {
		nbmand = B_FALSE;
	} else if (vfs_optionisset(vfsp, MNTOPT_NBMAND, NULL)) {
		nbmand = B_TRUE;
	} else {
		char osname[ZFS_MAX_DATASET_NAME_LEN];

		dmu_objset_name(os, osname);
		if (error = dsl_prop_get_integer(osname, "nbmand", &nbmand,
		    NULL)) {
			return (error);
		}
	}
#endif

	/*
	 * Register property callbacks.
	 *
	 * It would probably be fine to just check for i/o error from
	 * the first prop_register(), but I guess I like to go
	 * overboard...
	 */
	ds = dmu_objset_ds(os);
	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	error = dsl_prop_register(ds,

	    zfs_prop_to_name(ZFS_PROP_ATIME), atime_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_XATTR), xattr_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_RECORDSIZE), blksz_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_READONLY), readonly_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_DEVICES), devices_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SETUID), setuid_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_EXEC), exec_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_SNAPDIR), snapdir_changed_cb, zfsvfs);
    // This appears to be PROP_PRIVATE, investigate if we want this
    // ZOL calls this ACLTYPE
	error = error ? error : dsl_prop_register(ds,
        zfs_prop_to_name(ZFS_PROP_ACLMODE), acl_mode_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_ACLINHERIT), acl_inherit_changed_cb,
	    zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_VSCAN), vscan_changed_cb, zfsvfs);
#ifdef _APPLE_
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_APPLE_BROWSE), finderbrowse_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_APPLE_IGNOREOWNER), ignoreowner_changed_cb, zfsvfs);
	error = error ? error : dsl_prop_register(ds,
	    zfs_prop_to_name(ZFS_PROP_APPLE_MIMIC_HFS), mimic_hfs_changed_cb, zfsvfs);
#endif
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);
	if (error)
		goto unregister;

	/*
	 * Invoke our callbacks to restore temporary mount options.
	 */
	if (do_readonly)
		readonly_changed_cb(zfsvfs, readonly);
	if (do_setuid)
		setuid_changed_cb(zfsvfs, setuid);
	if (do_exec)
		exec_changed_cb(zfsvfs, exec);
	if (do_devices)
		devices_changed_cb(zfsvfs, devices);
	if (do_xattr)
		xattr_changed_cb(zfsvfs, xattr);
	if (do_atime)
		atime_changed_cb(zfsvfs, atime);
#ifdef _WIN32
	if (do_finderbrowse)
		finderbrowse_changed_cb(zfsvfs, finderbrowse);
	if (do_ignoreowner)
		ignoreowner_changed_cb(zfsvfs, ignoreowner);
#endif
#ifndef _WIN32

	nbmand_changed_cb(zfsvfs, nbmand);
#endif

	return (0);

unregister:
	dsl_prop_unregister_all(ds, zfsvfs);
	return (error);
}

static int
zfs_space_delta_cb(dmu_object_type_t bonustype, void *data,
    uint64_t *userp, uint64_t *groupp)
{
	//int error = 0;

	/*
	 * Is it a valid type of object to track?
	 */
	if (bonustype != DMU_OT_ZNODE && bonustype != DMU_OT_SA)
		return (SET_ERROR(ENOENT));

	/*
	 * If we have a NULL data pointer
	 * then assume the id's aren't changing and
	 * return EEXIST to the dmu to let it know to
	 * use the same ids
	 */
	if (data == NULL)
		return (SET_ERROR(EEXIST));

	if (bonustype == DMU_OT_ZNODE) {
		znode_phys_t *znp = data;
		*userp = znp->zp_uid;
		*groupp = znp->zp_gid;
	} else {
#if 1
		int hdrsize;
		sa_hdr_phys_t *sap = data;
		sa_hdr_phys_t sa = *sap;
		boolean_t swap = B_FALSE;

		ASSERT(bonustype == DMU_OT_SA);

		if (sa.sa_magic == 0) {
			/*
			 * This should only happen for newly created
			 * files that haven't had the znode data filled
			 * in yet.
			 */
			*userp = 0;
			*groupp = 0;
			return (0);
		}
		if (sa.sa_magic == BSWAP_32(SA_MAGIC)) {
			sa.sa_magic = SA_MAGIC;
			sa.sa_layout_info = BSWAP_16(sa.sa_layout_info);
			swap = B_TRUE;
		} else {
			if (sa.sa_magic != SA_MAGIC) {
				dprintf("ZFS: sa.sa_magic %x is not SA_MAGIC\n",
					sa.sa_magic);
				return -1;
			}
			VERIFY3U(sa.sa_magic, ==, SA_MAGIC);
		}

		hdrsize = sa_hdrsize(&sa);
		VERIFY3U(hdrsize, >=, sizeof (sa_hdr_phys_t));
		*userp = *((uint64_t *)((uintptr_t)data + hdrsize +
		    SA_UID_OFFSET));
		*groupp = *((uint64_t *)((uintptr_t)data + hdrsize +
		    SA_GID_OFFSET));
		if (swap) {
			*userp = BSWAP_64(*userp);
			*groupp = BSWAP_64(*groupp);
		}
#endif
	}
	return (0);
}

static void
fuidstr_to_sid(zfsvfs_t *zfsvfs, const char *fuidstr,
    char *domainbuf, int buflen, uid_t *ridp)
{
	uint64_t fuid;
	const char *domain;

	fuid = zfs_strtonum(fuidstr, NULL);

	domain = zfs_fuid_find_by_idx(zfsvfs, FUID_INDEX(fuid));
	if (domain)
		(void) strlcpy(domainbuf, domain, buflen);
	else
		domainbuf[0] = '\0';
	*ridp = FUID_RID(fuid);
}

static uint64_t
zfs_userquota_prop_to_obj(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type)
{
	switch (type) {
	case ZFS_PROP_USERUSED:
		return (DMU_USERUSED_OBJECT);
	case ZFS_PROP_GROUPUSED:
		return (DMU_GROUPUSED_OBJECT);
	case ZFS_PROP_USERQUOTA:
		return (zfsvfs->z_userquota_obj);
	case ZFS_PROP_GROUPQUOTA:
		return (zfsvfs->z_groupquota_obj);
    default:
		return (SET_ERROR(ENOTSUP));
        break;
	}
	return (0);
}

int
zfs_userspace_many(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    uint64_t *cookiep, void *vbuf, uint64_t *bufsizep)
{
	int error;
	zap_cursor_t zc;
	zap_attribute_t za;
	zfs_useracct_t *buf = vbuf;
	uint64_t obj;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == 0) {
		*bufsizep = 0;
		return (0);
	}

	for (zap_cursor_init_serialized(&zc, zfsvfs->z_os, obj, *cookiep);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		if ((uintptr_t)buf - (uintptr_t)vbuf + sizeof (zfs_useracct_t) >
		    *bufsizep)
			break;

		fuidstr_to_sid(zfsvfs, za.za_name,
		    buf->zu_domain, sizeof (buf->zu_domain), &buf->zu_rid);

		buf->zu_space = za.za_first_integer;
		buf++;
	}
	if (error == ENOENT)
		error = 0;

	ASSERT3U((uintptr_t)buf - (uintptr_t)vbuf, <=, *bufsizep);
	*bufsizep = (uintptr_t)buf - (uintptr_t)vbuf;
	*cookiep = zap_cursor_serialize(&zc);
	zap_cursor_fini(&zc);
	return (error);
}

/*
 * buf must be big enough (eg, 32 bytes)
 */
static int
id_to_fuidstr(zfsvfs_t *zfsvfs, const char *domain, uid_t rid,
    char *buf, boolean_t addok)
{
	uint64_t fuid;
	int domainid = 0;

	if (domain && domain[0]) {
		domainid = zfs_fuid_find_by_domain(zfsvfs, domain, NULL, addok);
		if (domainid == -1)
			return (SET_ERROR(ENOENT));
	}
	fuid = FUID_ENCODE(domainid, rid);
	(void) snprintf(buf, 32, "%llx", (longlong_t)fuid);
	return (0);
}

int
zfs_userspace_one(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t *valp)
{
	char buf[32];
	int err;
	uint64_t obj;

	*valp = 0;

	if (!dmu_objset_userspace_present(zfsvfs->z_os))
		return (SET_ERROR(ENOTSUP));

	obj = zfs_userquota_prop_to_obj(zfsvfs, type);
	if (obj == 0)
		return (0);

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_FALSE);
	if (err)
		return (err);

	err = zap_lookup(zfsvfs->z_os, obj, buf, 8, 1, valp);
	if (err == ENOENT)
		err = 0;
	return (err);
}

int
zfs_set_userquota(zfsvfs_t *zfsvfs, zfs_userquota_prop_t type,
    const char *domain, uint64_t rid, uint64_t quota)
{
	char buf[32];
	int err;
	dmu_tx_t *tx;
	uint64_t *objp;
	boolean_t fuid_dirtied;

	if (type != ZFS_PROP_USERQUOTA && type != ZFS_PROP_GROUPQUOTA)
		return (SET_ERROR(EINVAL));

	if (zfsvfs->z_version < ZPL_VERSION_USERSPACE)
		return (SET_ERROR(ENOTSUP));

	objp = (type == ZFS_PROP_USERQUOTA) ? &zfsvfs->z_userquota_obj :
	    &zfsvfs->z_groupquota_obj;

	err = id_to_fuidstr(zfsvfs, domain, rid, buf, B_TRUE);
	if (err)
		return (err);
	fuid_dirtied = zfsvfs->z_fuid_dirty;

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, *objp ? *objp : DMU_NEW_OBJECT, B_TRUE, NULL);
	if (*objp == 0) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    zfs_userquota_prop_prefixes[type]);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	err = dmu_tx_assign(tx, TXG_WAIT);
	if (err) {
		dmu_tx_abort(tx);
		return (err);
	}

	mutex_enter(&zfsvfs->z_lock);
	if (*objp == 0) {
		*objp = zap_create(zfsvfs->z_os, DMU_OT_USERGROUP_QUOTA,
		    DMU_OT_NONE, 0, tx);
		VERIFY(0 == zap_add(zfsvfs->z_os, MASTER_NODE_OBJ,
		    zfs_userquota_prop_prefixes[type], 8, 1, objp, tx));
	}
	mutex_exit(&zfsvfs->z_lock);

	if (quota == 0) {
		err = zap_remove(zfsvfs->z_os, *objp, buf, tx);
		if (err == ENOENT)
			err = 0;
	} else {
		err = zap_update(zfsvfs->z_os, *objp, buf, 8, 1, &quota, tx);
	}
	ASSERT(err == 0);
	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);
	dmu_tx_commit(tx);
	return (err);
}

boolean_t
zfs_fuid_overquota(zfsvfs_t *zfsvfs, boolean_t isgroup, uint64_t fuid)
{
	char buf[32];
	uint64_t used, quota, usedobj, quotaobj;
	int err;

	usedobj = isgroup ? DMU_GROUPUSED_OBJECT : DMU_USERUSED_OBJECT;
	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	(void) snprintf(buf, sizeof(buf), "%llx", (longlong_t)fuid);
	err = zap_lookup(zfsvfs->z_os, quotaobj, buf, 8, 1, &quota);
	if (err != 0)
		return (B_FALSE);

	err = zap_lookup(zfsvfs->z_os, usedobj, buf, 8, 1, &used);
	if (err != 0)
		return (B_FALSE);
	return (used >= quota);
}

boolean_t
zfs_owner_overquota(zfsvfs_t *zfsvfs, znode_t *zp, boolean_t isgroup)
{
	uint64_t fuid;
	uint64_t quotaobj;

	quotaobj = isgroup ? zfsvfs->z_groupquota_obj : zfsvfs->z_userquota_obj;

	fuid = isgroup ? zp->z_gid : zp->z_uid;

	if (quotaobj == 0 || zfsvfs->z_replay)
		return (B_FALSE);

	return (zfs_fuid_overquota(zfsvfs, isgroup, fuid));
}

/*
 * Associate this zfsvfs with the given objset, which must be owned.
 * This will cache a bunch of on-disk state from the objset in the
 * zfsvfs.
 */
static int
zfsvfs_init(zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;
	uint64_t val;

	zfsvfs->z_max_blksz = SPA_OLD_MAXBLOCKSIZE;
	zfsvfs->z_show_ctldir = ZFS_SNAPDIR_VISIBLE;
	zfsvfs->z_os = os;

	/* Volume status "all ok" */
	zfsvfs->z_notification_conditions = 0;
	zfsvfs->z_freespace_notify_warninglimit = 0;
	zfsvfs->z_freespace_notify_dangerlimit = 0;
	zfsvfs->z_freespace_notify_desiredlevel = 0;

	error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &zfsvfs->z_version);
	if (error != 0)
		return (error);
	if (zfsvfs->z_version >
		zfs_zpl_version_map(spa_version(dmu_objset_spa(os)))) {
	 dprintf("Can't mount a version %lld file system "
					  "on a version %lld pool\n. Pool must be upgraded to mount "
					  "this file system.\n", (u_longlong_t)zfsvfs->z_version,
					  (u_longlong_t)spa_version(dmu_objset_spa(os)));
		return (SET_ERROR(ENOTSUP));
	}
	error = zfs_get_zplprop(os, ZFS_PROP_NORMALIZE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_norm = (int)val;

	error = zfs_get_zplprop(os, ZFS_PROP_UTF8ONLY, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_utf8 = (val != 0);

	error = zfs_get_zplprop(os, ZFS_PROP_CASE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_case = (uint_t)val;

	error = zfs_get_zplprop(os, ZFS_PROP_ACLMODE, &val);
	if (error != 0)
		return (error);
	zfsvfs->z_acl_mode = (uint_t)val;
#if __APPLE__
	zfs_get_zplprop(os, ZFS_PROP_APPLE_LASTUNMOUNT, &val);
	zfsvfs->z_last_unmount_time = val;
#endif
	/*
	 * Fold case on file systems that are always or sometimes case
	 * insensitive.
	 */
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
	    zfsvfs->z_case == ZFS_CASE_MIXED)
		zfsvfs->z_norm |= U8_TEXTPREP_TOUPPER;

	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);

	uint64_t sa_obj = 0;
	if (zfsvfs->z_use_sa) {
		/* should either have both of these objects or none */
		error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1,
		    &sa_obj);

		if (error != 0)
			return (error);

		error = zfs_get_zplprop(os, ZFS_PROP_XATTR, &val);
		if ((error == 0) && (val == ZFS_XATTR_SA))
			zfsvfs->z_xattr_sa = B_TRUE;
	}

	error = sa_setup(os, sa_obj, zfs_attr_table, ZPL_END,
	    &zfsvfs->z_attr_table);
	if (error != 0)
		return (error);

	if (zfsvfs->z_version >= ZPL_VERSION_SA)
		sa_register_update_callback(os, zfs_sa_upgrade);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1,
	    &zfsvfs->z_root);
	if (error != 0)
		return (error);
	ASSERT(zfsvfs->z_root != 0);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1,
	    &zfsvfs->z_unlinkedobj);
	if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA],
	    8, 1, &zfsvfs->z_userquota_obj);
	if (error == ENOENT)
		zfsvfs->z_userquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ,
	    zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA],
	    8, 1, &zfsvfs->z_groupquota_obj);
	if (error == ENOENT)
		zfsvfs->z_groupquota_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES, 8, 1,
	    &zfsvfs->z_fuid_obj);
	if (error == ENOENT)
		zfsvfs->z_fuid_obj = 0;
	else if (error != 0)
		return (error);

	error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SHARES_DIR, 8, 1,
	    &zfsvfs->z_shares_dir);
	if (error == ENOENT)
		zfsvfs->z_shares_dir = 0;
	else if (error != 0)
		return (error);

	return (0);
}

int
zfsvfs_create(const char *osname, zfsvfs_t **zfvp)
{
	objset_t *os;
	zfsvfs_t *zfsvfs;
	int error;

	zfsvfs = kmem_zalloc(sizeof (zfsvfs_t), KM_SLEEP);

	/*
	 * We claim to always be readonly so we can open snapshots;
	 * other ZPL code will prevent us from writing to snapshots.
	 */
	error = dmu_objset_own(osname, DMU_OST_ZFS, B_TRUE, B_TRUE,
	    zfsvfs, &os);
	if (error != 0) {
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	error = zfsvfs_create_impl(zfvp, zfsvfs, os);
	if (error != 0) {
		dmu_objset_disown(os, B_TRUE, zfsvfs);
	}
	return (error);
}

int
zfsvfs_create_impl(zfsvfs_t **zfvp, zfsvfs_t *zfsvfs, objset_t *os)
{
	int error;

	zfsvfs->z_vfs = NULL;
	zfsvfs->z_parent = zfsvfs;

	mutex_init(&zfsvfs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
	mutex_init(&zfsvfs->z_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&zfsvfs->z_all_znodes, sizeof (znode_t),
	    offsetof(znode_t, z_link_node));

	rrm_init(&zfsvfs->z_teardown_lock, B_FALSE);
	rw_init(&zfsvfs->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL);
	rw_init(&zfsvfs->z_fuid_lock, NULL, RW_DEFAULT, NULL);
#ifdef _WIN32
	rw_init(&zfsvfs->z_hardlinks_lock, NULL, RW_DEFAULT, NULL);
	avl_create(&zfsvfs->z_hardlinks, hardlinks_compare,
			   sizeof (hardlinks_t), offsetof(hardlinks_t, hl_node));
	avl_create(&zfsvfs->z_hardlinks_linkid, hardlinks_compare_linkid,
			   sizeof (hardlinks_t), offsetof(hardlinks_t, hl_node_linkid));
	zfsvfs->z_rdonly = 0;
#endif
	for (int i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_init(&zfsvfs->z_hold_mtx[i], NULL, MUTEX_DEFAULT, NULL);

	mutex_init(&zfsvfs->z_drain_lock, NULL, MUTEX_DEFAULT, NULL);
    cv_init(&zfsvfs->z_drain_cv, NULL, CV_DEFAULT, NULL);

	error = zfsvfs_init(zfsvfs, os);
	if (error != 0) {
		*zfvp = NULL;
		kmem_free(zfsvfs, sizeof (zfsvfs_t));
		return (error);
	}

	*zfvp = zfsvfs;
	return (0);
}

static int
zfsvfs_setup(zfsvfs_t *zfsvfs, boolean_t mounting)
{
	int error;
	boolean_t readonly = vfs_isrdonly(zfsvfs->z_vfs);

	/*
	 * Check for a bad on-disk format version now since we
	 * lied about owning the dataset readonly before.
	 */
	if (!readonly &&
	    dmu_objset_incompatible_encryption_version(zfsvfs->z_os))
		return (SET_ERROR(EROFS));

	error = zfs_register_callbacks(zfsvfs->z_vfs);
	if (error)
		return (error);

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);
	zfsvfs->z_log = zil_open(zfsvfs->z_os, zfs_get_data);

	/*
	 * If we are not mounting (ie: online recv), then we don't
	 * have to worry about replaying the log as we blocked all
	 * operations out since we closed the ZIL.
	 */
	if (mounting) {

		/*
		 * During replay we remove the read only flag to
		 * allow replays to succeed.
		 */

		if (readonly != 0)
			readonly_changed_cb(zfsvfs, B_FALSE);
		else
			if (!zfs_vnop_skip_unlinked_drain)
				zfs_unlinked_drain(zfsvfs);

		/*
		 * Parse and replay the intent log.
		 *
		 * Because of ziltest, this must be done after
		 * zfs_unlinked_drain().  (Further note: ziltest
		 * doesn't use readonly mounts, where
		 * zfs_unlinked_drain() isn't called.)  This is because
		 * ziltest causes spa_sync() to think it's committed,
		 * but actually it is not, so the intent log contains
		 * many txg's worth of changes.
		 *
		 * In particular, if object N is in the unlinked set in
		 * the last txg to actually sync, then it could be
		 * actually freed in a later txg and then reallocated
		 * in a yet later txg.  This would write a "create
		 * object N" record to the intent log.  Normally, this
		 * would be fine because the spa_sync() would have
		 * written out the fact that object N is free, before
		 * we could write the "create object N" intent log
		 * record.
		 *
		 * But when we are in ziltest mode, we advance the "open
		 * txg" without actually spa_sync()-ing the changes to
		 * disk.  So we would see that object N is still
		 * allocated and in the unlinked set, and there is an
		 * intent log record saying to allocate it.
		 */
		if (spa_writeable(dmu_objset_spa(zfsvfs->z_os))) {
			if (zil_replay_disable) {
				zil_destroy(zfsvfs->z_log, B_FALSE);
			} else {
				zfsvfs->z_replay = B_TRUE;
				zil_replay(zfsvfs->z_os, zfsvfs,
				    zfs_replay_vector);
				zfsvfs->z_replay = B_FALSE;
			}
		}

		/* restore readonly bit */
		if (readonly != 0)
			readonly_changed_cb(zfsvfs, B_TRUE);
	}

	/*
	 * Set the objset user_ptr to track its zfsvfs.
	 */
	mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
	dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
	mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	return (0);
}

extern krwlock_t zfsvfs_lock; /* in zfs_znode.c */

void
zfsvfs_free(zfsvfs_t *zfsvfs)
{
	int i;
    dprintf("+zfsvfs_free\n");
	/*
	 * This is a barrier to prevent the filesystem from going away in
	 * zfs_znode_move() until we can safely ensure that the filesystem is
	 * not unmounted. We consider the filesystem valid before the barrier
	 * and invalid after the barrier.
	 */
	//rw_enter(&zfsvfs_lock, RW_READER);
	//rw_exit(&zfsvfs_lock);

	zfs_fuid_destroy(zfsvfs);

    cv_destroy(&zfsvfs->z_drain_cv);
    mutex_destroy(&zfsvfs->z_drain_lock);
	mutex_destroy(&zfsvfs->z_znodes_lock);
	mutex_destroy(&zfsvfs->z_lock);
	list_destroy(&zfsvfs->z_all_znodes);
	rrm_destroy(&zfsvfs->z_teardown_lock);
	rw_destroy(&zfsvfs->z_teardown_inactive_lock);
	rw_destroy(&zfsvfs->z_fuid_lock);
#ifdef _WIN32
	dprintf("ZFS: Unloading hardlink AVLtree: %lu\n",
		   avl_numnodes(&zfsvfs->z_hardlinks));
	void *cookie = NULL;
	hardlinks_t *hardlink;
	rw_destroy(&zfsvfs->z_hardlinks_lock);
	while((hardlink = avl_destroy_nodes(&zfsvfs->z_hardlinks_linkid, &cookie))) {
	}
	cookie = NULL;
	while((hardlink = avl_destroy_nodes(&zfsvfs->z_hardlinks, &cookie))) {
		kmem_free(hardlink, sizeof(*hardlink));
	}
	avl_destroy(&zfsvfs->z_hardlinks);
	avl_destroy(&zfsvfs->z_hardlinks_linkid);
#endif
	for (i = 0; i != ZFS_OBJ_MTX_SZ; i++)
		mutex_destroy(&zfsvfs->z_hold_mtx[i]);
	kmem_free(zfsvfs, sizeof (zfsvfs_t));
    dprintf("-zfsvfs_free\n");
}

static void
zfs_set_fuid_feature(zfsvfs_t *zfsvfs)
{
	zfsvfs->z_use_fuids = USE_FUIDS(zfsvfs->z_version, zfsvfs->z_os);
	if (zfsvfs->z_vfs) {
#if 0
		if (zfsvfs->z_use_fuids) {
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
			vfs_set_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
		} else {
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_XVATTR);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_SYSATTR_VIEWS);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACEMASKONACCESS);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACLONCREATE);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_ACCESS_FILTER);
			vfs_clear_feature(zfsvfs->z_vfs, VFSFT_REPARSE);
		}
#endif
	}
	zfsvfs->z_use_sa = USE_SA(zfsvfs->z_version, zfsvfs->z_os);
}

static int
zfs_domount(struct mount *vfsp, dev_t mount_dev, char *osname, char *options,
	vfs_context_t *ctx)
{
dprintf("%s\n", __func__);
	int error = 0;
	zfsvfs_t *zfsvfs;
#ifndef _WIN32
	uint64_t recordsize, fsid_guid;
	vnode_t *vp;
#else
	uint64_t mimic_hfs = 0;
//	struct timeval tv;
#endif

	ASSERT(vfsp);
	ASSERT(osname);

	error = zfsvfs_create(osname, &zfsvfs);
	if (error)
		return (error);
	zfsvfs->z_vfs = vfsp;

	error = zfsvfs_parse_options(options, zfsvfs->z_vfs);
	if (error)
		goto out;

#ifdef illumos
	/* Initialize the generic filesystem structure. */
	vfsp->vfs_bcount = 0;
	vfsp->vfs_data = NULL;

	if (zfs_create_unique_device(&mount_dev) == -1) {
		error = ENODEV;
		goto out;
	}
	ASSERT(vfs_devismounted(mount_dev) == 0);
#endif


#ifdef _WIN32
	zfsvfs->z_rdev = mount_dev;

	/* HFS sets this prior to mounting */
	//vfs_setflags(vfsp, (uint64_t)((unsigned int)MNT_DOVOLFS));
	/* Advisory locking should be handled at the VFS layer */
	//vfs_setlocklocal(vfsp);

	/*
	 * Record the mount time (for Spotlight)
	 */
	//microtime(&tv);
	//zfsvfs->z_mount_time = tv.tv_sec;

	vfs_setfsprivate(vfsp, zfsvfs);
#else
	if (error = dsl_prop_get_integer(osname, "recordsize", &recordsize,
	    NULL))
		goto out;
	zfsvfs->z_vfs->vfs_bsize = SPA_MINBLOCKSIZE;
	zfsvfs->z_vfs->mnt_stat.f_iosize = recordsize;

	vfsp->vfs_data = zfsvfs;
	vfsp->mnt_flag |= MNT_LOCAL;
	vfsp->mnt_kern_flag |= MNTK_LOOKUP_SHARED;
	vfsp->mnt_kern_flag |= MNTK_SHARED_WRITES;
	vfsp->mnt_kern_flag |= MNTK_EXTENDED_SHARED;
#endif

	/*
	 * The fsid is 64 bits, composed of an 8-bit fs type, which
	 * separates our fsid from any other filesystem types, and a
	 * 56-bit objset unique ID.  The objset unique ID is unique to
	 * all objsets open on this system, provided by unique_create().
	 * The 8-bit fs type must be put in the low bits of fsid[1]
	 * because that's where other Solaris filesystems put it.
	 */

#ifdef __APPLE__
	error = dsl_prop_get_integer(osname, "com.apple.mimic_hfs", &mimic_hfs, NULL);
	if (zfsvfs->z_rdev) {
		struct vfsstatfs *vfsstatfs;
		vfsstatfs = vfs_statfs(vfsp);
		vfsstatfs->f_fsid.val[0] = zfsvfs->z_rdev;
		vfsstatfs->f_fsid.val[1] = vfs_typenum(vfsp);
	} else {
		// Otherwise, ask VFS to give us a random unique one.
		vfs_getnewfsid(vfsp);
		struct vfsstatfs *vfsstatfs;
		vfsstatfs = vfs_statfs(vfsp);
		zfsvfs->z_rdev = vfsstatfs->f_fsid.val[0];
	}

	/* If we are readonly (ie, waiting for rootmount) we need to reply
	 * honestly, so launchd runs fsck_zfs and mount_zfs
	 */
	if(mimic_hfs) {
	    struct vfsstatfs *vfsstatfs;
	    vfsstatfs = vfs_statfs(vfsp);
	    strlcpy(vfsstatfs->f_fstypename, "hfs", MFSTYPENAMELEN);
	}

#endif

	/*
	 * Set features for file system.
	 */
	zfs_set_fuid_feature(zfsvfs);
#if 0
	if (zfsvfs->z_case == ZFS_CASE_INSENSITIVE) {
		vfs_set_feature(vfsp, VFSFT_DIRENTFLAGS);
		vfs_set_feature(vfsp, VFSFT_CASEINSENSITIVE);
		vfs_set_feature(vfsp, VFSFT_NOCASESENSITIVE);
	} else if (zfsvfs->z_case == ZFS_CASE_MIXED) {
		vfs_set_feature(vfsp, VFSFT_DIRENTFLAGS);
		vfs_set_feature(vfsp, VFSFT_CASEINSENSITIVE);
	}
	vfs_set_feature(vfsp, VFSFT_ZEROCOPY_SUPPORTED);
#endif

	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
		uint64_t pval;
		char fsname[MAXNAMELEN];
		zfsvfs_t *fs_zfsvfs;

		dmu_fsname(osname, fsname);
		error = getzfsvfs(fsname, &fs_zfsvfs);
		if (error == 0) {
			if (fs_zfsvfs->z_unmounted)
				error = SET_ERROR(EINVAL);
			vfs_unbusy(fs_zfsvfs->z_vfs);
		}
		if (error) {
			dprintf("file system '%s' is unmounted : error %d\n",
			    fsname,
			    error);
			goto out;
		}

		atime_changed_cb(zfsvfs, B_FALSE);
		readonly_changed_cb(zfsvfs, B_TRUE);
		if ((error = dsl_prop_get_integer(osname, "xattr", &pval, NULL)))
			goto out;
		xattr_changed_cb(zfsvfs, pval);
		zfsvfs->z_issnap = B_TRUE;
		zfsvfs->z_os->os_sync = ZFS_SYNC_DISABLED;

		mutex_enter(&zfsvfs->z_os->os_user_ptr_lock);
		dmu_objset_set_user(zfsvfs->z_os, zfsvfs);
		mutex_exit(&zfsvfs->z_os->os_user_ptr_lock);

	} else {
		if ((error = zfsvfs_setup(zfsvfs, B_TRUE)))
			goto out;
	}

#ifdef _WIN32
	vfs_setflags(vfsp, (uint64_t)((unsigned int)MNT_JOURNALED));

	if ((vfs_flags(vfsp) & MNT_ROOTFS) != 0) {
		/* Root FS */
		vfs_clearflags(vfsp,
		    (uint64_t)((unsigned int)MNT_UNKNOWNPERMISSIONS));
		vfs_clearflags(vfsp,
		    (uint64_t)((unsigned int)MNT_IGNORE_OWNERSHIP));
	//} else {
		//vfs_setflags(vfsp,
		//    (uint64_t)((unsigned int)MNT_AUTOMOUNTED));
	}

	vfs_mountedfrom(vfsp, osname);
#else
	/* Grab extra reference. */
	VERIFY(VFS_ROOT(vfsp, LK_EXCLUSIVE, &vp) == 0);
	VOP_UNLOCK(vp, 0);
#endif

#if 0 // Want .zfs or not
	if (!zfsvfs->z_issnap) {
		zfsctl_create(zfsvfs);
    }
#endif
out:
	if (error) {
		vfs_setfsprivate(vfsp, NULL);
		dmu_objset_disown(zfsvfs->z_os, B_TRUE, zfsvfs);
		zfsvfs_free(zfsvfs);
	} else {
		atomic_inc_32(&zfs_active_fs_count);
	}

	return (error);
}

void
zfs_unregister_callbacks(zfsvfs_t *zfsvfs)
{
	objset_t *os = zfsvfs->z_os;

	/*
	 * Unregister properties.
	 */
	if (!dmu_objset_is_snapshot(os)) {
		dsl_prop_unregister_all(dmu_objset_ds(os), zfsvfs);
	}
}

#ifdef SECLABEL
/*
 * Convert a decimal digit string to a uint64_t integer.
 */
static int
str_to_uint64(char *str, uint64_t *objnum)
{
	uint64_t num = 0;

	while (*str) {
		if (*str < '0' || *str > '9')
			return (EINVAL);

		num = num*10 + *str++ - '0';
	}

	*objnum = num;
	return (0);
}

/*
 * The boot path passed from the boot loader is in the form of
 * "rootpool-name/root-filesystem-object-number'. Convert this
 * string to a dataset name: "rootpool-name/root-filesystem-name".
 */
static int
zfs_parse_bootfs(char *bpath, char *outpath)
{
	char *slashp;
	uint64_t objnum;
	int error;

	if (*bpath == 0 || *bpath == '/')
		return (EINVAL);

	(void) strcpy(outpath, bpath);

	slashp = strchr(bpath, '/');

	/* if no '/', just return the pool name */
	if (slashp == NULL) {
		return (0);
	}

	/* if not a number, just return the root dataset name */
	if (str_to_uint64(slashp+1, &objnum)) {
		return (0);
	}

	*slashp = '\0';
	error = dsl_dsobj_to_dsname(bpath, objnum, outpath);
	*slashp = '/';

	return (error);
}

/*
 * Check that the hex label string is appropriate for the dataset being
 * mounted into the global_zone proper.
 *
 * Return an error if the hex label string is not default or
 * admin_low/admin_high.  For admin_low labels, the corresponding
 * dataset must be readonly.
 */
int
zfs_check_global_label(const char *dsname, const char *hexsl)
{
	if (strcasecmp(hexsl, ZFS_MLSLABEL_DEFAULT) == 0)
		return (0);
	if (strcasecmp(hexsl, ADMIN_HIGH) == 0)
		return (0);
	if (strcasecmp(hexsl, ADMIN_LOW) == 0) {
		/* must be readonly */
		uint64_t rdonly;

		if (dsl_prop_get_integer(dsname,
		    zfs_prop_to_name(ZFS_PROP_READONLY), &rdonly, NULL))
			return (SET_ERROR(EACCES));
		return (rdonly ? 0 : EACCES);
	}
	return (SET_ERROR(EACCES));
}

/*
 * zfs_mount_label_policy:
 *	Determine whether the mount is allowed according to MAC check.
 *	by comparing (where appropriate) label of the dataset against
 *	the label of the zone being mounted into.  If the dataset has
 *	no label, create one.
 *
 *	Returns:
 *		 0 :	access allowed
 *		>0 :	error code, such as EACCES
 */
static int
zfs_mount_label_policy(vfs_t *vfsp, char *osname)
{
	int		error, retv;
	zone_t		*mntzone = NULL;
	ts_label_t	*mnt_tsl;
	bslabel_t	*mnt_sl;
	bslabel_t	ds_sl;
	char		ds_hexsl[MAXNAMELEN];

	retv = EACCES;				/* assume the worst */

	/*
	 * Start by getting the dataset label if it exists.
	 */
	error = dsl_prop_get(osname, zfs_prop_to_name(ZFS_PROP_MLSLABEL),
	    1, sizeof (ds_hexsl), &ds_hexsl, NULL);
	if (error)
		return (EACCES);

	/*
	 * If labeling is NOT enabled, then disallow the mount of datasets
	 * which have a non-default label already.  No other label checks
	 * are needed.
	 */
	if (!is_system_labeled()) {
		if (strcasecmp(ds_hexsl, ZFS_MLSLABEL_DEFAULT) == 0)
			return (0);
		return (EACCES);
	}

	/*
	 * Get the label of the mountpoint.  If mounting into the global
	 * zone (i.e. mountpoint is not within an active zone and the
	 * zoned property is off), the label must be default or
	 * admin_low/admin_high only; no other checks are needed.
	 */
	mntzone = zone_find_by_any_path(refstr_value(vfsp->vfs_mntpt), B_FALSE);
	if (mntzone->zone_id == GLOBAL_ZONEID) {
		uint64_t zoned;

		zone_rele(mntzone);

		if (dsl_prop_get_integer(osname,
		    zfs_prop_to_name(ZFS_PROP_ZONED), &zoned, NULL))
			return (EACCES);
		if (!zoned)
			return (zfs_check_global_label(osname, ds_hexsl));
		else
			/*
			 * This is the case of a zone dataset being mounted
			 * initially, before the zone has been fully created;
			 * allow this mount into global zone.
			 */
			return (0);
	}

	mnt_tsl = mntzone->zone_slabel;
	ASSERT(mnt_tsl != NULL);
	label_hold(mnt_tsl);
	mnt_sl = label2bslabel(mnt_tsl);

	if (strcasecmp(ds_hexsl, ZFS_MLSLABEL_DEFAULT) == 0) {
		/*
		 * The dataset doesn't have a real label, so fabricate one.
		 */
		char *str = NULL;

		if (l_to_str_internal(mnt_sl, &str) == 0 &&
		    dsl_prop_set_string(osname,
		    zfs_prop_to_name(ZFS_PROP_MLSLABEL),
		    ZPROP_SRC_LOCAL, str) == 0)
			retv = 0;
		if (str != NULL)
			kmem_free(str, strlen(str) + 1);
	} else if (hexstr_to_label(ds_hexsl, &ds_sl) == 0) {
		/*
		 * Now compare labels to complete the MAC check.  If the
		 * labels are equal then allow access.  If the mountpoint
		 * label dominates the dataset label, allow readonly access.
		 * Otherwise, access is denied.
		 */
		if (blequal(mnt_sl, &ds_sl))
			retv = 0;
		else if (bldominates(mnt_sl, &ds_sl)) {
			vfs_setmntopt(vfsp, MNTOPT_RO, NULL, 0);
			retv = 0;
		}
	}

	label_rele(mnt_tsl);
	zone_rele(mntzone);
	return (retv);
}
#endif	/* SECLABEL */

#ifdef ZFS_BOOT
/* Used by mountroot */
#define	ZFS_BOOT_MOUNT_DEV	1
int zfs_boot_get_path(char *, int);
#endif

/*
 * zfs_vfs_mountroot
 * Given a device vnode created by vfs_mountroot bdevvp,
 * and with the root pool already imported, root mount the
 * dataset specified in the pool's bootfs property.
 *
 * Inputs:
 * mp: VFS mount struct
 * devvp: device vnode, currently only used to retrieve the
 *  dev_t for the fsid. Could vnode_get, vnode_ref, vnode_put,
 *  with matching get/rele/put in zfs_vfs_umount, but this is
 *  already done by XNU as well.
 * ctx: VFS context, unused.
 *
 * Return:
 * 0 on success, positive int on failure.
 */
int
zfs_vfs_mountroot(struct mount *mp, struct vnode *devvp, vfs_context_t *ctx)
{
	int error = EINVAL;
#if 0
	/*
	static int zfsrootdone = 0;
	*/
	zfsvfs_t *zfsvfs = NULL;
	//znode_t *zp = NULL;
	spa_t *spa = 0;
	char *zfs_bootfs = 0;
	char *path = 0;
	dev_t dev = 0;
	//int len = MAXPATHLEN;

printf("ZFS: %s\n", __func__);
	ASSERT(mp);
	ASSERT(devvp);
	ASSERT(ctx);
	if (!mp || !devvp | !ctx) {
		cmn_err(CE_NOTE, "%s: missing one of mp %p devvp %p"
		    " or ctx %p", __func__, mp, devvp, ctx);
		return (EINVAL);
	}

	/* Look up bootfs variable from pool here */
	zfs_bootfs = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (!zfs_bootfs) {
		cmn_err(CE_NOTE, "%s: bootfs alloc failed",
		    __func__);
		return (ENOMEM);
	}

#ifdef ZFS_BOOT_MOUNT_DEV
	path = kmem_alloc(MAXPATHLEN, KM_SLEEP);
	if (!path) {
		cmn_err(CE_NOTE, "%s: path alloc failed",
		    __func__);
		kmem_free(zfs_bootfs, MAXPATHLEN);
		return (ENOMEM);
	}
#endif

	mutex_enter(&spa_namespace_lock);
	spa = spa_next(NULL);
	if (!spa) {
		mutex_exit(&spa_namespace_lock);
		cmn_err(CE_NOTE, "%s: no pool available",
		    __func__);
		goto out;
	}

	error = dsl_dsobj_to_dsname(spa_name(spa),
	    spa_bootfs(spa), zfs_bootfs);
	if (error != 0) {
		mutex_exit(&spa_namespace_lock);
		cmn_err(CE_NOTE, "%s: bootfs to name error %d",
		    __func__, error);
		goto out;
	}
	mutex_exit(&spa_namespace_lock);

#ifdef ZFS_BOOT_MOUNT_DEV
	/* XXX Could also do IOKit lookup from dev_t to diskN */
	error = zfs_boot_get_path(path, MAXPATHLEN);
	if (error != 0) {
		cmn_err(CE_NOTE, "get_path: error %d", error);
		goto out;
	}
#endif

	/*
	 * By setting the dev_t value in the mount vfsp,
	 * mount_zfs will be called with the /dev/diskN
	 * proxy, but we can leave the dataset name in
	 * the mountedfrom field
	 */
	dev = vnode_specrdev(devvp);

	printf("Setting readonly\n");

	if ((error = zfs_domount(mp, dev, zfs_bootfs, NULL, ctx)) != 0) {
		//cmn_err(CE_NOTE, "zfs_domount: error %d", error);
		printf("zfs_domount: error %d", error);
		/* Only drop the usecount if mount fails */
		//vnode_rele(devvp);
		goto out;
	}
	// vfs_clearflags(mp, (u_int64_t)((unsigned int)MNT_AUTOMOUNTED));

#ifdef ZFS_BOOT_MOUNT_DEV
	// override the mount from field
	if (strlen(path) > 0) {
		vfs_mountedfrom(mp, path);
	} else {
		printf("%s couldn't set vfs_mountedfrom\n", __func__);
		//goto out;
	}
#endif

	zfsvfs = (zfsvfs_t *)vfs_fsprivate(mp);
	ASSERT(zfsvfs);
	if (!zfsvfs) {
		cmn_err(CE_NOTE, "missing zfsvfs");
		goto out;
	}

	/* Set this mount to read-only */
	zfsvfs->z_rdonly = 1;

	/*
	 * Due to XNU mount flags, readonly gets set off for a short
	 * while, which means mimic will kick in if enabled. But we need
	 * to reply with true "zfs" until root has been remounted RW, so
	 * that launchd tries to run mount_zfs instead of mount_hfs
	 */
	mimic_hfs_changed_cb(zfsvfs, B_FALSE);

	/*
	 * Leave rootvp held.  The root file system is never unmounted.
	 *
	 * XXX APPLE
	 * xnu will in fact call vfs_unmount on the root filesystem
	 * during shutdown/reboot.
	 */

out:

	if (path) {
		kmem_free(path, MAXPATHLEN);
	}
	if (zfs_bootfs) {
		kmem_free(zfs_bootfs, MAXPATHLEN);
	}

#endif
	return (error);
}

#ifdef __LINUX__
static int
getpoolname(const char *osname, char *poolname)
{
	char *p;

	p = strchr(osname, '/');
	if (p == NULL) {
		if (strlen(osname) >= MAXNAMELEN)
			return (ENAMETOOLONG);
		(void) strlcpy(poolname, osname, MAXNAMELEN);
	} else {
		if (p - osname >= MAXNAMELEN)
			return (ENAMETOOLONG);
		(void) strncpy(poolname, osname, p - osname);
		poolname[p - osname] = '\0';
	}
	return (0);
}
#endif

/*ARGSUSED*/
int
zfs_vfs_mount(struct mount *vfsp, vnode_t *mvp /*devvp*/,
              user_addr_t data, vfs_context_t *context)
{
	int		error = 0;
	cred_t		*cr = NULL;//(cred_t *)vfs_context_ucred(context);
	char		*osname = NULL;
	char		*options = NULL;
	uint64_t	flags = vfs_flags(vfsp);
	int		canwrite;
	int		rdonly = 0;
	int		mflag = 0;

#ifdef _WIN32
	struct zfs_mount_args *mnt_args = (struct zfs_mount_args *)data;
	size_t		osnamelen = 0;
	uint32_t	cmdflags = 0;

dprintf("%s\n", __func__);
	cmdflags = (uint32_t) vfs_flags(vfsp) & MNT_CMDFLAGS;
	rdonly = vfs_isrdonly(vfsp);
dprintf("%s cmdflags %u rdonly %d\n", __func__, cmdflags, rdonly);

	/*
	* Get the objset name (the "special" mount argument).
	*/
	if (data) {

		// Allocate string area
		osname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

		strlcpy(osname, mnt_args->fspec, MAXPATHLEN);

	}

	if (mnt_args->struct_size == sizeof(*mnt_args)) {

		mflag = mnt_args->mflag;

		if (mnt_args->optlen) {
			options = kmem_alloc(mnt_args->optlen, KM_SLEEP);
			strlcpy(options, mnt_args->optptr, mnt_args->optlen);
		}
			//dprintf("vfs_mount: fspec '%s' : mflag %04llx : optptr %p : optlen %d :"
		dprintf("%s: fspec '%s' : mflag %04x : optptr %p : optlen %d :"
		    " options %s\n", __func__,
			osname,
			mnt_args->mflag,
			mnt_args->optptr,
			mnt_args->optlen,
			options);
	}

//	(void) dnlc_purge_vfsp(vfsp, 0);

	if (mflag & MS_RDONLY) {
		dprintf("%s: adding MNT_RDONLY\n", __func__);
		flags |= MNT_RDONLY;
	}

	if (mflag & MS_OVERLAY) {
		dprintf("%s: adding MNT_UNION\n", __func__);
		flags |= MNT_UNION;
	}

	if (mflag & MS_FORCE) {
		dprintf("%s: adding MNT_FORCE\n", __func__);
		flags |= MNT_FORCE;
	}

	if (mflag & MS_REMOUNT) {
		dprintf("%s: adding MNT_UPDATE on MS_REMOUNT\n", __func__);
		flags |= MNT_UPDATE;
	}

	vfs_setflags(vfsp, (uint64_t)flags);

#endif


	/*
	 * If full-owner-access is enabled and delegated administration is
	 * turned on, we must set nosuid.
	 */
#if 0
	if (zfs_super_owner &&
	    dsl_deleg_access(osname, ZFS_DELEG_PERM_MOUNT, cr) != ECANCELED) {
		secpolicy_fs_mount_clearopts(cr, vfsp);
	}
#endif

	/*
	 * Check for mount privilege?
	 *
	 * If we don't have privilege then see if
	 * we have local permission to allow it
	 */
	error = secpolicy_fs_mount(cr, mvp, vfsp);
	if (error) {
		if (dsl_deleg_access(osname, ZFS_DELEG_PERM_MOUNT, cr) != 0) {
			cmn_err(CE_NOTE, "%s: mount perm error",
			    __func__);
			goto out;
		}

#if 0
		if (!(vfsp->vfs_flag & MS_REMOUNT)) {
			vattr_t		vattr;

			/*
			 * Make sure user is the owner of the mount point
			 * or has sufficient privileges.
			 */

			vattr.va_mask = AT_UID;

			vn_lock(mvp, LK_SHARED | LK_RETRY);
			if (VOP_GETATTR(mvp, &vattr, cr)) {
				VOP_UNLOCK(mvp, 0);
				goto out;
			}

			if (secpolicy_vnode_owner(mvp, cr, vattr.va_uid) != 0 &&
			    VOP_ACCESS(mvp, VWRITE, cr, td) != 0) {
				VOP_UNLOCK(mvp, 0);
				goto out;
			}
			VOP_UNLOCK(mvp, 0);
		}
#endif
		secpolicy_fs_mount_clearopts(cr, vfsp);
	}

	/*
	 * Refuse to mount a filesystem if we are in a local zone and the
	 * dataset is not visible.
	 */
	if (!INGLOBALZONE(curthread) &&
	    (!zone_dataset_visible(osname, &canwrite) || !canwrite)) {
		error = EPERM;
		goto out;
	}

#ifdef SECLABEL
	error = zfs_mount_label_policy(vfsp, osname);
	if (error)
		goto out;
#endif

#ifndef _WIN32
	vfsp->vfs_flag |= MNT_NFS4ACLS;

	/*
	 * When doing a remount, we simply refresh our temporary properties
	 * according to those options set in the current VFS options.
	 */
	if (vfsp->vfs_flag & MS_REMOUNT) {
		/* refresh mount options */
		zfs_unregister_callbacks(vfsp->vfs_data);
		error = zfs_register_callbacks(vfsp);
		goto out;
	}

	/* Initial root mount: try hard to import the requested root pool. */
	if ((vfsp->vfs_flag & MNT_ROOTFS) != 0 &&
	    (vfsp->vfs_flag & MNT_UPDATE) == 0) {
		char pname[MAXNAMELEN];

		error = getpoolname(osname, pname);
		if (error == 0)
			error = spa_import_rootpool(pname);
		if (error)
			goto out;
	}
#else
	/*
	 * When doing a remount, we simply refresh our temporary properties
	 * according to those options set in the current VFS options.
	 */
	if (cmdflags & MNT_UPDATE) {

		if (cmdflags & MNT_RELOAD) {
			dprintf("%s: reload after fsck\n", __func__);
			error = 0;
			goto out;
		}

		/* refresh mount options */
		zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);
		ASSERT(zfsvfs);

		if (zfsvfs->z_rdonly == 0 && (flags & MNT_RDONLY ||
		    vfs_isrdonly(vfsp))) {
			/* downgrade */
			dprintf("%s: downgrade requested\n", __func__);
			zfsvfs->z_rdonly = 1;
			readonly_changed_cb(zfsvfs, B_TRUE);
			zfs_unregister_callbacks(zfsvfs);
			error = zfs_register_callbacks(vfsp);
			if (error) {
				dprintf("%s: remount returned %d",
				    __func__, error);
			}
		}

		//if (zfsvfs->z_rdonly != 0 && vfs_iswriteupgrade(vfsp)) {
		if (0 /*vfs_iswriteupgrade(vfsp)*/) {
			/* upgrade */
			dprintf("%s: upgrade requested\n", __func__);
			zfsvfs->z_rdonly = 0;
			readonly_changed_cb(zfsvfs, B_FALSE);
			zfs_unregister_callbacks(zfsvfs);
			error = zfs_register_callbacks(vfsp);
			if (error) {
				dprintf("%s: remount returned %d",
				    __func__, error);
			}
		}

/*
		printf("%s: would do remount\n", __func__);

		zfs_unregister_callbacks(zfsvfs);
		error = zfs_register_callbacks(vfsp);

		if (error) {
			//cmn_err(CE_NOTE, "%s: remount returned %d",
			printf("%s: remount returned %d",
			    __func__, error);
		}
*/
		goto out;
	}

	if (vfs_fsprivate(vfsp) != NULL) {
		dprintf("already mounted\n");
		error = 0;
		goto out;
	}

//dprintf("%s: calling zfs_domount\n", __func__);
#endif

	error = zfs_domount(vfsp, 0, osname, options, context);

	if (error) {
		//cmn_err(CE_NOTE, "%s: zfs_domount returned %d\n",
		dprintf("%s: zfs_domount returned %d\n",
		    __func__, error);
	error = 0;
		goto out;
	}

#ifdef sun
	/*
	 * Add an extra VFS_HOLD on our parent vfs so that it can't
	 * disappear due to a forced unmount.
	 */
	if (error == 0 && ((zfsvfs_t *)vfsp->vfs_data)->z_issnap)
		VFS_HOLD(mvp->v_vfsp);
#endif	/* sun */

out:
#ifdef _WIN32
	//dprintf("%s out: %d\n", __func__, error);
	if (error == 0) {
#if 0
		zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);
		ASSERT(zfsvfs);
#endif

		//dprintf("%s: setting vfs flags\n", __func__);
		/* Indicate to VFS that we support ACLs. */
//		vfs_setextendedsecurity(vfsp);
	}

	if (error)
		dprintf("zfs_vfs_mount: error %d\n", error);

	if (osname)
		kmem_free(osname, MAXPATHLEN);

	if (options)
		kmem_free(options, mnt_args->optlen);
#endif

	return (error);
}


int
zfs_vfs_getattr(struct mount *mp, struct vfs_attr *fsap,  vfs_context_t *context)
{
#if 0
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	uint64_t refdbytes, availbytes, usedobjs, availobjs;
	uint64_t log_blksize;
	uint64_t log_blkcnt;

    dprintf("vfs_getattr\n");

	ZFS_ENTER(zfsvfs);

	/* Finder will show the old/incorrect size, we can force a sync of the pool
	 * to make it correct, but that has side effects which are undesirable.
	 */
	/* txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0); */

	dmu_objset_space(zfsvfs->z_os,
	    &refdbytes, &availbytes, &usedobjs, &availobjs);

	VFSATTR_RETURN(fsap, f_objcount, usedobjs);
	VFSATTR_RETURN(fsap, f_maxobjcount, 0x7fffffffffffffff);
	/*
	 * Carbon depends on f_filecount and f_dircount so
	 * make up some values based on total objects.
	 */
	VFSATTR_RETURN(fsap, f_filecount, usedobjs - (usedobjs / 4));
	VFSATTR_RETURN(fsap, f_dircount, usedobjs / 4);

	/*
	 * Model after HFS in working out if we should use the legacy size
	 * 512, or go to 4096. Note that XNU only likes those two
	 * blocksizes, so we don't use the ZFS recordsize
	 */
	log_blkcnt = (u_int64_t)((refdbytes + availbytes) >> SPA_MINBLOCKSHIFT);
	log_blksize = (log_blkcnt > 0x000000007fffffff) ?
		4096 :
		(1 << SPA_MINBLOCKSHIFT);

	/*
	 * The underlying storage pool actually uses multiple block sizes.
	 * We report the fragsize as the smallest block size we support,
	 * and we report our blocksize as the filesystem's maximum blocksize.
	 */
	VFSATTR_RETURN(fsap, f_bsize, log_blksize);
	VFSATTR_RETURN(fsap, f_iosize, zfsvfs->z_max_blksz);

	/*
	 * The following report "total" blocks of various kinds in the
	 * file system, but reported in terms of f_frsize - the
	 * "fragment" size.
	 */
	VFSATTR_RETURN(fsap, f_blocks,
	               (u_int64_t)((refdbytes + availbytes) / log_blksize));
	VFSATTR_RETURN(fsap, f_bfree, (u_int64_t)(availbytes / log_blksize));
	VFSATTR_RETURN(fsap, f_bavail, fsap->f_bfree);  /* no root reservation */
	VFSATTR_RETURN(fsap, f_bused, fsap->f_blocks - fsap->f_bfree);

	/*
	 * statvfs() should really be called statufs(), because it assumes
	 * static metadata.  ZFS doesn't preallocate files, so the best
	 * we can do is report the max that could possibly fit in f_files,
	 * and that minus the number actually used in f_ffree.
	 * For f_ffree, report the smaller of the number of object available
	 * and the number of blocks (each object will take at least a block).
	 */
	VFSATTR_RETURN(fsap, f_ffree, (u_int64_t)MIN(availobjs, fsap->f_bfree));
	VFSATTR_RETURN(fsap, f_files,  fsap->f_ffree + usedobjs);

	if (VFSATTR_IS_ACTIVE(fsap, f_fsid)) {
		fsap->f_fsid.val[0] = zfsvfs->z_rdev;
		fsap->f_fsid.val[1] = vfs_typenum(mp);
		VFSATTR_SET_SUPPORTED(fsap, f_fsid);
	}
	if (VFSATTR_IS_ACTIVE(fsap, f_capabilities)) {
		fsap->f_capabilities.capabilities[VOL_CAPABILITIES_FORMAT] =
			VOL_CAP_FMT_PERSISTENTOBJECTIDS |
			VOL_CAP_FMT_HARDLINKS |      // ZFS
			VOL_CAP_FMT_SPARSE_FILES |   // ZFS
			VOL_CAP_FMT_2TB_FILESIZE |   // ZFS
			VOL_CAP_FMT_JOURNAL | VOL_CAP_FMT_JOURNAL_ACTIVE | // ZFS
			VOL_CAP_FMT_SYMBOLICLINKS |  // msdos..

			// ZFS has root times just fine
			/*VOL_CAP_FMT_NO_ROOT_TIMES |*/

			// Ask XNU to remember zero-runs, instead of writing
			// zeros to it.
			VOL_CAP_FMT_ZERO_RUNS |

			VOL_CAP_FMT_CASE_PRESERVING |
			VOL_CAP_FMT_FAST_STATFS |
			VOL_CAP_FMT_PATH_FROM_ID |
			VOL_CAP_FMT_64BIT_OBJECT_IDS |
			VOL_CAP_FMT_HIDDEN_FILES ;
		fsap->f_capabilities.capabilities[VOL_CAPABILITIES_INTERFACES] =
			VOL_CAP_INT_ATTRLIST |          // ZFS
			VOL_CAP_INT_NFSEXPORT |         // ZFS
			VOL_CAP_INT_EXTENDED_SECURITY | // ZFS
#if NAMEDSTREAMS
			VOL_CAP_INT_NAMEDSTREAMS |      // ZFS
#endif
			VOL_CAP_INT_EXTENDED_ATTR |     // ZFS
			VOL_CAP_INT_VOL_RENAME |        // msdos..
			VOL_CAP_INT_ADVLOCK |

			// ZFS does not yet have exchangedata (it's in a branch)
			/* VOL_CAP_INT_EXCHANGEDATA| */

			// ZFS does not yet have copyfile
			/* VOL_CAP_INT_COPYFILE| */

			// ZFS does not yet have allocate
			/*VOL_CAP_INT_ALLOCATE|*/

			VOL_CAP_INT_FLOCK ;
		fsap->f_capabilities.capabilities[VOL_CAPABILITIES_RESERVED1] = 0;
		fsap->f_capabilities.capabilities[VOL_CAPABILITIES_RESERVED2] = 0;

		/* This is the list of valid capabilities at time of
		 * compile. The valid list should have them all defined
		 * and the "capability" list above should enable only
		 * those we have implemented
		 */
		fsap->f_capabilities.valid[VOL_CAPABILITIES_FORMAT] =
			VOL_CAP_FMT_PERSISTENTOBJECTIDS |
			VOL_CAP_FMT_SYMBOLICLINKS |
			VOL_CAP_FMT_HARDLINKS |
			VOL_CAP_FMT_JOURNAL |
			VOL_CAP_FMT_JOURNAL_ACTIVE |
			VOL_CAP_FMT_NO_ROOT_TIMES |
			VOL_CAP_FMT_SPARSE_FILES |
			VOL_CAP_FMT_ZERO_RUNS |
			VOL_CAP_FMT_CASE_SENSITIVE |
			VOL_CAP_FMT_CASE_PRESERVING |
			VOL_CAP_FMT_FAST_STATFS |
			VOL_CAP_FMT_2TB_FILESIZE |
			VOL_CAP_FMT_OPENDENYMODES |
			VOL_CAP_FMT_PATH_FROM_ID |
			VOL_CAP_FMT_64BIT_OBJECT_IDS |
			VOL_CAP_FMT_NO_VOLUME_SIZES |
			VOL_CAP_FMT_DECMPFS_COMPRESSION |
			VOL_CAP_FMT_HIDDEN_FILES ;
		fsap->f_capabilities.valid[VOL_CAPABILITIES_INTERFACES] =
			VOL_CAP_INT_SEARCHFS |
			VOL_CAP_INT_ATTRLIST |
			VOL_CAP_INT_NFSEXPORT |
			VOL_CAP_INT_READDIRATTR |
			VOL_CAP_INT_EXCHANGEDATA |
			VOL_CAP_INT_COPYFILE |
			VOL_CAP_INT_ALLOCATE |
			VOL_CAP_INT_VOL_RENAME |
			VOL_CAP_INT_ADVLOCK |
			VOL_CAP_INT_FLOCK |
			VOL_CAP_INT_EXTENDED_ATTR |
			VOL_CAP_INT_USERACCESS |
#if NAMEDSTREAMS
			VOL_CAP_INT_NAMEDSTREAMS |
#endif

			VOL_CAP_INT_MANLOCK ;
		fsap->f_capabilities.valid[VOL_CAPABILITIES_RESERVED1] = 0;
		fsap->f_capabilities.valid[VOL_CAPABILITIES_RESERVED2] = 0;

		/* Check if we are case-sensitive */
		if (zfsvfs->z_case == ZFS_CASE_SENSITIVE)
			fsap->f_capabilities.capabilities[VOL_CAPABILITIES_FORMAT]
				|= VOL_CAP_FMT_CASE_SENSITIVE;

		/* Check if xattr is enabled */
		if (zfsvfs->z_xattr == B_TRUE) {
			fsap->f_capabilities.capabilities[VOL_CAPABILITIES_INTERFACES]
				|= VOL_CAP_INT_EXTENDED_ATTR;
		}

		VFSATTR_SET_SUPPORTED(fsap, f_capabilities);
	}
	if (VFSATTR_IS_ACTIVE(fsap, f_attributes)) {

		fsap->f_attributes.validattr.commonattr =
			ATTR_CMN_NAME	|
			ATTR_CMN_DEVID	|
			ATTR_CMN_FSID	|
			ATTR_CMN_OBJTYPE |
			ATTR_CMN_OBJTAG	|
			ATTR_CMN_OBJID	|
			ATTR_CMN_OBJPERMANENTID |
			ATTR_CMN_PAROBJID |
			/* ATTR_CMN_SCRIPT | */
			ATTR_CMN_CRTIME |
			ATTR_CMN_MODTIME |
			ATTR_CMN_CHGTIME |
			ATTR_CMN_ACCTIME |
			/* ATTR_CMN_BKUPTIME | */
			ATTR_CMN_FNDRINFO |
			ATTR_CMN_OWNERID |
			ATTR_CMN_GRPID	|
			ATTR_CMN_ACCESSMASK |
			ATTR_CMN_FLAGS	|
			ATTR_CMN_USERACCESS |
			ATTR_CMN_EXTENDED_SECURITY |
			ATTR_CMN_UUID |
			ATTR_CMN_GRPUUID |
			0;
		fsap->f_attributes.validattr.volattr =
			ATTR_VOL_FSTYPE	|
			ATTR_VOL_SIGNATURE |
			ATTR_VOL_SIZE	|
			ATTR_VOL_SPACEFREE |
			ATTR_VOL_SPACEAVAIL |
			ATTR_VOL_MINALLOCATION |
			ATTR_VOL_ALLOCATIONCLUMP |
			ATTR_VOL_IOBLOCKSIZE |
			ATTR_VOL_OBJCOUNT |
			ATTR_VOL_FILECOUNT |
			ATTR_VOL_DIRCOUNT |
			ATTR_VOL_MAXOBJCOUNT |
			ATTR_VOL_MOUNTPOINT |
			ATTR_VOL_NAME	|
			ATTR_VOL_MOUNTFLAGS |
			ATTR_VOL_MOUNTEDDEVICE |
			/* ATTR_VOL_ENCODINGSUSED */
			ATTR_VOL_CAPABILITIES |
			ATTR_VOL_ATTRIBUTES;
		fsap->f_attributes.validattr.dirattr =
			ATTR_DIR_LINKCOUNT |
			ATTR_DIR_ENTRYCOUNT |
			ATTR_DIR_MOUNTSTATUS;
		fsap->f_attributes.validattr.fileattr =
			ATTR_FILE_LINKCOUNT |
			ATTR_FILE_TOTALSIZE |
			ATTR_FILE_ALLOCSIZE |
			/* ATTR_FILE_IOBLOCKSIZE */
			ATTR_FILE_DEVTYPE |
			/* ATTR_FILE_FORKCOUNT */
			/* ATTR_FILE_FORKLIST */
			ATTR_FILE_DATALENGTH |
			ATTR_FILE_DATAALLOCSIZE |
			ATTR_FILE_RSRCLENGTH |
			ATTR_FILE_RSRCALLOCSIZE;
		fsap->f_attributes.validattr.forkattr = 0;
		fsap->f_attributes.nativeattr.commonattr =
			ATTR_CMN_NAME	|
			ATTR_CMN_DEVID	|
			ATTR_CMN_FSID	|
			ATTR_CMN_OBJTYPE |
			ATTR_CMN_OBJTAG	|
			ATTR_CMN_OBJID	|
			ATTR_CMN_OBJPERMANENTID |
			ATTR_CMN_PAROBJID |
			/* ATTR_CMN_SCRIPT | */
			ATTR_CMN_CRTIME |
			ATTR_CMN_MODTIME |
			/* ATTR_CMN_CHGTIME | */	/* Supported but not native */
			ATTR_CMN_ACCTIME |
			/* ATTR_CMN_BKUPTIME | */
			/* ATTR_CMN_FNDRINFO | */
			ATTR_CMN_OWNERID | 	/* Supported but not native */
			ATTR_CMN_GRPID	| 	/* Supported but not native */
			ATTR_CMN_ACCESSMASK | 	/* Supported but not native */
			ATTR_CMN_FLAGS	|
			ATTR_CMN_USERACCESS |
			ATTR_CMN_EXTENDED_SECURITY |
			ATTR_CMN_UUID |
			ATTR_CMN_GRPUUID |
			0;
		fsap->f_attributes.nativeattr.volattr =
			ATTR_VOL_FSTYPE	|
			ATTR_VOL_SIGNATURE |
			ATTR_VOL_SIZE	|
			ATTR_VOL_SPACEFREE |
			ATTR_VOL_SPACEAVAIL |
			ATTR_VOL_MINALLOCATION |
			ATTR_VOL_ALLOCATIONCLUMP |
			ATTR_VOL_IOBLOCKSIZE |
			ATTR_VOL_OBJCOUNT |
			ATTR_VOL_FILECOUNT |
			ATTR_VOL_DIRCOUNT |
			ATTR_VOL_MAXOBJCOUNT |
			ATTR_VOL_MOUNTPOINT |
			ATTR_VOL_NAME	|
			ATTR_VOL_MOUNTFLAGS |
			ATTR_VOL_MOUNTEDDEVICE |
			/* ATTR_VOL_ENCODINGSUSED */
			ATTR_VOL_CAPABILITIES |
			ATTR_VOL_ATTRIBUTES;
		fsap->f_attributes.nativeattr.dirattr = 0;
		fsap->f_attributes.nativeattr.fileattr =
			/* ATTR_FILE_LINKCOUNT | */	/* Supported but not native */
			ATTR_FILE_TOTALSIZE |
			ATTR_FILE_ALLOCSIZE |
			/* ATTR_FILE_IOBLOCKSIZE */
			ATTR_FILE_DEVTYPE |
			/* ATTR_FILE_FORKCOUNT */
			/* ATTR_FILE_FORKLIST */
			ATTR_FILE_DATALENGTH |
			ATTR_FILE_DATAALLOCSIZE |
			ATTR_FILE_RSRCLENGTH |
			ATTR_FILE_RSRCALLOCSIZE;
		fsap->f_attributes.nativeattr.forkattr = 0;

		VFSATTR_SET_SUPPORTED(fsap, f_attributes);
	}
	if (VFSATTR_IS_ACTIVE(fsap, f_create_time)) {
		char osname[MAXNAMELEN];
		uint64_t value;

		// Get dataset name
		dmu_objset_name(zfsvfs->z_os, osname);
		dsl_prop_get_integer(osname, "CREATION",
							 &value, NULL);
		fsap->f_create_time.tv_sec  = value;
		fsap->f_create_time.tv_nsec = 0;
		VFSATTR_SET_SUPPORTED(fsap, f_create_time);
	}
	if (VFSATTR_IS_ACTIVE(fsap, f_modify_time)) {
        timestruc_t  now;
        uint64_t        mtime[2];

        gethrestime(&now);
        ZFS_TIME_ENCODE(&now, mtime);
        //fsap->f_modify_time = mtime;
        ZFS_TIME_DECODE(&fsap->f_modify_time, mtime);

		VFSATTR_SET_SUPPORTED(fsap, f_modify_time);
	}
	/*
	 * For Carbon compatibility, pretend to support this legacy/unused
	 * attribute
	 */
	if (VFSATTR_IS_ACTIVE(fsap, f_backup_time)) {
		fsap->f_backup_time.tv_sec = 0;
		fsap->f_backup_time.tv_nsec = 0;
		VFSATTR_SET_SUPPORTED(fsap, f_backup_time);
	}

	if (VFSATTR_IS_ACTIVE(fsap, f_vol_name)) {
		char osname[MAXNAMELEN], *slash;
		dmu_objset_name(zfsvfs->z_os, osname);

		slash = strrchr(osname, '/');
		if (slash) {
			/* Advance past last slash */
			slash += 1;
		} else {
			/* Copy whole osname (pool root) */
			slash = osname;
		}
		strlcpy(fsap->f_vol_name, slash, MAXPATHLEN);

#if 0
		/*
		 * Finder volume name is set to the basename of the mountpoint path,
		 * unless the mountpoint path is "/" or NULL, in which case we use
		 * the f_mntfromname, such as "MyPool/mydataset"
		 */
		/* Get last path component of mnt 'on' */
		char *volname = strrchr(vfs_statfs(zfsvfs->z_vfs)->f_mntonname, '/');
		if (volname && (*(&volname[1]) != '\0')) {
			strlcpy(fsap->f_vol_name, &volname[1], MAXPATHLEN);
		} else {
			/* Get last path component of mnt 'from' */
			volname = strrchr(vfs_statfs(zfsvfs->z_vfs)->f_mntfromname, '/');
			if (volname && (*(&volname[1]) != '\0')) {
				strlcpy(fsap->f_vol_name, &volname[1], MAXPATHLEN);
			} else {
				strlcpy(fsap->f_vol_name,
				    vfs_statfs(zfsvfs->z_vfs)->f_mntfromname,
				    MAXPATHLEN);
			}
		}
#endif

		VFSATTR_SET_SUPPORTED(fsap, f_vol_name);
		dprintf("vfs_getattr: volume name '%s'\n", fsap->f_vol_name);
	}
/*
	if (!zfsvfs->z_issnap) {
		VFSATTR_RETURN(fsap, f_fssubtype, 0);
	} else {
		VFSATTR_RETURN(fsap, f_fssubtype, 2);
	}
*/

	/* If we are mimicing, we need to let userland know we are really ZFS */
	VFSATTR_RETURN(fsap, f_fssubtype, MNTTYPE_ZFS_SUBTYPE);

    /* According to joshade over at
     * https://github.com/joshado/liberate-applefileserver/blob/master/liberate.m
     * the following values need to be returned for it to be considered
     * by Apple's AFS.
     */
	VFSATTR_RETURN(fsap, f_signature, 18475);  /*  */
	VFSATTR_RETURN(fsap, f_carbon_fsid, 0);
    // Make up a UUID here, based on the name
	if (VFSATTR_IS_ACTIVE(fsap, f_uuid)) {

		char osname[MAXNAMELEN];
		int error;

#if 0
		uint64_t pguid = 0;
		uint64_t guid = 0;
		spa_t *spa = 0;

		if (zfsvfs->z_os == NULL ||
		    (guid = dmu_objset_fsid_guid(zfsvfs->z_os)) == 0ULL ||
		    (spa = dmu_objset_spa(zfsvfs->z_os)) == NULL ||
		    (pguid = spa_guid(spa)) == 0ULL) {
			dprintf("%s couldn't get pguid or guid %llu %llu\n",
			    __func__, pguid, guid);
		}

		if (guid != 0ULL && pguid != 0ULL) {
			/*
			 * Seeding with the pool guid would
			 * also avoid clashes across pools
			 */
			/* Print 16 hex chars (8b guid), plus null char */
			/* snprintf puts null char */
			snprintf(osname, 33, "%016llx%016llx", pguid, guid);
			osname[32] = '\0'; /* just in case */
			dprintf("%s: using pguid+guid [%s]\n", __func__, osname);
		} else {
	/* XXX */
#endif
			// Get dataset name
			dmu_objset_name(zfsvfs->z_os, osname);
			dprintf("%s: osname [%s]\n", __func__, osname);
#if 0
	/* XXX */
		}
#endif
		if ((error = zfs_vfs_uuid_gen(osname,
		    fsap->f_uuid)) != 0) {
			dprintf("%s uuid_gen error %d\n", __func__, error);
		} else {
			/* return f_uuid in fsap */
			VFSATTR_SET_SUPPORTED(fsap, f_uuid);
		}
	}

	uint64_t missing = 0;
	missing = (fsap->f_active ^ (fsap->f_active & fsap->f_supported));
	if ( missing != 0) {
		dprintf("vfs_getattr:: asked %08llx replied %08llx       missing %08llx\n",
			   fsap->f_active, fsap->f_supported,
			   missing);
	}

	ZFS_EXIT(zfsvfs);
#endif
	return (0);
}

int
zfs_vnode_lock(vnode_t *vp, int flags)
{
	int error;

	ASSERT(vp != NULL);

	error = vn_lock(vp, flags);
	return (error);
}


#if !defined(HAVE_SPLIT_SHRINKER_CALLBACK) && !defined(HAVE_SHRINK) && \
	defined(HAVE_D_PRUNE_ALIASES)
/*
 * Linux kernels older than 3.1 do not support a per-filesystem shrinker.
 * To accommodate this we must improvise and manually walk the list of znodes
 * attempting to prune dentries in order to be able to drop the inodes.
 *
 * To avoid scanning the same znodes multiple times they are always rotated
 * to the end of the z_all_znodes list.  New znodes are inserted at the
 * end of the list so we're always scanning the oldest znodes first.
 */
static int
zfs_sb_prune_aliases(zfs_sb_t *zsb, unsigned long nr_to_scan)
{
	znode_t **zp_array, *zp;
	int max_array = MIN(nr_to_scan, PAGE_SIZE * 8 / sizeof (znode_t *));
	int objects = 0;
	int i = 0, j = 0;

	zp_array = kmem_zalloc(max_array * sizeof (znode_t *), KM_SLEEP);

	mutex_enter(&zsb->z_znodes_lock);
	while ((zp = list_head(&zsb->z_all_znodes)) != NULL) {

		if ((i++ > nr_to_scan) || (j >= max_array))
			break;

		ASSERT(list_link_active(&zp->z_link_node));
		list_remove(&zsb->z_all_znodes, zp);
		list_insert_tail(&zsb->z_all_znodes, zp);

		/* Skip active znodes and .zfs entries */
		if (MUTEX_HELD(&zp->z_lock) || zp->z_is_ctldir)
			continue;

		if (igrab(ZTOI(zp)) == NULL)
			continue;

		zp_array[j] = zp;
		j++;
	}
	mutex_exit(&zsb->z_znodes_lock);

	for (i = 0; i < j; i++) {
		zp = zp_array[i];

		ASSERT3P(zp, !=, NULL);
		d_prune_aliases(ZTOI(zp));

		if (atomic_read(&ZTOI(zp)->i_count) == 1)
			objects++;

		iput(ZTOI(zp));
	}

	kmem_free(zp_array, max_array * sizeof (znode_t *));

	return (objects);
}
#endif /* HAVE_D_PRUNE_ALIASES */
/*
 * The ARC has requested that the filesystem drop entries from the dentry
 * and inode caches.  This can occur when the ARC needs to free meta data
 * blocks but can't because they are all pinned by entries in these caches.
 */

/* Get vnode for the root object of this mount */
int
zfs_vfs_root(struct mount *mp, vnode_t **vpp,  vfs_context_t *context)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	znode_t *rootzp;
	int error;

	if (!zfsvfs) return EIO;

	ZFS_ENTER_NOERROR(zfsvfs);

	error = zfs_zget(zfsvfs, zfsvfs->z_root, &rootzp);
	if (error == 0)
		*vpp = ZTOV(rootzp);

	ZFS_EXIT(zfsvfs);

#if 0
	if (error == 0) {
		error = zfs_vnode_lock(*vpp, 0);
		if (error == 0)
			(*vpp)->v_vflag |= VV_ROOT;
	}
#endif
	if (error != 0)
		*vpp = NULL;

	/* zfs_vfs_mountroot() can be called first, and we need to stop
	 * getrootdir() from being called until root is mounted. XNU calls
	 * vfs_start() once that is done, then VFS_ROOT(&rootvnode) to set
	 * the rootvnode, so we inform SPL that getrootdir() is ready to
	 * be called. We only need to call this one, worth optimising?
	 */
	spl_vfs_start();

	return (error);
}

/*
 * Teardown the zfsvfs::z_os.
 *
 * Note, if 'unmounting' is FALSE, we return with the 'z_teardown_lock'
 * and 'z_teardown_inactive_lock' held.
 */
static int
zfsvfs_teardown(zfsvfs_t *zfsvfs, boolean_t unmounting)
{
	znode_t	*zp;
   /*
     * We have experienced deadlocks with dmu_recv_end happening between
     * suspend_fs() and resume_fs(). Clearly something is not quite ready
     * so we will wait for pools to be synced first.
     * This is considered a temporary solution until we can work out
     * the full issue.
     */

	zfs_unlinked_drain_stop_wait(zfsvfs);

	/*
	 * If someone has not already unmounted this file system,
	 * drain the iput_taskq to ensure all active references to the
	 * zfs_sb_t have been handled only then can it be safely destroyed.
	 */
	if (zfsvfs->z_os)
		taskq_wait(dsl_pool_vnrele_taskq(dmu_objset_pool(zfsvfs->z_os)));

	rrm_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);

	if (!unmounting) {
		/*
		 * We purge the parent filesystem's vfsp as the parent
		 * filesystem and all of its snapshots have their vnode's
		 * v_vfsp set to the parent's filesystem's vfsp.  Note,
		 * 'z_parent' is self referential for non-snapshots.
		 */
		(void) dnlc_purge_vfsp(zfsvfs->z_parent->z_vfs, 0);
#ifdef FREEBSD_NAMECACHE
		cache_purgevfs(zfsvfs->z_parent->z_vfs);
#endif
	}

	/*
	 * Close the zil. NB: Can't close the zil while zfs_inactive
	 * threads are blocked as zil_close can call zfs_inactive.
	 */
	if (zfsvfs->z_log) {
		zil_close(zfsvfs->z_log);
		zfsvfs->z_log = NULL;
	}

	rw_enter(&zfsvfs->z_teardown_inactive_lock, RW_WRITER);

	/*
	 * If we are not unmounting (ie: online recv) and someone already
	 * unmounted this file system while we were doing the switcheroo,
	 * or a reopen of z_os failed then just bail out now.
	 */
	if (!unmounting && (zfsvfs->z_unmounted || zfsvfs->z_os == NULL)) {
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
		return (SET_ERROR(EIO));
	}
	/*
	 * At this point there are no VFS ops active, and any new VFS ops
	 * will fail with EIO since we have z_teardown_lock for writer (only
	 * relevant for forced unmount).
	 *
	 * Release all holds on dbufs.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp != NULL;
	    zp = list_next(&zfsvfs->z_all_znodes, zp))
		if (zp->z_sa_hdl) {
			/* ASSERT(ZTOV(zp)->v_count >= 0); */
			zfs_znode_dmu_fini(zp);
		}
	mutex_exit(&zfsvfs->z_znodes_lock);

	/*
	 * If we are unmounting, set the unmounted flag and let new VFS ops
	 * unblock.  zfs_inactive will have the unmounted behavior, and all
	 * other VFS ops will fail with EIO.
	 */
	if (unmounting) {
		zfsvfs->z_unmounted = B_TRUE;
		rw_exit(&zfsvfs->z_teardown_inactive_lock);
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
	}

	/*
	 * z_os will be NULL if there was an error in attempting to reopen
	 * zfsvfs, so just return as the properties had already been
	 * unregistered and cached data had been evicted before.
	 */
	if (zfsvfs->z_os == NULL)
		return (0);

	/*
	 * Unregister properties.
	 */
	zfs_unregister_callbacks(zfsvfs);

	/*
	 * Evict cached data
	 */
	if (dsl_dataset_is_dirty(dmu_objset_ds(zfsvfs->z_os)) &&
	    !(vfs_isrdonly(zfsvfs->z_vfs)))
		txg_wait_synced(dmu_objset_pool(zfsvfs->z_os), 0);
	dmu_objset_evict_dbufs(zfsvfs->z_os);

    dprintf("-teardown\n");
	return (0);
}


int
zfs_vfs_unmount(struct mount *mp, int mntflags, vfs_context_t *context)
{
	dprintf("%s\n", __func__);
    zfsvfs_t *zfsvfs = vfs_fsprivate(mp);

	//kthread_t *td = (kthread_t *)curthread;
	objset_t *os;
#ifndef _WIN32
	cred_t *cr =  (cred_t *)vfs_context_ucred(context);
#endif
	int ret;

    dprintf("+unmount\n");

	zfs_unlinked_drain_stop_wait(zfsvfs);

	/*
	 * We might skip the sync called in the unmount path, since
	 * zfs_vfs_sync() is generally ignoring xnu's calls, and alas,
	 * mount_isforce() is set AFTER that sync call, so we can not
	 * detect unmount is inflight. But why not just sync now, it
	 * is safe. Optionally, sync if (mount_isforce());
	 */
	spa_sync_allpools();

#ifndef _WIN32
	/*XXX NOEL: delegation admin stuffs, add back if we use delg. admin */
	ret = secpolicy_fs_unmount(cr, zfsvfs->z_vfs);
	if (ret) {
		if (dsl_deleg_access((char *)refstr_value(vfsp->vfs_resource),
		    ZFS_DELEG_PERM_MOUNT, cr))
			return (ret);
	}
#endif
	/*
	 * We purge the parent filesystem's vfsp as the parent filesystem
	 * and all of its snapshots have their vnode's v_vfsp set to the
	 * parent's filesystem's vfsp.  Note, 'z_parent' is self
	 * referential for non-snapshots.
	 */
	(void) dnlc_purge_vfsp(zfsvfs->z_parent->z_vfs, 0);

	/*
	 * Unmount any snapshots mounted under .zfs before unmounting the
	 * dataset itself.
	 */
    dprintf("z_ctldir check: %p\n",zfsvfs->z_ctldir );
	if (zfsvfs->z_ctldir != NULL) {

#ifndef _WIN32
		// We can not unmount from kernel space, and there is a known
		// race causing panic
		if ((ret = zfsctl_umount_snapshots(zfsvfs->z_vfs, 0 /*fflag*/, cr)) != 0)
			return (ret);
#endif
        dprintf("vflush 1\n");
        ret = vflush(zfsvfs->z_vfs, zfsvfs->z_ctldir, (mntflags & MNT_FORCE) ? FORCECLOSE : 0|SKIPSYSTEM);
		//ret = vflush(zfsvfs->z_vfs, NULLVP, 0);
		//ASSERT(ret == EBUSY);
		if (!(mntflags & MNT_FORCE)) {
			if (vnode_isinuse(zfsvfs->z_ctldir, 1)) {
                dprintf("zfsctl vp still in use %p\n", zfsvfs->z_ctldir);
				return (EBUSY);
            }
			//ASSERT(zfsvfs->z_ctldir->v_count == 1);
		}
        dprintf("z_ctldir destroy\n");
#ifndef _WIN32
		zfsctl_destroy(zfsvfs);
#endif
		ASSERT(zfsvfs->z_ctldir == NULL);
	}

#if 0
    // If we are ourselves a snapshot
	if (dmu_objset_is_snapshot(zfsvfs->z_os)) {
        struct vnode *vp;
        printf("We are unmounting a snapshot\n");
        vp = vfs_vnodecovered(zfsvfs->z_vfs);
        if (vp) {
            struct vnop_inactive_args ap;
            ap.a_vp = vp;
            printf(".. telling gfs layer\n");
            gfs_dir_inactive(&ap);
            printf("..and put\n");
            vnode_put(vp);
        }
    }
#endif

	ret = vflush(mp, NULLVP, SKIPSYSTEM);

	if (mntflags & MNT_FORCE) {
		/*
		 * Mark file system as unmounted before calling
		 * vflush(FORCECLOSE). This way we ensure no future vnops
		 * will be called and risk operating on DOOMED vnodes.
		 */
		rrm_enter(&zfsvfs->z_teardown_lock, RW_WRITER, FTAG);
		zfsvfs->z_unmounted = B_TRUE;
		rrm_exit(&zfsvfs->z_teardown_lock, FTAG);
	}

	/*
	 * Flush all the files.
	 */
	ret = vflush(mp, NULLVP, (mntflags & MNT_FORCE) ? FORCECLOSE|SKIPSYSTEM : SKIPSYSTEM);

	if ((ret != 0) && !(mntflags & MNT_FORCE)) {
		if (!zfsvfs->z_issnap) {
#ifndef _WIN32
			zfsctl_create(zfsvfs);
#endif
			//ASSERT(zfsvfs->z_ctldir != NULL);
		}
		return (ret);
	}

#ifdef __APPLE__
	if (!vfs_isrdonly(zfsvfs->z_vfs) &&
		spa_writeable(dmu_objset_spa(zfsvfs->z_os)) &&
		!(mntflags & MNT_FORCE)) {
			/* Update the last-unmount time for Spotlight's next mount */
			char osname[MAXNAMELEN];
			timestruc_t  now;
			dmu_tx_t *tx;
			int error;
			uint64_t value;

			dmu_objset_name(zfsvfs->z_os, osname);
			dprintf("ZFS: '%s' Updating spotlight LASTUNMOUNT property\n",
				osname);

			gethrestime(&now);
			zfsvfs->z_last_unmount_time = now.tv_sec;

			tx = dmu_tx_create(zfsvfs->z_os);
			dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, TRUE, NULL);
			error = dmu_tx_assign(tx, TXG_WAIT);
			if (error) {
                dmu_tx_abort(tx);
			} else {
				value = zfsvfs->z_last_unmount_time;
				error = zap_update(zfsvfs->z_os, MASTER_NODE_OBJ,
								   zfs_prop_to_name(ZFS_PROP_APPLE_LASTUNMOUNT),
								   8, 1,
								   &value, tx);
				dmu_tx_commit(tx);
			}
			dprintf("ZFS: '%s' set lastunmount to 0x%lx (%d)\n",
					osname, zfsvfs->z_last_unmount_time, error);
		}

#endif

#ifdef sun
	if (!(fflag & MS_FORCE)) {
		/*
		 * Check the number of active vnodes in the file system.
		 * Our count is maintained in the vfs structure, but the
		 * number is off by 1 to indicate a hold on the vfs
		 * structure itself.
		 *
		 * The '.zfs' directory maintains a reference of its
		 * own, and any active references underneath are
		 * reflected in the vnode count.
		 */
		if (zfsvfs->z_ctldir == NULL) {
			if (vfsp->vfs_count > 1)
				return (EBUSY);
		} else {
			if (vfsp->vfs_count > 2 ||
			    zfsvfs->z_ctldir->v_count > 1)
				return (EBUSY);
		}
	}
#endif

	/*
	 * Last chance to dump unreferenced system files.
	 */
	(void) vflush(mp, NULLVP, FORCECLOSE);

	VERIFY(zfsvfs_teardown(zfsvfs, B_TRUE) == 0);
	os = zfsvfs->z_os;

    dprintf("OS %p\n", os);
	/*
	 * z_os will be NULL if there was an error in
	 * attempting to reopen zfsvfs.
	 */
	if (os != NULL) {
		/*
		 * Unset the objset user_ptr.
		 */
		mutex_enter(&os->os_user_ptr_lock);
        dprintf("mutex\n");
		dmu_objset_set_user(os, NULL);
        dprintf("set\n");
		mutex_exit(&os->os_user_ptr_lock);

		/*
		 * Finally release the objset
		 */
        dprintf("disown\n");
		dmu_objset_disown(os, B_TRUE, zfsvfs);
	}

    dprintf("OS released\n");

	/*
	 * We can now safely destroy the '.zfs' directory node.
	 */
#ifndef _WIN32
	if (zfsvfs->z_ctldir != NULL)
		zfsctl_destroy(zfsvfs);
#endif
#if 0
	if (zfsvfs->z_issnap) {
		vnode_t *svp = vfsp->mnt_vnodecovered;

		if (svp->v_count >= 2)
			VN_RELE(svp);
	}
#endif

    dprintf("freevfs\n");
	zfs_freevfs(zfsvfs->z_vfs);

    dprintf("-unmount\n");

	return (0);
}

static int
zfs_vget_internal(zfsvfs_t *zfsvfs, ino64_t ino, vnode_t **vpp)
{
	znode_t		*zp;
	int 		err = 0;
#if 0
    dprintf("vget get %llu\n", ino);
	/*
	 * zfs_zget() can't operate on virtual entries like .zfs/ or
	 * .zfs/snapshot/ directories, that's why we return EOPNOTSUPP.
	 * This will make NFS to switch to LOOKUP instead of using VGET.
	 */
	if (ino == ZFSCTL_INO_ROOT || ino == ZFSCTL_INO_SNAPDIR ||
	    (zfsvfs->z_shares_dir != 0 && ino == zfsvfs->z_shares_dir))
		return (EOPNOTSUPP);


	/*
	 * Check to see if we expect to find this in the hardlink avl tree of
	 * hashes. Use the MSB set high as indicator.
	 */
	hardlinks_t *findnode = NULL;
	if ((1ULL<<31) & ino) {
		hardlinks_t *searchnode;
		avl_index_t loc;

		searchnode = kmem_alloc(sizeof(hardlinks_t), KM_SLEEP);

		dprintf("ZFS: vget looking for (%llx,%llu)\n", ino, ino);

		searchnode->hl_linkid = ino;

		rw_enter(&zfsvfs->z_hardlinks_lock, RW_READER);
		findnode = avl_find(&zfsvfs->z_hardlinks_linkid, searchnode, &loc);
		rw_exit(&zfsvfs->z_hardlinks_lock);

		kmem_free(searchnode, sizeof(hardlinks_t));

		if (findnode) {
			dprintf("ZFS: vget found (%llu, %llu, %u): '%s'\n",
				   findnode->hl_parent,
				   findnode->hl_fileid, findnode->hl_linkid,
				   findnode->hl_name);
			// Lookup the actual zp instead
			ino = findnode->hl_fileid;
		} // findnode
	} // MSB set


	/* We can not be locked during zget. */
	if (!ino) {
		dprintf("%s: setting ino from %lld to 2\n", __func__, ino);
		ino = 2;
	}

	err = zfs_zget(zfsvfs, ino, &zp);

    if (err) {
        dprintf("zget failed %d\n", err);
        return err;
    }

	/* Don't expose EA objects! */
	if (zp->z_pflags & ZFS_XATTR) {
		err = ENOENT;
        goto out;
	}
	if (zp->z_unlinked) {
		err = EINVAL;
        goto out;
	}

    *vpp = ZTOV(zp);

    err = zfs_vnode_lock(*vpp, 0/*flags*/);

	/*
	 * Spotlight requires that vap->va_name() is set when returning
	 * from vfs_vget, so that vfs_getrealpath() can succeed in returning
	 * a path to mds.
	 */
	char *name = kmem_alloc(MAXPATHLEN + 2, KM_SLEEP);

	/* Root can't lookup in ZAP */
	if (zp->z_id == zfsvfs->z_root) {

		dmu_objset_name(zfsvfs->z_os, name);
		dprintf("vget: set root '%s'\n", name);
		vnode_update_identity(*vpp, NULL, name,
							  strlen(name), 0,
							  VNODE_UPDATE_NAME);

	} else {
		uint64_t parent;

		// if its a hardlink cache
		if (findnode) {

			dprintf("vget: updating vnode to '%s' and parent %llu\n",
				   findnode->hl_name, findnode->hl_parent);

			vnode_update_identity(*vpp,
								  NULL,
								  findnode->hl_name,
								  strlen(findnode->hl_name),
								  0,
								  VNODE_UPDATE_NAME|VNODE_UPDATE_PARENT);
			mutex_enter(&zp->z_lock);
			strlcpy(zp->z_name_cache, findnode->hl_name, PATH_MAX);
			zp->z_finder_parentid = findnode->hl_parent;
			mutex_exit(&zp->z_lock);


		// If we already have the name, cached in zfs_vnop_lookup
		} else if (zp->z_name_cache[0]) {
			dprintf("vget: cached name '%s'\n", zp->z_name_cache);
			vnode_update_identity(*vpp, NULL, zp->z_name_cache,
								  strlen(zp->z_name_cache), 0,
								  VNODE_UPDATE_NAME);

			/* If needed, if findnode is set, we can update the parentid too */

		} else {

			/* Lookup name from ID, grab parent */
			VERIFY(sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
							 &parent, sizeof (parent)) == 0);

			if (zap_value_search(zfsvfs->z_os, parent, zp->z_id,
								 ZFS_DIRENT_OBJ(-1ULL), name) == 0) {

				dprintf("vget: set name '%s'\n", name);
				vnode_update_identity(*vpp, NULL, name,
									  strlen(name), 0,
									  VNODE_UPDATE_NAME);
			} else {
				dprintf("vget: unable to get name for %llu\n", zp->z_id);
			} // !zap_search
		}
	} // rootid

	kmem_free(name, MAXPATHLEN + 2);

 out:
    /*
     * We do not release the vp here in vget, if we do, we panic with io_count
     * != 1
     *
     * VN_RELE(ZTOV(zp));
     */
	if (err != 0) {
		VN_RELE(ZTOV(zp));
		*vpp = NULL;
	}

    dprintf("vget return %d\n", err);
#endif
	return (err);
}

#ifdef _WIN32
/*
 * Get a vnode from a file id (ignoring the generation)
 *
 * Use by NFS Server (readdirplus) and VFS (build_path)
 */
int
zfs_vfs_vget(struct mount *mp, ino64_t ino, vnode_t **vpp,  vfs_context_t *context)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	int error;

	ZFS_ENTER(zfsvfs);

	/*
	 * On Mac OS X we always export the root directory id as 2.
	 * So we don't expect to see the real root directory id
	 * from zfs_vfs_vget KPI (unless of course the real id was
	 * already 2).
	 */
	if (ino == 2) ino = zfsvfs->z_root;

	if ((ino == zfsvfs->z_root) && (zfsvfs->z_root != 2)) {
		error = VFS_ROOT(mp, 0, vpp);
		ZFS_EXIT(zfsvfs);
		return (error);
	}

	error = zfs_vget_internal(zfsvfs, ino, vpp);

	ZFS_EXIT(zfsvfs);
	return (error);
}
#endif /* _WIN32 */


#ifndef _WIN32
static int
zfs_checkexp(vfs_t *vfsp, struct sockaddr *nam, int *extflagsp,
    struct ucred **credanonp, int *numsecflavors, int **secflavors)
{
	zfsvfs_t *zfsvfs = vfsp->vfs_data;

	/*
	 * If this is regular file system vfsp is the same as
	 * zfsvfs->z_parent->z_vfs, but if it is snapshot,
	 * zfsvfs->z_parent->z_vfs represents parent file system
	 * which we have to use here, because only this file system
	 * has mnt_export configured.
	 */
	return (vfs_stdcheckexp(zfsvfs->z_parent->z_vfs, nam, extflagsp,
	    credanonp, numsecflavors, secflavors));
}

CTASSERT(SHORT_FID_LEN <= sizeof(struct fid));
CTASSERT(LONG_FID_LEN <= sizeof(struct fid));

#endif

#ifdef _WIN32

int
zfs_vfs_setattr( struct mount *mp,  struct vfs_attr *fsap,  vfs_context_t *context)
{
	// 10a286 bits has an implementation of this
	return (ENOTSUP);
}

/*
 * NFS Server File Handle File ID
 */
typedef struct zfs_zfid {
	uint8_t   zf_object[8];		/* obj[i] = obj >> (8 * i) */
	uint8_t   zf_gen[8];		/* gen[i] = gen >> (8 * i) */
} zfs_zfid_t;
#endif /* _WIN32 */

#ifdef _WIN32
/*
 * File handle to vnode pointer
 */
int
zfs_vfs_fhtovp(struct mount *mp, int fhlen, unsigned char *fhp,
               vnode_t **vpp,  vfs_context_t *context)
{
dprintf("%s\n", __func__);
	zfsvfs_t *zfsvfs = vfs_fsprivate(mp);
	zfs_zfid_t	*zfid = (zfs_zfid_t *)fhp;
	znode_t		*zp;
	uint64_t	obj_num = 0;
	uint64_t	fid_gen = 0;
	uint64_t	zp_gen;
	int 		i;
	int		error;

	*vpp = NULL;

	ZFS_ENTER(zfsvfs);

	if (fhlen < sizeof (zfs_zfid_t)) {
		error = EINVAL;
		goto out;
	}

	/*
	 * Grab the object and gen numbers in an endian neutral manner
	 */
	for (i = 0; i < sizeof (zfid->zf_object); i++)
		obj_num |= ((uint64_t)zfid->zf_object[i]) << (8 * i);

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		fid_gen |= ((uint64_t)zfid->zf_gen[i]) << (8 * i);

	if ((error = zfs_zget(zfsvfs, obj_num, &zp))) {
		goto out;
	}

	zp_gen = zp->z_gen;
	if (zp_gen == 0)
		zp_gen = 1;

	if (zp->z_unlinked || zp_gen != fid_gen) {
		VN_RELE(ZTOV(zp));
		error = EINVAL;
		goto out;
	}
	*vpp = ZTOV(zp);
out:
	ZFS_EXIT(zfsvfs);
	return (error);
}
#endif //_WIN32

#ifdef _WIN32
/*
 * Vnode pointer to File handle
 *
 * XXX Do we want to check the DSL sharenfs property?
 */
int
zfs_vfs_vptofh(vnode_t *vp, int *fhlenp, unsigned char *fhp,  vfs_context_t *context)
{
dprintf("%s\n", __func__);
#if 0
zfsvfs_t	*zfsvfs = vfs_fsprivate(vnode_mount(vp));
	zfs_zfid_t	*zfid = (zfs_zfid_t *)fhp;
	znode_t		*zp = VTOZ(vp);
	uint64_t	obj_num;
	uint64_t	zp_gen;
	int		i;
	//int		error;

	if (*fhlenp < sizeof (zfs_zfid_t)) {
		return (EOVERFLOW);
	}

	ZFS_ENTER(zfsvfs);

	obj_num = zp->z_id;
	zp_gen = zp->z_gen;
	if (zp_gen == 0)
		zp_gen = 1;

	/*
	 * Store the object and gen numbers in an endian neutral manner
	 */
	for (i = 0; i < sizeof (zfid->zf_object); i++)
		zfid->zf_object[i] = (uint8_t)(obj_num >> (8 * i));

	for (i = 0; i < sizeof (zfid->zf_gen); i++)
		zfid->zf_gen[i] = (uint8_t)(zp_gen >> (8 * i));

	*fhlenp = sizeof (zfs_zfid_t);

	ZFS_EXIT(zfsvfs);
#endif
	return (0);
}
#endif /* _WIN32 */


/*
 * Block out VOPs and close zfsvfs_t::z_os
 *
 * Note, if successful, then we return with the 'z_teardown_lock' and
 * 'z_teardown_inactive_lock' write held.  We leave ownership of the underlying
 * dataset and objset intact so that they can be atomically handed off during
 * a subsequent rollback or recv operation and the resume thereafter.
 */
int
zfs_suspend_fs(zfsvfs_t *zfsvfs)
{
	int error;

	if ((error = zfsvfs_teardown(zfsvfs, B_FALSE)) != 0)
		return (error);

	return (0);
}

/*
 * Reopen zfsvfs_t::z_os and release VOPs.
 */
int
zfs_resume_fs(zfsvfs_t *zfsvfs, dsl_dataset_t *ds)
{
	int err, err2;
	znode_t *zp;

	ASSERT(RRM_WRITE_HELD(&zfsvfs->z_teardown_lock));
	ASSERT(RW_WRITE_HELD(&zfsvfs->z_teardown_inactive_lock));

	/*
	 * We already own this, so just update the objset_t, as the one we
	 * had before may have been evicted.
	 */
	objset_t *os;
	VERIFY3P(ds->ds_owner, ==, zfsvfs);
	VERIFY(dsl_dataset_long_held(ds));
	VERIFY0(dmu_objset_from_ds(ds, &os));

	err = zfsvfs_init(zfsvfs, os);
	if (err != 0)
		goto bail;

    VERIFY(zfsvfs_setup(zfsvfs, B_FALSE) == 0);

	zfs_set_fuid_feature(zfsvfs);
	//zfsvfs->z_rollback_time = jiffies;

	/*
	 * Attempt to re-establish all the active inodes with their
	 * dbufs.  If a zfs_rezget() fails, then we unhash the inode
	 * and mark it stale.  This prevents a collision if a new
	 * inode/object is created which must use the same inode
	 * number.  The stale inode will be be released when the
	 * VFS prunes the dentry holding the remaining references
	 * on the stale inode.
	 */
	mutex_enter(&zfsvfs->z_znodes_lock);
	for (zp = list_head(&zfsvfs->z_all_znodes); zp;
	    zp = list_next(&zfsvfs->z_all_znodes, zp)) {
		err2 = zfs_rezget(zp);
		if (err2) {
			//remove_inode_hash(ZTOI(zp));
			zp->z_is_stale = B_TRUE;
		}
	}
	mutex_exit(&zfsvfs->z_znodes_lock);

	if (!vfs_isrdonly(zfsvfs->z_vfs) && !zfsvfs->z_unmounted) {
		/*
		 * zfs_suspend_fs() could have interrupted freeing
		 * of dnodes. We need to restart this freeing so
		 * that we don't "leak" the space.
		 */
		zfs_unlinked_drain(zfsvfs);
	}

bail:
	/* release the VFS ops */
	rw_exit(&zfsvfs->z_teardown_inactive_lock);
	rrm_exit(&zfsvfs->z_teardown_lock, FTAG);

	if (err) {
		/*
		 * Since we couldn't setup the sa framework, try to force
		 * unmount this file system.
		 */
#ifndef _WIN32
		if (zfsvfs->z_os)
			(void) zfs_umount(zfsvfs->z_sb);
#endif
	}
	return (err);
}


void
zfs_freevfs(struct mount *vfsp)
{
	zfsvfs_t *zfsvfs = vfs_fsprivate(vfsp);

    dprintf("+freevfs\n");

#ifdef sun
	/*
	 * If this is a snapshot, we have an extra VFS_HOLD on our parent
	 * from zfs_mount().  Release it here.  If we came through
	 * zfs_mountroot() instead, we didn't grab an extra hold, so
	 * skip the VFS_RELE for rootvfs.
	 */
	if (zfsvfs->z_issnap && (vfsp != rootvfs))
		VFS_RELE(zfsvfs->z_parent->z_vfs);
#endif	/* sun */

	vfs_setfsprivate(vfsp, NULL);

	zfsvfs_free(zfsvfs);

	atomic_dec_32(&zfs_active_fs_count);
    dprintf("-freevfs\n");
}

#ifdef __i386__
static int desiredvnodes_backup;
#endif

static void
zfs_vnodes_adjust(void)
{
    // What is this?
#ifdef __i386XXX__
	int newdesiredvnodes;

	desiredvnodes_backup = desiredvnodes;

	/*
	 * We calculate newdesiredvnodes the same way it is done in
	 * vntblinit(). If it is equal to desiredvnodes, it means that
	 * it wasn't tuned by the administrator and we can tune it down.
	 */
	newdesiredvnodes = min(maxproc + cnt.v_page_count / 4, 2 *
	    vm_kmem_size / (5 * (sizeof(struct vm_object) +
	    sizeof(struct vnode))));
	if (newdesiredvnodes == desiredvnodes)
		desiredvnodes = (3 * newdesiredvnodes) / 4;
#endif
}

static void
zfs_vnodes_adjust_back(void)
{

#ifdef __i386XXX__
	desiredvnodes = desiredvnodes_backup;
#endif
}

void
zfs_init(void)
{

	dprintf("ZFS filesystem version: " ZPL_VERSION_STRING "\n");

	/*
	 * Initialize .zfs directory structures
	 */
//	zfsctl_init();

	/*
	 * Initialize znode cache, vnode ops, etc...
	 */
	zfs_znode_init();

	/*
	 * Reduce number of vnodes. Originally number of vnodes is calculated
	 * with UFS inode in mind. We reduce it here, because it's too big for
	 * ZFS/i386.
	 */
	zfs_vnodes_adjust();

	dmu_objset_register_type(DMU_OST_ZFS, zfs_space_delta_cb);
}

void
zfs_fini(void)
{
//	zfsctl_fini();
	zfs_znode_fini();
	zfs_vnodes_adjust_back();
}

int
zfs_busy(void)
{
	return (zfs_active_fs_count != 0);
}

int
zfs_set_version(zfsvfs_t *zfsvfs, uint64_t newvers)
{
	int error;
	objset_t *os = zfsvfs->z_os;
	dmu_tx_t *tx;

	if (newvers < ZPL_VERSION_INITIAL || newvers > ZPL_VERSION)
		return (SET_ERROR(EINVAL));

	if (newvers < zfsvfs->z_version)
		return (SET_ERROR(EINVAL));

	if (zfs_spa_version_map(newvers) >
	    spa_version(dmu_objset_spa(zfsvfs->z_os)))
		return (SET_ERROR(ENOTSUP));

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, ZPL_VERSION_STR);
	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE,
		    ZFS_SA_ATTRS);
		dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	}
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
	    8, 1, &newvers, tx);

	if (error) {
		dmu_tx_commit(tx);
		return (error);
	}

	if (newvers >= ZPL_VERSION_SA && !zfsvfs->z_use_sa) {
		uint64_t sa_obj;

		ASSERT3U(spa_version(dmu_objset_spa(zfsvfs->z_os)), >=,
		    SPA_VERSION_SA);
		sa_obj = zap_create(os, DMU_OT_SA_MASTER_NODE,
		    DMU_OT_NONE, 0, tx);

		error = zap_add(os, MASTER_NODE_OBJ,
		    ZFS_SA_ATTRS, 8, 1, &sa_obj, tx);
		ASSERT(error==0);

		VERIFY(0 == sa_set_sa_object(os, sa_obj));
		sa_register_update_callback(os, zfs_sa_upgrade);
	}

	spa_history_log_internal(dmu_objset_spa(os), "upgrade", tx,
	    "oldver=%llu newver=%llu dataset = %llu", zfsvfs->z_version, newvers,
	    dmu_objset_id(os));

	dmu_tx_commit(tx);

	zfsvfs->z_version = newvers;
	os->os_version = newvers;

	zfs_set_fuid_feature(zfsvfs);

	return (0);
}

/*
 * Read a property stored within the master node.
 */
int
zfs_get_zplprop(objset_t *os, zfs_prop_t prop, uint64_t *value)
{
	uint64_t *cached_copy = NULL;

	/*
	 * Figure out where in the objset_t the cached copy would live, if it
	 * is available for the requested property.
	 */
	if (os != NULL) {
		switch (prop) {
			case ZFS_PROP_VERSION:
				cached_copy = &os->os_version;
				break;
			case ZFS_PROP_NORMALIZE:
				cached_copy = &os->os_normalization;
				break;
			case ZFS_PROP_UTF8ONLY:
				cached_copy = &os->os_utf8only;
				break;
			case ZFS_PROP_CASE:
				cached_copy = &os->os_casesensitivity;
				break;
			default:
				break;
		}
	}
	if (cached_copy != NULL && *cached_copy != OBJSET_PROP_UNINITIALIZED) {
		*value = *cached_copy;
		return (0);
	}

	/*
	 * If the property wasn't cached, look up the file system's value for
	 * the property. For the version property, we look up a slightly
	 * different string.
	 */
	const char *pname;
	int error = ENOENT;
	if (prop == ZFS_PROP_VERSION) {
		pname = ZPL_VERSION_STR;
	} else {
		pname = zfs_prop_to_name(prop);
	}

	if (os != NULL) {
		ASSERT3U(os->os_phys->os_type, ==, DMU_OST_ZFS);
		error = zap_lookup(os, MASTER_NODE_OBJ, pname, 8, 1, value);
	}

	if (error == ENOENT) {
		/* No value set, use the default value */
		switch (prop) {
		case ZFS_PROP_VERSION:
			*value = ZPL_VERSION;
			break;
		case ZFS_PROP_NORMALIZE:
		case ZFS_PROP_UTF8ONLY:
			*value = 0;
			break;
		case ZFS_PROP_CASE:
			*value = ZFS_CASE_SENSITIVE;
			break;
		case ZFS_PROP_ACLMODE:
			*value = ZFS_ACLTYPE_OFF;
			break;
		default:
			return (error);
		}
		error = 0;
	}

	/*
	 * If one of the methods for getting the property value above worked,
	 * copy it into the objset_t's cache.
	 */
	if (error == 0 && cached_copy != NULL) {
		*cached_copy = *value;
	}

	return (error);
}

/*
 * Return true if the coresponding vfs's unmounted flag is set.
 * Otherwise return false.
 * If this function returns true we know VFS unmount has been initiated.
 */
boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{
	zfsvfs_t *zfvp;
	boolean_t unmounted = B_FALSE;

	ASSERT(dmu_objset_type(os) == DMU_OST_ZFS);

	mutex_enter(&os->os_user_ptr_lock);
	zfvp = dmu_objset_get_user(os);
	if (zfvp != NULL && zfvp->z_vfs != NULL &&
		(vfs_isunmount(zfvp->z_vfs)))
		unmounted = B_TRUE;
	mutex_exit(&os->os_user_ptr_lock);

	return (unmounted);
}

#ifdef _KERNEL
void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
#if 0
	char tmpbuf[MAXPATHLEN];
	struct mount *mp;
	char *fromname;
	size_t oldlen;

	oldlen = strlen(oldname);

	mtx_lock(&mountlist_mtx);
	TAILQ_FOREACH(mp, &mountlist, mnt_list) {
		fromname = mp->mnt_stat.f_mntfromname;
		if (strcmp(fromname, oldname) == 0) {
			(void)strlcpy(fromname, newname,
			    sizeof(mp->mnt_stat.f_mntfromname));
			continue;
		}
		if (strncmp(fromname, oldname, oldlen) == 0 &&
		    (fromname[oldlen] == '/' || fromname[oldlen] == '@')) {
			(void)snprintf(tmpbuf, sizeof(tmpbuf), "%s%s",
			    newname, fromname + oldlen);
			(void)strlcpy(fromname, tmpbuf,
			    sizeof(mp->mnt_stat.f_mntfromname));
			continue;
		}
	}
	mtx_unlock(&mountlist_mtx);
#endif
}

#endif
