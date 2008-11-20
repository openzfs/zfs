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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)zvol.c	1.31	08/04/09 SMI"

/*
 * ZFS volume emulation driver.
 *
 * Makes a DMU object look like a volume of arbitrary size, up to 2^64 bytes.
 * Volumes are accessed through the symbolic links named:
 *
 * /dev/zvol/dsk/<pool_name>/<dataset_name>
 * /dev/zvol/rdsk/<pool_name>/<dataset_name>
 *
 * These links are created by the ZFS-specific devfsadm link generator.
 * Volumes are persistent through reboot.  No user command needs to be
 * run before opening and using a device.
 */

#include <sys/types.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/modctl.h>
#include <sys/open.h>
#include <sys/kmem.h>
#include <sys/conf.h>
#include <sys/cmn_err.h>
#include <sys/stat.h>
#include <sys/zap.h>
#include <sys/spa.h>
#include <sys/zio.h>
#include <sys/dmu_traverse.h>
#include <sys/dnode.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_prop.h>
#include <sys/dkio.h>
#include <sys/efi_partition.h>
#include <sys/byteorder.h>
#include <sys/pathname.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
#include <sys/crc32.h>
#include <sys/dirent.h>
#include <sys/policy.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/mkdev.h>
#include <sys/zil.h>
#include <sys/refcount.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_rlock.h>
#include <sys/vdev_disk.h>
#include <sys/vdev_impl.h>
#include <sys/zvol.h>
#include <sys/dumphdr.h>

#include "zfs_namecheck.h"

static void *zvol_state;

#define	ZVOL_DUMPSIZE		"dumpsize"

/*
 * This lock protects the zvol_state structure from being modified
 * while it's being used, e.g. an open that comes in before a create
 * finishes.  It also protects temporary opens of the dataset so that,
 * e.g., an open doesn't get a spurious EBUSY.
 */
static kmutex_t zvol_state_lock;
static uint32_t zvol_minors;

#define	NUM_EXTENTS	((SPA_MAXBLOCKSIZE) / sizeof (zvol_extent_t))

typedef struct zvol_extent {
	dva_t		ze_dva;		/* dva associated with this extent */
	uint64_t	ze_stride;	/* extent stride */
	uint64_t	ze_size;	/* number of blocks in extent */
} zvol_extent_t;

/*
 * The list of extents associated with the dump device
 */
typedef struct zvol_ext_list {
	zvol_extent_t		zl_extents[NUM_EXTENTS];
	struct zvol_ext_list	*zl_next;
} zvol_ext_list_t;

/*
 * The in-core state of each volume.
 */
typedef struct zvol_state {
	char		zv_name[MAXPATHLEN]; /* pool/dd name */
	uint64_t	zv_volsize;	/* amount of space we advertise */
	uint64_t	zv_volblocksize; /* volume block size */
	minor_t		zv_minor;	/* minor number */
	uint8_t		zv_min_bs;	/* minimum addressable block shift */
	uint8_t		zv_flags;	/* readonly; dumpified */
	objset_t	*zv_objset;	/* objset handle */
	uint32_t	zv_mode;	/* DS_MODE_* flags at open time */
	uint32_t	zv_open_count[OTYPCNT];	/* open counts */
	uint32_t	zv_total_opens;	/* total open count */
	zilog_t		*zv_zilog;	/* ZIL handle */
	zvol_ext_list_t	*zv_list;	/* List of extents for dump */
	uint64_t	zv_txg_assign;	/* txg to assign during ZIL replay */
	znode_t		zv_znode;	/* for range locking */
} zvol_state_t;

/*
 * zvol specific flags
 */
#define	ZVOL_RDONLY	0x1
#define	ZVOL_DUMPIFIED	0x2

/*
 * zvol maximum transfer in one DMU tx.
 */
int zvol_maxphys = DMU_MAX_ACCESS/2;

extern int zfs_set_prop_nvlist(const char *, nvlist_t *);
static int zvol_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio);
static int zvol_dumpify(zvol_state_t *zv);
static int zvol_dump_fini(zvol_state_t *zv);
static int zvol_dump_init(zvol_state_t *zv, boolean_t resize);

static void
zvol_size_changed(zvol_state_t *zv, major_t maj)
{
	dev_t dev = makedevice(maj, zv->zv_minor);

	VERIFY(ddi_prop_update_int64(dev, zfs_dip,
	    "Size", zv->zv_volsize) == DDI_SUCCESS);
	VERIFY(ddi_prop_update_int64(dev, zfs_dip,
	    "Nblocks", lbtodb(zv->zv_volsize)) == DDI_SUCCESS);

	/* Notify specfs to invalidate the cached size */
	spec_size_invalidate(dev, VBLK);
	spec_size_invalidate(dev, VCHR);
}

int
zvol_check_volsize(uint64_t volsize, uint64_t blocksize)
{
	if (volsize == 0)
		return (EINVAL);

	if (volsize % blocksize != 0)
		return (EINVAL);

#ifdef _ILP32
	if (volsize - 1 > SPEC_MAXOFFSET_T)
		return (EOVERFLOW);
#endif
	return (0);
}

int
zvol_check_volblocksize(uint64_t volblocksize)
{
	if (volblocksize < SPA_MINBLOCKSIZE ||
	    volblocksize > SPA_MAXBLOCKSIZE ||
	    !ISP2(volblocksize))
		return (EDOM);

	return (0);
}

static void
zvol_readonly_changed_cb(void *arg, uint64_t newval)
{
	zvol_state_t *zv = arg;

	if (newval)
		zv->zv_flags |= ZVOL_RDONLY;
	else
		zv->zv_flags &= ~ZVOL_RDONLY;
}

int
zvol_get_stats(objset_t *os, nvlist_t *nv)
{
	int error;
	dmu_object_info_t doi;
	uint64_t val;


	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &val);
	if (error)
		return (error);

	dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLSIZE, val);

	error = dmu_object_info(os, ZVOL_OBJ, &doi);

	if (error == 0) {
		dsl_prop_nvlist_add_uint64(nv, ZFS_PROP_VOLBLOCKSIZE,
		    doi.doi_data_block_size);
	}

	return (error);
}

/*
 * Find a free minor number.
 */
static minor_t
zvol_minor_alloc(void)
{
	minor_t minor;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	for (minor = 1; minor <= ZVOL_MAX_MINOR; minor++)
		if (ddi_get_soft_state(zvol_state, minor) == NULL)
			return (minor);

	return (0);
}

static zvol_state_t *
zvol_minor_lookup(const char *name)
{
	minor_t minor;
	zvol_state_t *zv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	for (minor = 1; minor <= ZVOL_MAX_MINOR; minor++) {
		zv = ddi_get_soft_state(zvol_state, minor);
		if (zv == NULL)
			continue;
		if (strcmp(zv->zv_name, name) == 0)
			break;
	}

	return (zv);
}

void
zvol_init_extent(zvol_extent_t *ze, blkptr_t *bp)
{
	ze->ze_dva = bp->blk_dva[0];	/* structure assignment */
	ze->ze_stride = 0;
	ze->ze_size = 1;
}

/* extent mapping arg */
struct maparg {
	zvol_ext_list_t	*ma_list;
	zvol_extent_t	*ma_extent;
	int		ma_gang;
};

/*ARGSUSED*/
static int
zvol_map_block(traverse_blk_cache_t *bc, spa_t *spa, void *arg)
{
	zbookmark_t *zb = &bc->bc_bookmark;
	blkptr_t *bp = &bc->bc_blkptr;
	void *data = bc->bc_data;
	dnode_phys_t *dnp = bc->bc_dnode;
	struct maparg *ma = (struct maparg *)arg;
	uint64_t stride;

	/* If there is an error, then keep trying to make progress */
	if (bc->bc_errno)
		return (ERESTART);

#ifdef ZFS_DEBUG
	if (zb->zb_level == -1) {
		ASSERT3U(BP_GET_TYPE(bp), ==, DMU_OT_OBJSET);
		ASSERT3U(BP_GET_LEVEL(bp), ==, 0);
	} else {
		ASSERT3U(BP_GET_TYPE(bp), ==, dnp->dn_type);
		ASSERT3U(BP_GET_LEVEL(bp), ==, zb->zb_level);
	}

	if (zb->zb_level > 0) {
		uint64_t fill = 0;
		blkptr_t *bpx, *bpend;

		for (bpx = data, bpend = bpx + BP_GET_LSIZE(bp) / sizeof (*bpx);
		    bpx < bpend; bpx++) {
			if (bpx->blk_birth != 0) {
				fill += bpx->blk_fill;
			} else {
				ASSERT(bpx->blk_fill == 0);
			}
		}
		ASSERT3U(fill, ==, bp->blk_fill);
	}

	if (zb->zb_level == 0 && dnp->dn_type == DMU_OT_DNODE) {
		uint64_t fill = 0;
		dnode_phys_t *dnx, *dnend;

		for (dnx = data, dnend = dnx + (BP_GET_LSIZE(bp)>>DNODE_SHIFT);
		    dnx < dnend; dnx++) {
			if (dnx->dn_type != DMU_OT_NONE)
				fill++;
		}
		ASSERT3U(fill, ==, bp->blk_fill);
	}
#endif

	if (zb->zb_level || dnp->dn_type == DMU_OT_DNODE)
		return (0);

	/* Abort immediately if we have encountered gang blocks */
	if (BP_IS_GANG(bp)) {
		ma->ma_gang++;
		return (EINTR);
	}

	/* first time? */
	if (ma->ma_extent->ze_size == 0) {
		zvol_init_extent(ma->ma_extent, bp);
		return (0);
	}

	stride = (DVA_GET_OFFSET(&bp->blk_dva[0])) -
	    ((DVA_GET_OFFSET(&ma->ma_extent->ze_dva)) +
	    (ma->ma_extent->ze_size - 1) * (ma->ma_extent->ze_stride));
	if (DVA_GET_VDEV(BP_IDENTITY(bp)) ==
	    DVA_GET_VDEV(&ma->ma_extent->ze_dva)) {
		if (ma->ma_extent->ze_stride == 0) {
			/* second block in this extent */
			ma->ma_extent->ze_stride = stride;
			ma->ma_extent->ze_size++;
			return (0);
		} else if (ma->ma_extent->ze_stride == stride) {
			/*
			 * the block we allocated has the same
			 * stride
			 */
			ma->ma_extent->ze_size++;
			return (0);
		}
	}

	/*
	 * dtrace -n 'zfs-dprintf
	 * /stringof(arg0) == "zvol.c"/
	 * {
	 *	printf("%s: %s", stringof(arg1), stringof(arg3))
	 * } '
	 */
	dprintf("ma_extent 0x%lx mrstride 0x%lx stride %lx\n",
	    ma->ma_extent->ze_size, ma->ma_extent->ze_stride, stride);
	dprintf_bp(bp, "%s", "next blkptr:");
	/* start a new extent */
	if (ma->ma_extent == &ma->ma_list->zl_extents[NUM_EXTENTS - 1]) {
		ma->ma_list->zl_next = kmem_zalloc(sizeof (zvol_ext_list_t),
		    KM_SLEEP);
		ma->ma_list = ma->ma_list->zl_next;
		ma->ma_extent = &ma->ma_list->zl_extents[0];
	} else {
		ma->ma_extent++;
	}
	zvol_init_extent(ma->ma_extent, bp);
	return (0);
}

/* ARGSUSED */
void
zvol_create_cb(objset_t *os, void *arg, cred_t *cr, dmu_tx_t *tx)
{
	zfs_creat_t *zct = arg;
	nvlist_t *nvprops = zct->zct_props;
	int error;
	uint64_t volblocksize, volsize;

	VERIFY(nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE), &volsize) == 0);
	if (nvlist_lookup_uint64(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE), &volblocksize) != 0)
		volblocksize = zfs_prop_default_numeric(ZFS_PROP_VOLBLOCKSIZE);

	/*
	 * These properties must be removed from the list so the generic
	 * property setting step won't apply to them.
	 */
	VERIFY(nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLSIZE)) == 0);
	(void) nvlist_remove_all(nvprops,
	    zfs_prop_to_name(ZFS_PROP_VOLBLOCKSIZE));

	error = dmu_object_claim(os, ZVOL_OBJ, DMU_OT_ZVOL, volblocksize,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_create_claim(os, ZVOL_ZAP_OBJ, DMU_OT_ZVOL_PROP,
	    DMU_OT_NONE, 0, tx);
	ASSERT(error == 0);

	error = zap_update(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize, tx);
	ASSERT(error == 0);
}

/*
 * Replay a TX_WRITE ZIL transaction that didn't get committed
 * after a system failure
 */
static int
zvol_replay_write(zvol_state_t *zv, lr_write_t *lr, boolean_t byteswap)
{
	objset_t *os = zv->zv_objset;
	char *data = (char *)(lr + 1);	/* data follows lr_write_t */
	uint64_t off = lr->lr_offset;
	uint64_t len = lr->lr_length;
	dmu_tx_t *tx;
	int error;

	if (byteswap)
		byteswap_uint64_array(lr, sizeof (*lr));

	tx = dmu_tx_create(os);
	dmu_tx_hold_write(tx, ZVOL_OBJ, off, len);
	error = dmu_tx_assign(tx, zv->zv_txg_assign);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		dmu_write(os, ZVOL_OBJ, off, len, data, tx);
		dmu_tx_commit(tx);
	}

	return (error);
}

/* ARGSUSED */
static int
zvol_replay_err(zvol_state_t *zv, lr_t *lr, boolean_t byteswap)
{
	return (ENOTSUP);
}

/*
 * Callback vectors for replaying records.
 * Only TX_WRITE is needed for zvol.
 */
zil_replay_func_t *zvol_replay_vector[TX_MAX_TYPE] = {
	zvol_replay_err,	/* 0 no such transaction type */
	zvol_replay_err,	/* TX_CREATE */
	zvol_replay_err,	/* TX_MKDIR */
	zvol_replay_err,	/* TX_MKXATTR */
	zvol_replay_err,	/* TX_SYMLINK */
	zvol_replay_err,	/* TX_REMOVE */
	zvol_replay_err,	/* TX_RMDIR */
	zvol_replay_err,	/* TX_LINK */
	zvol_replay_err,	/* TX_RENAME */
	zvol_replay_write,	/* TX_WRITE */
	zvol_replay_err,	/* TX_TRUNCATE */
	zvol_replay_err,	/* TX_SETATTR */
	zvol_replay_err,	/* TX_ACL */
};

/*
 * reconstruct dva that gets us to the desired offset (offset
 * is in bytes)
 */
int
zvol_get_dva(zvol_state_t *zv, uint64_t offset, dva_t *dva)
{
	zvol_ext_list_t	*zl;
	zvol_extent_t	*ze;
	int		idx;
	uint64_t	tmp;

	if ((zl = zv->zv_list) == NULL)
		return (EIO);
	idx = 0;
	ze =  &zl->zl_extents[0];
	while (offset >= ze->ze_size * zv->zv_volblocksize) {
		offset -= ze->ze_size * zv->zv_volblocksize;

		if (idx == NUM_EXTENTS - 1) {
			/* we've reached the end of this array */
			ASSERT(zl->zl_next != NULL);
			if (zl->zl_next == NULL)
				return (-1);
			zl = zl->zl_next;
			ze = &zl->zl_extents[0];
			idx = 0;
		} else {
			ze++;
			idx++;
		}
	}
	DVA_SET_VDEV(dva, DVA_GET_VDEV(&ze->ze_dva));
	tmp = DVA_GET_OFFSET((&ze->ze_dva));
	tmp += (ze->ze_stride * (offset / zv->zv_volblocksize));
	DVA_SET_OFFSET(dva, tmp);
	return (0);
}

static void
zvol_free_extents(zvol_state_t *zv)
{
	zvol_ext_list_t *zl;
	zvol_ext_list_t *tmp;

	if (zv->zv_list != NULL) {
		zl = zv->zv_list;
		while (zl != NULL) {
			tmp = zl->zl_next;
			kmem_free(zl, sizeof (zvol_ext_list_t));
			zl = tmp;
		}
		zv->zv_list = NULL;
	}
}

int
zvol_get_lbas(zvol_state_t *zv)
{
	struct maparg	ma;
	zvol_ext_list_t	*zl;
	zvol_extent_t	*ze;
	uint64_t	blocks = 0;
	int		err;

	ma.ma_list = zl = kmem_zalloc(sizeof (zvol_ext_list_t), KM_SLEEP);
	ma.ma_extent = &ma.ma_list->zl_extents[0];
	ma.ma_gang = 0;
	zv->zv_list = ma.ma_list;

	err = traverse_zvol(zv->zv_objset, ADVANCE_PRE, zvol_map_block, &ma);
	if (err == EINTR && ma.ma_gang) {
		/*
		 * We currently don't support dump devices when the pool
		 * is so fragmented that our allocation has resulted in
		 * gang blocks.
		 */
		zvol_free_extents(zv);
		return (EFRAGS);
	}
	ASSERT3U(err, ==, 0);

	ze = &zl->zl_extents[0];
	while (ze) {
		blocks += ze->ze_size;
		if (ze == &zl->zl_extents[NUM_EXTENTS - 1]) {
			zl = zl->zl_next;
			ze = &zl->zl_extents[0];
		} else {
			ze++;
		}
	}
	if (blocks != (zv->zv_volsize / zv->zv_volblocksize)) {
		zvol_free_extents(zv);
		return (EIO);
	}

	return (0);
}

/*
 * Create a minor node (plus a whole lot more) for the specified volume.
 */
int
zvol_create_minor(const char *name, major_t maj)
{
	zvol_state_t *zv;
	objset_t *os;
	dmu_object_info_t doi;
	uint64_t volsize;
	minor_t minor = 0;
	struct pathname linkpath;
	int ds_mode = DS_MODE_PRIMARY;
	vnode_t *vp = NULL;
	char *devpath;
	size_t devpathlen = strlen(ZVOL_FULL_DEV_DIR) + strlen(name) + 1;
	char chrbuf[30], blkbuf[30];
	int error;

	mutex_enter(&zvol_state_lock);

	if ((zv = zvol_minor_lookup(name)) != NULL) {
		mutex_exit(&zvol_state_lock);
		return (EEXIST);
	}

	if (strchr(name, '@') != 0)
		ds_mode |= DS_MODE_READONLY;

	error = dmu_objset_open(name, DMU_OST_ZVOL, ds_mode, &os);

	if (error) {
		mutex_exit(&zvol_state_lock);
		return (error);
	}

	error = zap_lookup(os, ZVOL_ZAP_OBJ, "size", 8, 1, &volsize);

	if (error) {
		dmu_objset_close(os);
		mutex_exit(&zvol_state_lock);
		return (error);
	}

	/*
	 * If there's an existing /dev/zvol symlink, try to use the
	 * same minor number we used last time.
	 */
	devpath = kmem_alloc(devpathlen, KM_SLEEP);

	(void) sprintf(devpath, "%s%s", ZVOL_FULL_DEV_DIR, name);

	error = lookupname(devpath, UIO_SYSSPACE, NO_FOLLOW, NULL, &vp);

	kmem_free(devpath, devpathlen);

	if (error == 0 && vp->v_type != VLNK)
		error = EINVAL;

	if (error == 0) {
		pn_alloc(&linkpath);
		error = pn_getsymlink(vp, &linkpath, kcred);
		if (error == 0) {
			char *ms = strstr(linkpath.pn_path, ZVOL_PSEUDO_DEV);
			if (ms != NULL) {
				ms += strlen(ZVOL_PSEUDO_DEV);
				minor = stoi(&ms);
			}
		}
		pn_free(&linkpath);
	}

	if (vp != NULL)
		VN_RELE(vp);

	/*
	 * If we found a minor but it's already in use, we must pick a new one.
	 */
	if (minor != 0 && ddi_get_soft_state(zvol_state, minor) != NULL)
		minor = 0;

	if (minor == 0)
		minor = zvol_minor_alloc();

	if (minor == 0) {
		dmu_objset_close(os);
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}

	if (ddi_soft_state_zalloc(zvol_state, minor) != DDI_SUCCESS) {
		dmu_objset_close(os);
		mutex_exit(&zvol_state_lock);
		return (EAGAIN);
	}

	(void) ddi_prop_update_string(minor, zfs_dip, ZVOL_PROP_NAME,
	    (char *)name);

	(void) sprintf(chrbuf, "%uc,raw", minor);

	if (ddi_create_minor_node(zfs_dip, chrbuf, S_IFCHR,
	    minor, DDI_PSEUDO, 0) == DDI_FAILURE) {
		ddi_soft_state_free(zvol_state, minor);
		dmu_objset_close(os);
		mutex_exit(&zvol_state_lock);
		return (EAGAIN);
	}

	(void) sprintf(blkbuf, "%uc", minor);

	if (ddi_create_minor_node(zfs_dip, blkbuf, S_IFBLK,
	    minor, DDI_PSEUDO, 0) == DDI_FAILURE) {
		ddi_remove_minor_node(zfs_dip, chrbuf);
		ddi_soft_state_free(zvol_state, minor);
		dmu_objset_close(os);
		mutex_exit(&zvol_state_lock);
		return (EAGAIN);
	}

	zv = ddi_get_soft_state(zvol_state, minor);

	(void) strcpy(zv->zv_name, name);
	zv->zv_min_bs = DEV_BSHIFT;
	zv->zv_minor = minor;
	zv->zv_volsize = volsize;
	zv->zv_objset = os;
	zv->zv_mode = ds_mode;
	zv->zv_zilog = zil_open(os, zvol_get_data);
	mutex_init(&zv->zv_znode.z_range_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&zv->zv_znode.z_range_avl, zfs_range_compare,
	    sizeof (rl_t), offsetof(rl_t, r_node));
	/* get and cache the blocksize */
	error = dmu_object_info(os, ZVOL_OBJ, &doi);
	ASSERT(error == 0);
	zv->zv_volblocksize = doi.doi_data_block_size;

	zil_replay(os, zv, &zv->zv_txg_assign, zvol_replay_vector);
	zvol_size_changed(zv, maj);

	/* XXX this should handle the possible i/o error */
	VERIFY(dsl_prop_register(dmu_objset_ds(zv->zv_objset),
	    "readonly", zvol_readonly_changed_cb, zv) == 0);

	zvol_minors++;

	mutex_exit(&zvol_state_lock);

	return (0);
}

/*
 * Remove minor node for the specified volume.
 */
int
zvol_remove_minor(const char *name)
{
	zvol_state_t *zv;
	char namebuf[30];

	mutex_enter(&zvol_state_lock);

	if ((zv = zvol_minor_lookup(name)) == NULL) {
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}

	if (zv->zv_total_opens != 0) {
		mutex_exit(&zvol_state_lock);
		return (EBUSY);
	}

	(void) sprintf(namebuf, "%uc,raw", zv->zv_minor);
	ddi_remove_minor_node(zfs_dip, namebuf);

	(void) sprintf(namebuf, "%uc", zv->zv_minor);
	ddi_remove_minor_node(zfs_dip, namebuf);

	VERIFY(dsl_prop_unregister(dmu_objset_ds(zv->zv_objset),
	    "readonly", zvol_readonly_changed_cb, zv) == 0);

	zil_close(zv->zv_zilog);
	zv->zv_zilog = NULL;
	dmu_objset_close(zv->zv_objset);
	zv->zv_objset = NULL;
	avl_destroy(&zv->zv_znode.z_range_avl);
	mutex_destroy(&zv->zv_znode.z_range_lock);

	ddi_soft_state_free(zvol_state, zv->zv_minor);

	zvol_minors--;

	mutex_exit(&zvol_state_lock);

	return (0);
}

static int
zvol_truncate(zvol_state_t *zv, uint64_t offset, uint64_t size)
{
	dmu_tx_t *tx;
	int error;

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_free(tx, ZVOL_OBJ, offset, size);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}
	error = dmu_free_range(zv->zv_objset, ZVOL_OBJ, offset, size, tx);
	dmu_tx_commit(tx);
	return (0);
}

int
zvol_prealloc(zvol_state_t *zv)
{
	objset_t *os = zv->zv_objset;
	dmu_tx_t *tx;
	void *data;
	uint64_t refd, avail, usedobjs, availobjs;
	uint64_t resid = zv->zv_volsize;
	uint64_t off = 0;

	/* Check the space usage before attempting to allocate the space */
	dmu_objset_space(os, &refd, &avail, &usedobjs, &availobjs);
	if (avail < zv->zv_volsize)
		return (ENOSPC);

	/* Free old extents if they exist */
	zvol_free_extents(zv);

	/* allocate the blocks by writing each one */
	data = kmem_zalloc(SPA_MAXBLOCKSIZE, KM_SLEEP);

	while (resid != 0) {
		int error;
		uint64_t bytes = MIN(resid, SPA_MAXBLOCKSIZE);

		tx = dmu_tx_create(os);
		dmu_tx_hold_write(tx, ZVOL_OBJ, off, bytes);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			kmem_free(data, SPA_MAXBLOCKSIZE);
			(void) zvol_truncate(zv, 0, off);
			return (error);
		}
		dmu_write(os, ZVOL_OBJ, off, bytes, data, tx);
		dmu_tx_commit(tx);
		off += bytes;
		resid -= bytes;
	}
	kmem_free(data, SPA_MAXBLOCKSIZE);
	txg_wait_synced(dmu_objset_pool(os), 0);

	return (0);
}

int
zvol_update_volsize(zvol_state_t *zv, major_t maj, uint64_t volsize)
{
	dmu_tx_t *tx;
	int error;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	dmu_tx_hold_free(tx, ZVOL_OBJ, volsize, DMU_OBJECT_END);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	error = zap_update(zv->zv_objset, ZVOL_ZAP_OBJ, "size", 8, 1,
	    &volsize, tx);
	dmu_tx_commit(tx);

	if (error == 0)
		error = zvol_truncate(zv, volsize, DMU_OBJECT_END);

	if (error == 0) {
		zv->zv_volsize = volsize;
		zvol_size_changed(zv, maj);
	}
	return (error);
}

int
zvol_set_volsize(const char *name, major_t maj, uint64_t volsize)
{
	zvol_state_t *zv;
	int error;
	dmu_object_info_t doi;
	uint64_t old_volsize = 0ULL;

	mutex_enter(&zvol_state_lock);

	if ((zv = zvol_minor_lookup(name)) == NULL) {
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}
	old_volsize = zv->zv_volsize;

	if ((error = dmu_object_info(zv->zv_objset, ZVOL_OBJ, &doi)) != 0 ||
	    (error = zvol_check_volsize(volsize,
	    doi.doi_data_block_size)) != 0) {
		mutex_exit(&zvol_state_lock);
		return (error);
	}

	if (zv->zv_flags & ZVOL_RDONLY || (zv->zv_mode & DS_MODE_READONLY)) {
		mutex_exit(&zvol_state_lock);
		return (EROFS);
	}

	error = zvol_update_volsize(zv, maj, volsize);

	/*
	 * Reinitialize the dump area to the new size. If we
	 * failed to resize the dump area then restore the it back to
	 * it's original size.
	 */
	if (error == 0 && zv->zv_flags & ZVOL_DUMPIFIED) {
		if ((error = zvol_dumpify(zv)) != 0 ||
		    (error = dumpvp_resize()) != 0) {
			(void) zvol_update_volsize(zv, maj, old_volsize);
			error = zvol_dumpify(zv);
		}
	}

	mutex_exit(&zvol_state_lock);

	return (error);
}

int
zvol_set_volblocksize(const char *name, uint64_t volblocksize)
{
	zvol_state_t *zv;
	dmu_tx_t *tx;
	int error;

	mutex_enter(&zvol_state_lock);

	if ((zv = zvol_minor_lookup(name)) == NULL) {
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}
	if (zv->zv_flags & ZVOL_RDONLY || (zv->zv_mode & DS_MODE_READONLY)) {
		mutex_exit(&zvol_state_lock);
		return (EROFS);
	}

	tx = dmu_tx_create(zv->zv_objset);
	dmu_tx_hold_bonus(tx, ZVOL_OBJ);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
	} else {
		error = dmu_object_set_blocksize(zv->zv_objset, ZVOL_OBJ,
		    volblocksize, 0, tx);
		if (error == ENOTSUP)
			error = EBUSY;
		dmu_tx_commit(tx);
	}

	mutex_exit(&zvol_state_lock);

	return (error);
}

/*ARGSUSED*/
int
zvol_open(dev_t *devp, int flag, int otyp, cred_t *cr)
{
	minor_t minor = getminor(*devp);
	zvol_state_t *zv;

	if (minor == 0)			/* This is the control device */
		return (0);

	mutex_enter(&zvol_state_lock);

	zv = ddi_get_soft_state(zvol_state, minor);
	if (zv == NULL) {
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}

	ASSERT(zv->zv_objset != NULL);

	if ((flag & FWRITE) &&
	    (zv->zv_flags & ZVOL_RDONLY || (zv->zv_mode & DS_MODE_READONLY))) {
		mutex_exit(&zvol_state_lock);
		return (EROFS);
	}

	if (zv->zv_open_count[otyp] == 0 || otyp == OTYP_LYR) {
		zv->zv_open_count[otyp]++;
		zv->zv_total_opens++;
	}

	mutex_exit(&zvol_state_lock);

	return (0);
}

/*ARGSUSED*/
int
zvol_close(dev_t dev, int flag, int otyp, cred_t *cr)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;

	if (minor == 0)		/* This is the control device */
		return (0);

	mutex_enter(&zvol_state_lock);

	zv = ddi_get_soft_state(zvol_state, minor);
	if (zv == NULL) {
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}

	/*
	 * The next statement is a workaround for the following DDI bug:
	 * 6343604 specfs race: multiple "last-close" of the same device
	 */
	if (zv->zv_total_opens == 0) {
		mutex_exit(&zvol_state_lock);
		return (0);
	}

	/*
	 * If the open count is zero, this is a spurious close.
	 * That indicates a bug in the kernel / DDI framework.
	 */
	ASSERT(zv->zv_open_count[otyp] != 0);
	ASSERT(zv->zv_total_opens != 0);

	/*
	 * You may get multiple opens, but only one close.
	 */
	zv->zv_open_count[otyp]--;
	zv->zv_total_opens--;

	mutex_exit(&zvol_state_lock);

	return (0);
}

static void
zvol_get_done(dmu_buf_t *db, void *vzgd)
{
	zgd_t *zgd = (zgd_t *)vzgd;
	rl_t *rl = zgd->zgd_rl;

	dmu_buf_rele(db, vzgd);
	zfs_range_unlock(rl);
	zil_add_block(zgd->zgd_zilog, zgd->zgd_bp);
	kmem_free(zgd, sizeof (zgd_t));
}

/*
 * Get data to generate a TX_WRITE intent log record.
 */
static int
zvol_get_data(void *arg, lr_write_t *lr, char *buf, zio_t *zio)
{
	zvol_state_t *zv = arg;
	objset_t *os = zv->zv_objset;
	dmu_buf_t *db;
	rl_t *rl;
	zgd_t *zgd;
	uint64_t boff; 			/* block starting offset */
	int dlen = lr->lr_length;	/* length of user data */
	int error;

	ASSERT(zio);
	ASSERT(dlen != 0);

	/*
	 * Write records come in two flavors: immediate and indirect.
	 * For small writes it's cheaper to store the data with the
	 * log record (immediate); for large writes it's cheaper to
	 * sync the data and get a pointer to it (indirect) so that
	 * we don't have to write the data twice.
	 */
	if (buf != NULL) /* immediate write */
		return (dmu_read(os, ZVOL_OBJ, lr->lr_offset, dlen, buf));

	zgd = (zgd_t *)kmem_alloc(sizeof (zgd_t), KM_SLEEP);
	zgd->zgd_zilog = zv->zv_zilog;
	zgd->zgd_bp = &lr->lr_blkptr;

	/*
	 * Lock the range of the block to ensure that when the data is
	 * written out and its checksum is being calculated that no other
	 * thread can change the block.
	 */
	boff = P2ALIGN_TYPED(lr->lr_offset, zv->zv_volblocksize, uint64_t);
	rl = zfs_range_lock(&zv->zv_znode, boff, zv->zv_volblocksize,
	    RL_READER);
	zgd->zgd_rl = rl;

	VERIFY(0 == dmu_buf_hold(os, ZVOL_OBJ, lr->lr_offset, zgd, &db));
	error = dmu_sync(zio, db, &lr->lr_blkptr,
	    lr->lr_common.lrc_txg, zvol_get_done, zgd);
	if (error == 0)
		zil_add_block(zv->zv_zilog, &lr->lr_blkptr);
	/*
	 * If we get EINPROGRESS, then we need to wait for a
	 * write IO initiated by dmu_sync() to complete before
	 * we can release this dbuf.  We will finish everything
	 * up in the zvol_get_done() callback.
	 */
	if (error == EINPROGRESS)
		return (0);
	dmu_buf_rele(db, zgd);
	zfs_range_unlock(rl);
	kmem_free(zgd, sizeof (zgd_t));
	return (error);
}

/*
 * zvol_log_write() handles synchronous writes using TX_WRITE ZIL transactions.
 *
 * We store data in the log buffers if it's small enough.
 * Otherwise we will later flush the data out via dmu_sync().
 */
ssize_t zvol_immediate_write_sz = 32768;

static void
zvol_log_write(zvol_state_t *zv, dmu_tx_t *tx, offset_t off, ssize_t len)
{
	uint32_t blocksize = zv->zv_volblocksize;
	lr_write_t *lr;

	while (len) {
		ssize_t nbytes = MIN(len, blocksize - P2PHASE(off, blocksize));
		itx_t *itx = zil_itx_create(TX_WRITE, sizeof (*lr));

		itx->itx_wr_state =
		    len > zvol_immediate_write_sz ?  WR_INDIRECT : WR_NEED_COPY;
		itx->itx_private = zv;
		lr = (lr_write_t *)&itx->itx_lr;
		lr->lr_foid = ZVOL_OBJ;
		lr->lr_offset = off;
		lr->lr_length = nbytes;
		lr->lr_blkoff = off - P2ALIGN_TYPED(off, blocksize, uint64_t);
		BP_ZERO(&lr->lr_blkptr);

		(void) zil_itx_assign(zv->zv_zilog, itx, tx);
		len -= nbytes;
		off += nbytes;
	}
}

int
zvol_dumpio(vdev_t *vd, uint64_t size, uint64_t offset, void *addr,
    int bflags, int isdump)
{
	vdev_disk_t *dvd;
	int direction;
	int c;
	int numerrors = 0;

	for (c = 0; c < vd->vdev_children; c++) {
		if (zvol_dumpio(vd->vdev_child[c], size, offset,
		    addr, bflags, isdump) != 0) {
			numerrors++;
		} else if (bflags & B_READ) {
			break;
		}
	}

	if (!vd->vdev_ops->vdev_op_leaf)
		return (numerrors < vd->vdev_children ? 0 : EIO);

	if (!vdev_writeable(vd))
		return (EIO);

	dvd = vd->vdev_tsd;
	ASSERT3P(dvd, !=, NULL);
	direction = bflags & (B_WRITE | B_READ);
	ASSERT(ISP2(direction));
	offset += VDEV_LABEL_START_SIZE;

	if (ddi_in_panic() || isdump) {
		if (direction & B_READ)
			return (EIO);
		return (ldi_dump(dvd->vd_lh, addr, lbtodb(offset),
		    lbtodb(size)));
	} else {
		return (vdev_disk_physio(dvd->vd_lh, addr, size, offset,
		    direction));
	}
}

int
zvol_physio(zvol_state_t *zv, int bflags, uint64_t off,
    uint64_t size, void *addr, int isdump)
{
	dva_t dva;
	vdev_t *vd;
	int error;
	spa_t *spa = dmu_objset_spa(zv->zv_objset);

	ASSERT(size <= zv->zv_volblocksize);

	/* restrict requests to multiples of the system block size */
	if (P2PHASE(off, DEV_BSIZE) || P2PHASE(size, DEV_BSIZE))
		return (EINVAL);

	if (zvol_get_dva(zv, off, &dva) != 0)
		return (EIO);

	spa_config_enter(spa, RW_READER, FTAG);
	vd = vdev_lookup_top(spa, DVA_GET_VDEV(&dva));

	error = zvol_dumpio(vd, size,
	    DVA_GET_OFFSET(&dva) + (off % zv->zv_volblocksize),
	    addr, bflags & (B_READ | B_WRITE | B_PHYS), isdump);

	spa_config_exit(spa, FTAG);
	return (error);
}

int
zvol_strategy(buf_t *bp)
{
	zvol_state_t *zv = ddi_get_soft_state(zvol_state, getminor(bp->b_edev));
	uint64_t off, volsize;
	size_t size, resid;
	char *addr;
	objset_t *os;
	rl_t *rl;
	int error = 0;
	boolean_t reading, is_dump = zv->zv_flags & ZVOL_DUMPIFIED;

	if (zv == NULL) {
		bioerror(bp, ENXIO);
		biodone(bp);
		return (0);
	}

	if (getminor(bp->b_edev) == 0) {
		bioerror(bp, EINVAL);
		biodone(bp);
		return (0);
	}

	if (!(bp->b_flags & B_READ) &&
	    (zv->zv_flags & ZVOL_RDONLY ||
	    zv->zv_mode & DS_MODE_READONLY)) {
		bioerror(bp, EROFS);
		biodone(bp);
		return (0);
	}

	off = ldbtob(bp->b_blkno);
	volsize = zv->zv_volsize;

	os = zv->zv_objset;
	ASSERT(os != NULL);

	bp_mapin(bp);
	addr = bp->b_un.b_addr;
	resid = bp->b_bcount;

	/*
	 * There must be no buffer changes when doing a dmu_sync() because
	 * we can't change the data whilst calculating the checksum.
	 */
	reading = bp->b_flags & B_READ;
	rl = zfs_range_lock(&zv->zv_znode, off, resid,
	    reading ? RL_READER : RL_WRITER);

	if (resid > volsize - off)	/* don't write past the end */
		resid = volsize - off;

	while (resid != 0 && off < volsize) {

		size = MIN(resid, zvol_maxphys);
		if (is_dump) {
			/* can't straddle a block boundary */
			size = MIN(size, P2END(off, zv->zv_volblocksize) - off);
			error = zvol_physio(zv, bp->b_flags, off, size,
			    addr, 0);
		} else if (reading) {
			error = dmu_read(os, ZVOL_OBJ, off, size, addr);
		} else {
			dmu_tx_t *tx = dmu_tx_create(os);
			dmu_tx_hold_write(tx, ZVOL_OBJ, off, size);
			error = dmu_tx_assign(tx, TXG_WAIT);
			if (error) {
				dmu_tx_abort(tx);
			} else {
				dmu_write(os, ZVOL_OBJ, off, size, addr, tx);
				zvol_log_write(zv, tx, off, size);
				dmu_tx_commit(tx);
			}
		}
		if (error)
			break;
		off += size;
		addr += size;
		resid -= size;
	}
	zfs_range_unlock(rl);

	if ((bp->b_resid = resid) == bp->b_bcount)
		bioerror(bp, off > volsize ? EINVAL : error);

	if (!(bp->b_flags & B_ASYNC) && !reading && !zil_disable && !is_dump)
		zil_commit(zv->zv_zilog, UINT64_MAX, ZVOL_OBJ);
	biodone(bp);

	return (0);
}

/*
 * Set the buffer count to the zvol maximum transfer.
 * Using our own routine instead of the default minphys()
 * means that for larger writes we write bigger buffers on X86
 * (128K instead of 56K) and flush the disk write cache less often
 * (every zvol_maxphys - currently 1MB) instead of minphys (currently
 * 56K on X86 and 128K on sparc).
 */
void
zvol_minphys(struct buf *bp)
{
	if (bp->b_bcount > zvol_maxphys)
		bp->b_bcount = zvol_maxphys;
}

int
zvol_dump(dev_t dev, caddr_t addr, daddr_t blkno, int nblocks)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	int error = 0;
	uint64_t size;
	uint64_t boff;
	uint64_t resid;

	if (minor == 0)			/* This is the control device */
		return (ENXIO);

	zv = ddi_get_soft_state(zvol_state, minor);
	if (zv == NULL)
		return (ENXIO);

	boff = ldbtob(blkno);
	resid = ldbtob(nblocks);
	if (boff + resid > zv->zv_volsize) {
		/* dump should know better than to write here */
		ASSERT(blkno + resid <= zv->zv_volsize);
		return (EIO);
	}
	while (resid) {
		/* can't straddle a block boundary */
		size = MIN(resid, P2END(boff, zv->zv_volblocksize) - boff);

		error = zvol_physio(zv, B_WRITE, boff, size, addr, 1);
		if (error)
			break;
		boff += size;
		addr += size;
		resid -= size;
	}

	return (error);
}

/*ARGSUSED*/
int
zvol_read(dev_t dev, uio_t *uio, cred_t *cr)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	rl_t *rl;
	int error = 0;

	if (minor == 0)			/* This is the control device */
		return (ENXIO);

	zv = ddi_get_soft_state(zvol_state, minor);
	if (zv == NULL)
		return (ENXIO);

	rl = zfs_range_lock(&zv->zv_znode, uio->uio_loffset, uio->uio_resid,
	    RL_READER);
	while (uio->uio_resid > 0) {
		uint64_t bytes = MIN(uio->uio_resid, DMU_MAX_ACCESS >> 1);

		error =  dmu_read_uio(zv->zv_objset, ZVOL_OBJ, uio, bytes);
		if (error)
			break;
	}
	zfs_range_unlock(rl);
	return (error);
}

/*ARGSUSED*/
int
zvol_write(dev_t dev, uio_t *uio, cred_t *cr)
{
	minor_t minor = getminor(dev);
	zvol_state_t *zv;
	rl_t *rl;
	int error = 0;

	if (minor == 0)			/* This is the control device */
		return (ENXIO);

	zv = ddi_get_soft_state(zvol_state, minor);
	if (zv == NULL)
		return (ENXIO);

	if (zv->zv_flags & ZVOL_DUMPIFIED) {
		error = physio(zvol_strategy, NULL, dev, B_WRITE,
		    zvol_minphys, uio);
		return (error);
	}

	rl = zfs_range_lock(&zv->zv_znode, uio->uio_loffset, uio->uio_resid,
	    RL_WRITER);
	while (uio->uio_resid > 0) {
		uint64_t bytes = MIN(uio->uio_resid, DMU_MAX_ACCESS >> 1);
		uint64_t off = uio->uio_loffset;

		dmu_tx_t *tx = dmu_tx_create(zv->zv_objset);
		dmu_tx_hold_write(tx, ZVOL_OBJ, off, bytes);
		error = dmu_tx_assign(tx, TXG_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			break;
		}
		error = dmu_write_uio(zv->zv_objset, ZVOL_OBJ, uio, bytes, tx);
		if (error == 0)
			zvol_log_write(zv, tx, off, bytes);
		dmu_tx_commit(tx);

		if (error)
			break;
	}
	zfs_range_unlock(rl);
	return (error);
}

/*
 * Dirtbag ioctls to support mkfs(1M) for UFS filesystems.  See dkio(7I).
 */
/*ARGSUSED*/
int
zvol_ioctl(dev_t dev, int cmd, intptr_t arg, int flag, cred_t *cr, int *rvalp)
{
	zvol_state_t *zv;
	struct dk_cinfo dki;
	struct dk_minfo dkm;
	dk_efi_t efi;
	struct dk_callback *dkc;
	struct uuid uuid = EFI_RESERVED;
	uint32_t crc;
	int error = 0;
	rl_t *rl;

	mutex_enter(&zvol_state_lock);

	zv = ddi_get_soft_state(zvol_state, getminor(dev));

	if (zv == NULL) {
		mutex_exit(&zvol_state_lock);
		return (ENXIO);
	}

	switch (cmd) {

	case DKIOCINFO:
		bzero(&dki, sizeof (dki));
		(void) strcpy(dki.dki_cname, "zvol");
		(void) strcpy(dki.dki_dname, "zvol");
		dki.dki_ctype = DKC_UNKNOWN;
		dki.dki_maxtransfer = 1 << (SPA_MAXBLOCKSHIFT - zv->zv_min_bs);
		mutex_exit(&zvol_state_lock);
		if (ddi_copyout(&dki, (void *)arg, sizeof (dki), flag))
			error = EFAULT;
		return (error);

	case DKIOCGMEDIAINFO:
		bzero(&dkm, sizeof (dkm));
		dkm.dki_lbsize = 1U << zv->zv_min_bs;
		dkm.dki_capacity = zv->zv_volsize >> zv->zv_min_bs;
		dkm.dki_media_type = DK_UNKNOWN;
		mutex_exit(&zvol_state_lock);
		if (ddi_copyout(&dkm, (void *)arg, sizeof (dkm), flag))
			error = EFAULT;
		return (error);

	case DKIOCGETEFI:
		if (ddi_copyin((void *)arg, &efi, sizeof (dk_efi_t), flag)) {
			mutex_exit(&zvol_state_lock);
			return (EFAULT);
		}
		efi.dki_data = (void *)(uintptr_t)efi.dki_data_64;

		/*
		 * Some clients may attempt to request a PMBR for the
		 * zvol.  Currently this interface will return ENOTTY to
		 * such requests.  These requests could be supported by
		 * adding a check for lba == 0 and consing up an appropriate
		 * PMBR.
		 */
		if (efi.dki_lba == 1) {
			efi_gpt_t gpt;
			efi_gpe_t gpe;

			bzero(&gpt, sizeof (gpt));
			bzero(&gpe, sizeof (gpe));

			if (efi.dki_length < sizeof (gpt)) {
				mutex_exit(&zvol_state_lock);
				return (EINVAL);
			}

			gpt.efi_gpt_Signature = LE_64(EFI_SIGNATURE);
			gpt.efi_gpt_Revision = LE_32(EFI_VERSION_CURRENT);
			gpt.efi_gpt_HeaderSize = LE_32(sizeof (gpt));
			gpt.efi_gpt_FirstUsableLBA = LE_64(34ULL);
			gpt.efi_gpt_LastUsableLBA =
			    LE_64((zv->zv_volsize >> zv->zv_min_bs) - 1);
			gpt.efi_gpt_NumberOfPartitionEntries = LE_32(1);
			gpt.efi_gpt_PartitionEntryLBA = LE_64(2ULL);
			gpt.efi_gpt_SizeOfPartitionEntry = LE_32(sizeof (gpe));

			UUID_LE_CONVERT(gpe.efi_gpe_PartitionTypeGUID, uuid);
			gpe.efi_gpe_StartingLBA = gpt.efi_gpt_FirstUsableLBA;
			gpe.efi_gpe_EndingLBA = gpt.efi_gpt_LastUsableLBA;

			CRC32(crc, &gpe, sizeof (gpe), -1U, crc32_table);
			gpt.efi_gpt_PartitionEntryArrayCRC32 = LE_32(~crc);

			CRC32(crc, &gpt, sizeof (gpt), -1U, crc32_table);
			gpt.efi_gpt_HeaderCRC32 = LE_32(~crc);

			mutex_exit(&zvol_state_lock);
			if (ddi_copyout(&gpt, efi.dki_data, sizeof (gpt), flag))
				error = EFAULT;
		} else if (efi.dki_lba == 2) {
			efi_gpe_t gpe;

			bzero(&gpe, sizeof (gpe));

			if (efi.dki_length < sizeof (gpe)) {
				mutex_exit(&zvol_state_lock);
				return (EINVAL);
			}

			UUID_LE_CONVERT(gpe.efi_gpe_PartitionTypeGUID, uuid);
			gpe.efi_gpe_StartingLBA = LE_64(34ULL);
			gpe.efi_gpe_EndingLBA =
			    LE_64((zv->zv_volsize >> zv->zv_min_bs) - 1);

			mutex_exit(&zvol_state_lock);
			if (ddi_copyout(&gpe, efi.dki_data, sizeof (gpe), flag))
				error = EFAULT;
		} else {
			mutex_exit(&zvol_state_lock);
			error = EINVAL;
		}
		return (error);

	case DKIOCFLUSHWRITECACHE:
		dkc = (struct dk_callback *)arg;
		zil_commit(zv->zv_zilog, UINT64_MAX, ZVOL_OBJ);
		if ((flag & FKIOCTL) && dkc != NULL && dkc->dkc_callback) {
			(*dkc->dkc_callback)(dkc->dkc_cookie, error);
			error = 0;
		}
		break;

	case DKIOCGGEOM:
	case DKIOCGVTOC:
		/*
		 * commands using these (like prtvtoc) expect ENOTSUP
		 * since we're emulating an EFI label
		 */
		error = ENOTSUP;
		break;

	case DKIOCDUMPINIT:
		rl = zfs_range_lock(&zv->zv_znode, 0, zv->zv_volsize,
		    RL_WRITER);
		error = zvol_dumpify(zv);
		zfs_range_unlock(rl);
		break;

	case DKIOCDUMPFINI:
		rl = zfs_range_lock(&zv->zv_znode, 0, zv->zv_volsize,
		    RL_WRITER);
		error = zvol_dump_fini(zv);
		zfs_range_unlock(rl);
		break;

	default:
		error = ENOTTY;
		break;

	}
	mutex_exit(&zvol_state_lock);
	return (error);
}

int
zvol_busy(void)
{
	return (zvol_minors != 0);
}

void
zvol_init(void)
{
	VERIFY(ddi_soft_state_init(&zvol_state, sizeof (zvol_state_t), 1) == 0);
	mutex_init(&zvol_state_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
zvol_fini(void)
{
	mutex_destroy(&zvol_state_lock);
	ddi_soft_state_fini(&zvol_state);
}

static boolean_t
zvol_is_swap(zvol_state_t *zv)
{
	vnode_t *vp;
	boolean_t ret = B_FALSE;
	char *devpath;
	size_t devpathlen;
	int error;

	devpathlen = strlen(ZVOL_FULL_DEV_DIR) + strlen(zv->zv_name) + 1;
	devpath = kmem_alloc(devpathlen, KM_SLEEP);
	(void) sprintf(devpath, "%s%s", ZVOL_FULL_DEV_DIR, zv->zv_name);
	error = lookupname(devpath, UIO_SYSSPACE, FOLLOW, NULLVPP, &vp);
	kmem_free(devpath, devpathlen);

	ret = !error && IS_SWAPVP(common_specvp(vp));

	if (vp != NULL)
		VN_RELE(vp);

	return (ret);
}

static int
zvol_dump_init(zvol_state_t *zv, boolean_t resize)
{
	dmu_tx_t *tx;
	int error = 0;
	objset_t *os = zv->zv_objset;
	nvlist_t *nv = NULL;
	uint64_t checksum, compress, refresrv;

	ASSERT(MUTEX_HELD(&zvol_state_lock));

	tx = dmu_tx_create(os);
	dmu_tx_hold_free(tx, ZVOL_OBJ, 0, DMU_OBJECT_END);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	/*
	 * If we are resizing the dump device then we only need to
	 * update the refreservation to match the newly updated
	 * zvolsize. Otherwise, we save off the original state of the
	 * zvol so that we can restore them if the zvol is ever undumpified.
	 */
	if (resize) {
		error = zap_update(os, ZVOL_ZAP_OBJ,
		    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 8, 1,
		    &zv->zv_volsize, tx);
	} else {
		error = dsl_prop_get_integer(zv->zv_name,
		    zfs_prop_to_name(ZFS_PROP_COMPRESSION), &compress, NULL);
		error = error ? error : dsl_prop_get_integer(zv->zv_name,
		    zfs_prop_to_name(ZFS_PROP_CHECKSUM), &checksum, NULL);
		error = error ? error : dsl_prop_get_integer(zv->zv_name,
		    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), &refresrv, NULL);

		error = error ? error : zap_update(os, ZVOL_ZAP_OBJ,
		    zfs_prop_to_name(ZFS_PROP_COMPRESSION), 8, 1,
		    &compress, tx);
		error = error ? error : zap_update(os, ZVOL_ZAP_OBJ,
		    zfs_prop_to_name(ZFS_PROP_CHECKSUM), 8, 1, &checksum, tx);
		error = error ? error : zap_update(os, ZVOL_ZAP_OBJ,
		    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 8, 1,
		    &refresrv, tx);
	}
	dmu_tx_commit(tx);

	/* Truncate the file */
	if (!error)
		error = zvol_truncate(zv, 0, DMU_OBJECT_END);

	if (error)
		return (error);

	/*
	 * We only need update the zvol's property if we are initializing
	 * the dump area for the first time.
	 */
	if (!resize) {
		VERIFY(nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) == 0);
		VERIFY(nvlist_add_uint64(nv,
		    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 0) == 0);
		VERIFY(nvlist_add_uint64(nv,
		    zfs_prop_to_name(ZFS_PROP_COMPRESSION),
		    ZIO_COMPRESS_OFF) == 0);
		VERIFY(nvlist_add_uint64(nv,
		    zfs_prop_to_name(ZFS_PROP_CHECKSUM),
		    ZIO_CHECKSUM_OFF) == 0);

		error = zfs_set_prop_nvlist(zv->zv_name, nv);
		nvlist_free(nv);

		if (error)
			return (error);
	}

	/* Allocate the space for the dump */
	error = zvol_prealloc(zv);
	return (error);
}

static int
zvol_dumpify(zvol_state_t *zv)
{
	int error = 0;
	uint64_t dumpsize = 0;
	dmu_tx_t *tx;
	objset_t *os = zv->zv_objset;

	if (zv->zv_flags & ZVOL_RDONLY || (zv->zv_mode & DS_MODE_READONLY))
		return (EROFS);

	/*
	 * We do not support swap devices acting as dump devices.
	 */
	if (zvol_is_swap(zv))
		return (ENOTSUP);

	if (zap_lookup(zv->zv_objset, ZVOL_ZAP_OBJ, ZVOL_DUMPSIZE,
	    8, 1, &dumpsize) != 0 || dumpsize != zv->zv_volsize) {
		boolean_t resize = (dumpsize > 0) ? B_TRUE : B_FALSE;

		if ((error = zvol_dump_init(zv, resize)) != 0) {
			(void) zvol_dump_fini(zv);
			return (error);
		}
	}

	/*
	 * Build up our lba mapping.
	 */
	error = zvol_get_lbas(zv);
	if (error) {
		(void) zvol_dump_fini(zv);
		return (error);
	}

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		(void) zvol_dump_fini(zv);
		return (error);
	}

	zv->zv_flags |= ZVOL_DUMPIFIED;
	error = zap_update(os, ZVOL_ZAP_OBJ, ZVOL_DUMPSIZE, 8, 1,
	    &zv->zv_volsize, tx);
	dmu_tx_commit(tx);

	if (error) {
		(void) zvol_dump_fini(zv);
		return (error);
	}

	txg_wait_synced(dmu_objset_pool(os), 0);
	return (0);
}

static int
zvol_dump_fini(zvol_state_t *zv)
{
	dmu_tx_t *tx;
	objset_t *os = zv->zv_objset;
	nvlist_t *nv;
	int error = 0;
	uint64_t checksum, compress, refresrv;

	tx = dmu_tx_create(os);
	dmu_tx_hold_zap(tx, ZVOL_ZAP_OBJ, TRUE, NULL);
	error = dmu_tx_assign(tx, TXG_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		return (error);
	}

	/*
	 * Attempt to restore the zvol back to its pre-dumpified state.
	 * This is a best-effort attempt as it's possible that not all
	 * of these properties were initialized during the dumpify process
	 * (i.e. error during zvol_dump_init).
	 */
	(void) zap_lookup(zv->zv_objset, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_CHECKSUM), 8, 1, &checksum);
	(void) zap_lookup(zv->zv_objset, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_COMPRESSION), 8, 1, &compress);
	(void) zap_lookup(zv->zv_objset, ZVOL_ZAP_OBJ,
	    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), 8, 1, &refresrv);

	(void) zap_remove(os, ZVOL_ZAP_OBJ, ZVOL_DUMPSIZE, tx);
	zvol_free_extents(zv);
	zv->zv_flags &= ~ZVOL_DUMPIFIED;
	dmu_tx_commit(tx);

	VERIFY(nvlist_alloc(&nv, NV_UNIQUE_NAME, KM_SLEEP) == 0);
	(void) nvlist_add_uint64(nv,
	    zfs_prop_to_name(ZFS_PROP_CHECKSUM), checksum);
	(void) nvlist_add_uint64(nv,
	    zfs_prop_to_name(ZFS_PROP_COMPRESSION), compress);
	(void) nvlist_add_uint64(nv,
	    zfs_prop_to_name(ZFS_PROP_REFRESERVATION), refresrv);
	(void) zfs_set_prop_nvlist(zv->zv_name, nv);
	nvlist_free(nv);

	return (0);
}
