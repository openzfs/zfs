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
 * Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */
/*
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 */
/*
 * Portions of this document are copyright Oracle and Joyent.
 * OS X implementation of ldi_ named functions for ZFS written by
 * Evan Susarret in 2015.
 */

/*
 * ZFS internal
 */
#include <sys/zfs_context.h>

/*
 * LDI Includes
 */
#include <sys/ldi_impl_osx.h>

/* Debug prints */

#define	ldi_log(fmt, ...) do {	\
	dprintf(fmt, __VA_ARGS__);	\
	/* delay(hz>>1); */		\
_NOTE(CONSTCOND) } while (0)

struct _handle_vnode {
	vnode_t		*devvp;
	char *vd_readlinkname;
};	/* 16b */

#define	LH_VNODE(lhp)	lhp->lh_tsd.vnode_tsd->devvp

void
handle_free_vnode(struct ldi_handle *lhp)
{
	if (!lhp) {
		dprintf("%s missing lhp\n", __func__);
		return;
	}

	if (!lhp->lh_tsd.vnode_tsd) {
		dprintf("%s missing vnode_tsd\n", __func__);
		return;
	}

	kmem_free(lhp->lh_tsd.vnode_tsd, sizeof (struct _handle_vnode));
	lhp->lh_tsd.vnode_tsd = 0;
}


/* Returns handle with lock still held */
struct ldi_handle *
handle_alloc_vnode(dev_t device, int fmode)
{
	struct ldi_handle *lhp, *retlhp;

	/* Search for existing handle */
	if ((retlhp = handle_find(device, fmode, B_TRUE)) != NULL) {
		dprintf("%s found handle before alloc\n", __func__);
		return (retlhp);
	}

	/* Validate arguments */
	if (device == 0 || fmode == 0) {
		dprintf("%s missing dev_t %d or fmode %d\n",
		    __func__, device, fmode);
		return (NULL);
	}

	/* Allocate LDI vnode handle */
	if ((lhp = handle_alloc_common(LDI_TYPE_VNODE, device,
	    fmode)) == NULL) {
		dprintf("%s couldn't allocate lhp\n", __func__);
		return (NULL);
	}

	/* Allocate and clear type-specific device data */
	lhp->lh_tsd.vnode_tsd = (struct _handle_vnode *)kmem_alloc(
	    sizeof (struct _handle_vnode), KM_SLEEP);
	LH_VNODE(lhp) = 0;

	/* Add the handle to the list, or return match */
	if ((retlhp = handle_add(lhp)) == NULL) {
		dprintf("%s handle_add failed\n", __func__);
		handle_release(lhp);
		return (NULL);
	}

	/* Check if new or found handle was returned */
	if (retlhp != lhp) {
		dprintf("%s found handle after alloc\n", __func__);
		handle_release(lhp);
		lhp = 0;
	}

	return (retlhp);
}

int
handle_close_vnode(struct ldi_handle *lhp)
{
	vfs_context_t context;
	int error = EINVAL;

	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_type, ==, LDI_TYPE_VNODE);
	ASSERT3U(LH_VNODE(lhp), !=, NULL);
	ASSERT3U(lhp->lh_status, ==, LDI_STATUS_CLOSING);

#ifdef DEBUG
	/* Validate vnode and context */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (ENOMEM);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		/* If getwithref failed, we can't call vnode_close.  */
		LH_VNODE(lhp) = NULLVP;
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	/* For read-write, clear mountedon flag and wait for writes */
	if (lhp->lh_fmode & FWRITE) {
		/* Wait for writes to complete */
		error = vnode_waitforwrites(LH_VNODE(lhp), 0, 0, 0,
		    "ldi::handle_close_vnode");
		if (error != 0) {
			dprintf("%s waitforwrites returned %d\n",
			    __func__, error);
		}
	}

	/* Drop usecount */
	vnode_rele(LH_VNODE(lhp));

	/* Drop iocount and refcount */
	error = vnode_close(LH_VNODE(lhp),
	    (lhp->lh_fmode & FWRITE ? FWASWRITTEN : 0),
	    context);
	/* Preserve error from vnode_close */

	/* Clear handle devvp vnode pointer */
	LH_VNODE(lhp) = NULLVP;
	/* Drop VFS context */
	vfs_context_rele(context);

	if (error) {
		dprintf("%s vnode_close error %d\n",
		    __func__, error);
	}
	/* Return error from close */
	return (error);
}

static int
handle_open_vnode(struct ldi_handle *lhp, char *path)
{
	vfs_context_t context;
	int error = EINVAL;

	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(path, !=, NULL);
	ASSERT3U(lhp->lh_type, ==, LDI_TYPE_VNODE);
	ASSERT3U(lhp->lh_status, ==, LDI_STATUS_OPENING);

	/* Validate path string */
	if (!path || strlen(path) <= 1) {
		dprintf("%s missing path\n", __func__);
		return (EINVAL);
	}

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (ENOMEM);
	}

	/* Try to open the device by path (takes iocount) */
	error = vnode_open(path, lhp->lh_fmode, 0, 0,
	    &(LH_VNODE(lhp)), context);

	if (error) {
		dprintf("%s vnode_open error %d\n", __func__, error);
		/* Return error from vnode_open */
		return (error);
	}

	/* Increase usecount, saving error. */
	error = vnode_ref(LH_VNODE(lhp));
	if (error != 0) {
		dprintf("%s couldn't vnode_ref\n", __func__);
		vnode_close(LH_VNODE(lhp), lhp->lh_fmode, context);
		/* Return error from vnode_ref */
		return (error);
	}

	/* Verify vnode refers to a block device */
	if (!vnode_isblk(LH_VNODE(lhp))) {
		dprintf("%s %s is not a block device\n",
		    __func__, path);
		vnode_rele(LH_VNODE(lhp));
		vnode_close(LH_VNODE(lhp), lhp->lh_fmode, context);
		return (ENOTBLK);
	}

	/* Drop iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop VFS context */
	vfs_context_rele(context);

	return (0);
}

int
handle_get_size_vnode(struct ldi_handle *lhp, uint64_t *dev_size)
{
	vfs_context_t context;
	uint64_t blkcnt = 0;
	uint32_t blksize = 0;
	int error = EINVAL;

#ifdef DEBUG
	if (!lhp || !dev_size) {
		dprintf("%s missing lhp or dev_size\n", __func__);
		return (EINVAL);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (ENOMEM);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	/* Fetch the blocksize */
	error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCGETBLOCKSIZE,
	    (caddr_t)&blksize, 0, context);
	error = (blksize == 0 ? ENODEV : error);

	/* Fetch the block count */
	error = (error ? error : VNOP_IOCTL(LH_VNODE(lhp),
	    DKIOCGETBLOCKCOUNT, (caddr_t)&blkcnt,
	    0, context));
	error = (blkcnt == 0 ? ENODEV : error);

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop VFS context */
	vfs_context_rele(context);

	/* Cast both to 64-bit then multiply */
	*dev_size = ((uint64_t)blksize * (uint64_t)blkcnt);
	if (*dev_size == 0) {
		dprintf("%s invalid blksize %u or blkcnt %llu\n",
		    __func__, blksize, blkcnt);
		return (ENODEV);
	}
	return (0);
}

int
handle_get_dev_path_vnode(struct ldi_handle *lhp, char *path, int len)
{
	vfs_context_t context;
	int error;

	if (!lhp || !path || len == 0) {
		dprintf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (ENOMEM);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	if ((error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCGETFIRMWAREPATH,
	    (caddr_t)path, len, context)) != 0) {
		dprintf("%s VNOP_IOCTL error %d\n", __func__, error);
		/* Preserve error to return */
	}

	/* Drop iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop VFS context */
	vfs_context_rele(context);

if (error == 0) dprintf("%s got device path [%s]\n", __func__, path);
	return (error);
}

int
handle_get_bootinfo_vnode(struct ldi_handle *lhp,
    struct io_bootinfo *bootinfo)
{
	int error;

	if (!lhp || !bootinfo) {
		dprintf("%s missing argument\n", __func__);
printf("%s missing argument\n", __func__);
		return (EINVAL);
	}

	if ((error = handle_get_size_vnode(lhp,
	    &bootinfo->dev_size)) != 0 ||
	    (error = handle_get_dev_path_vnode(lhp, bootinfo->dev_path,
	    sizeof (bootinfo->dev_path))) != 0) {
		dprintf("%s get size or dev_path error %d\n",
		    __func__, error);
	}

	return (error);
}

int
handle_sync_vnode(struct ldi_handle *lhp)
{
	vfs_context_t context;
	int error = EINVAL;

#ifdef DEBUG
	if (!lhp) {
		dprintf("%s missing lhp\n", __func__);
		return (EINVAL);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (ENOMEM);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	/*
	 * Flush out any old buffers remaining from a previous use.
	 * buf_invalidateblks flushes UPL buffers, VNOP_FSYNC informs
	 * the disk device to flush write buffers to disk.
	 */
	error = buf_invalidateblks(LH_VNODE(lhp), BUF_WRITE_DATA, 0, 0);

	error = (error ? error : VNOP_FSYNC(LH_VNODE(lhp),
	    MNT_WAIT, context));

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop VFS context */
	vfs_context_rele(context);

	if (error) {
		dprintf("%s buf_invalidateblks or VNOP_FSYNC error %d\n",
		    __func__, error);
		return (ENOTSUP);
	}
	return (0);
}

/* vnode_lookup, find dev_t info */
dev_t
dev_from_path(char *path)
{
	vfs_context_t context;
	vnode_t *devvp = NULLVP;
	dev_t device;
	int error = EINVAL;

#ifdef DEBUG
	/* Validate path */
	if (path == 0 || strlen(path) <= 1 || path[0] != '/') {
		dprintf("%s invalid path provided\n", __func__);
		return (0);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (0);
	}

	/* Try to lookup the vnode by path */
	error = vnode_lookup(path, 0, &devvp, context);
	if (error || devvp == NULLVP) {
		dprintf("%s vnode_lookup failed %d\n", __func__, error);
		vfs_context_rele(context);
		return (0);
	}

	/* Get the rdev of this vnode */
	device = vnode_specrdev(devvp);

	/* Drop iocount on devvp */
	vnode_put(devvp);
	/* Drop vfs_context */
	vfs_context_rele(context);

#ifdef DEBUG
	/* Validate dev_t */
	if (device == 0) {
		dprintf("%s invalid device\n", __func__);
	}
#endif

	/* Return 0 or valid dev_t */
	return (device);
}

/* Completion handler for vnode strategy */
static void
ldi_vnode_io_intr(buf_t bp, void *arg)
{
	ldi_buf_t *lbp = (ldi_buf_t *)arg;

	ASSERT3U(bp, !=, NULL);
	ASSERT3U(lbp, !=, NULL);

	/* Copyout error and resid */
	lbp->b_error = buf_error(bp);
	lbp->b_resid = buf_resid(bp);

#ifdef DEBUG
	if (lbp->b_error || lbp->b_resid != 0) {
		dprintf("%s io error %d resid %llu\n", __func__,
		    lbp->b_error, lbp->b_resid);
	}
#endif

	/* Teardown */
	buf_free(bp);

	/* Call original completion function */
	if (lbp->b_iodone) {
		lbp->b_iodone(lbp);
	}
}

int
buf_strategy_vnode(ldi_buf_t *lbp, struct ldi_handle *lhp)
{
	buf_t bp = 0;
	int error = EINVAL;

	ASSERT3U(lbp, !=, NULL);
	ASSERT3U(lhp, !=, NULL);

#ifdef DEBUG
	if (!lbp || !lhp) {
		dprintf("%s missing lbp or lhp\n", __func__);
		return (EINVAL);
	}
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s handle is not Online\n", __func__);
		return (ENODEV);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and verify buf_t */
	if (NULL == (bp = buf_alloc(LH_VNODE(lhp)))) {
		dprintf("%s couldn't allocate buf_t\n", __func__);
		return (ENOMEM);
	}

	/* Setup buffer */
	buf_setflags(bp, B_NOCACHE | (lbp->b_flags & B_READ ?
	    B_READ : B_WRITE) |
	    (lbp->b_flags & (B_PASSIVE | B_PHYS | B_RAW)) |
	    ((lbp->b_iodone == NULL) ? 0: B_ASYNC));
	buf_setcount(bp, lbp->b_bcount);
	buf_setdataptr(bp, (uintptr_t)lbp->b_un.b_addr);
	buf_setblkno(bp, lbp->b_lblkno);
	buf_setlblkno(bp, lbp->b_lblkno);
	buf_setsize(bp, lbp->b_bufsize);

	/* For asynchronous IO */
	if (lbp->b_iodone != NULL) {
		buf_setcallback(bp, &ldi_vnode_io_intr, lbp);
	}

	/* Recheck instantaneous value of handle status */
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s device not online\n", __func__);
		buf_free(bp);
		return (ENODEV);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		buf_free(bp);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	if (!(lbp->b_flags & B_READ)) {
		/* Does not return an error status */
		vnode_startwrite(LH_VNODE(lhp));
	}

	/* Issue the IO, preserving error */
	error = VNOP_STRATEGY(bp);

	if (error) {
		dprintf("%s VNOP_STRATEGY error %d\n",
		    __func__, error);
		/* Reclaim write count on vnode */
		if (!(lbp->b_flags & B_READ)) {
			vnode_writedone(LH_VNODE(lhp));
		}
		vnode_put(LH_VNODE(lhp));
		buf_free(bp);
		return (EIO);
	}

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));

	/* For synchronous IO, call completion */
	if (lbp->b_iodone == NULL) {
		ldi_vnode_io_intr(bp, (void*)lbp);
	}

	/* Pass error from VNOP_STRATEGY */
	return (error);
}

/* Client interface, alloc and open vnode handle by pathname */
int
ldi_open_vnode_by_path(char *path, dev_t device,
    int fmode, ldi_handle_t *lhp)
{
	struct ldi_handle *retlhp;
	ldi_status_t status;
	int error = EIO;

	/* Validate arguments */
	if (!path || strlen(path) <= 1 || device == 0 || !lhp) {
		dprintf("%s invalid argument %p %d %p\n", __func__,
		    path, device, lhp);
		if (path) {
			dprintf("*path string is %s\n", path);
		}
		return (EINVAL);
	}
	/* In debug build, be loud if we potentially leak a handle */
	ASSERT3U(*(struct ldi_handle **)lhp, ==, NULL);

	/* Allocate handle with path */
	retlhp = handle_alloc_vnode(device, fmode);
	if (retlhp == NULL) {
		dprintf("%s couldn't allocate vnode handle\n", __func__);
		return (ENOMEM);
	}

	/* Mark the handle as Opening, or increment openref */
	status = handle_open_start(retlhp);
	if (status == LDI_STATUS_ONLINE) {
		dprintf("%s already online, refs %d, openrefs %d\n", __func__,
		    retlhp->lh_ref, retlhp->lh_openref);
		/* Cast retlhp and assign to lhp (may be 0) */
		*lhp = (ldi_handle_t)retlhp;
		/* Successfully incremented open ref in open_start */
		return (0);
	}

	/* If state is now Opening, try to open device by vnode */
	if (status != LDI_STATUS_OPENING ||
	    (error = handle_open_vnode(retlhp, path)) != 0) {
		dprintf("%s Couldn't open handle\n", __func__);
		handle_open_done(retlhp, LDI_STATUS_CLOSED);
		handle_release(retlhp);
		retlhp = 0;
		return ((error == EACCES) ? EROFS:EIO);
	}
	handle_open_done(retlhp, LDI_STATUS_ONLINE);

	/* Register for disk notifications */
	handle_register_notifier(retlhp);

	/* Cast retlhp and assign to lhp (may be 0) */
	*lhp = (ldi_handle_t)retlhp;
	/* Pass error from open */
	return (error);
}

int
handle_get_media_info_vnode(struct ldi_handle *lhp,
    struct dk_minfo *dkm)
{
	vfs_context_t context;
	uint32_t blksize;
	uint64_t blkcount;
	int error;

#ifdef DEBUG
	if (!lhp || !dkm) {
		dprintf("%s missing lhp or dkm\n", __func__);
		return (EINVAL);
	}
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s handle is not Online\n", __func__);
		return (ENODEV);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (0);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	/* Get the blocksize and block count */
	error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCGETBLOCKSIZE,
	    (caddr_t)&blksize, 0, context);
	error = (error ? error : VNOP_IOCTL(LH_VNODE(lhp),
	    DKIOCGETBLOCKCOUNT, (caddr_t)&blkcount,
	    0, context));

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop vfs_context */
	vfs_context_rele(context);

	if (error) {
		dkm->dki_capacity = 0;
		dkm->dki_lbsize = 0;
		return (error);
	}

	/* If successful, set return values */
	dkm->dki_capacity = blkcount;
	dkm->dki_lbsize = blksize;
	return (0);
}

int
handle_get_media_info_ext_vnode(struct ldi_handle *lhp,
    struct dk_minfo_ext *dkmext)
{
	vfs_context_t context;
	uint32_t blksize, pblksize;
	uint64_t blkcount;
	int error;

#ifdef DEBUG
	if (!lhp || !dkmext) {
		dprintf("%s missing lhp or dkmext\n", __func__);
		return (EINVAL);
	}
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s handle is not Online\n", __func__);
		return (ENODEV);
	}

	/* Validate vnode and context */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode or context\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (0);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	/* Get the blocksize, physical blocksize, and block count */
	error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCGETBLOCKSIZE,
	    (caddr_t)&blksize, 0, context);
	error = (error ? error : VNOP_IOCTL(LH_VNODE(lhp),
	    DKIOCGETPHYSICALBLOCKSIZE, (caddr_t)&pblksize,
	    0, context));
	error = (error ? error : VNOP_IOCTL(LH_VNODE(lhp),
	    DKIOCGETBLOCKCOUNT, (caddr_t)&blkcount,
	    0, context));

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop vfs_context */
	vfs_context_rele(context);

	if (error) {
		dkmext->dki_capacity = 0;
		dkmext->dki_lbsize = 0;
		dkmext->dki_pbsize = 0;
		return (error);
	}

	/* If successful, set return values */
	dkmext->dki_capacity = blkcount;
	dkmext->dki_lbsize = blksize;
	dkmext->dki_pbsize = pblksize;
	return (0);
}

int
handle_check_media_vnode(struct ldi_handle *lhp, int *status)
{
	if (!lhp || !status) {
		dprintf("%s missing lhp or invalid status\n", __func__);
		return (EINVAL);
	}

	/* Validate vnode and context */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}

	/* XXX As yet unsupported */
	return (ENOTSUP);

	/* Check if the device is available and responding */
	return (0);
}

int
handle_is_solidstate_vnode(struct ldi_handle *lhp, int *isssd)
{
	vfs_context_t context;
	int error;

	if (!lhp || !isssd) {
		dprintf("%s missing lhp or invalid status\n", __func__);
		return (EINVAL);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (ENOMEM);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCISSOLIDSTATE,
	    (caddr_t)isssd, 0, context);

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop vfs_context */
	vfs_context_rele(context);

	return (error);
}

int
handle_features_vnode(struct ldi_handle *lhp,
    uint32_t *features)
{
	vfs_context_t context;
	int error;

#ifdef DEBUG
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s handle is not Online\n", __func__);
		return (ENODEV);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (0);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}

	/* All code paths from here must vnode_put. */

	error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCGETFEATURES,
	    (caddr_t)features, 0, context);

	if (error) {
		printf("%s: 0x%x\n", __func__, error);
	}

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop vfs_context */
	vfs_context_rele(context);

	return (error);
}

int
handle_unmap_vnode(struct ldi_handle *lhp,
    dkioc_free_list_ext_t *dkm)
{
	vfs_context_t context;
	int error;

#ifdef DEBUG
	if (!lhp || !dkm) {
		dprintf("%s missing lhp or dkm\n", __func__);
		return (EINVAL);
	}
	if (lhp->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s handle is not Online\n", __func__);
		return (ENODEV);
	}

	/* Validate vnode */
	if (LH_VNODE(lhp) == NULLVP) {
		dprintf("%s missing vnode\n", __func__);
		return (ENODEV);
	}
#endif

	/* Allocate and validate context */
	context = vfs_context_create(spl_vfs_context_kernel());
	if (!context) {
		dprintf("%s couldn't create VFS context\n", __func__);
		return (0);
	}

	/* Take an iocount on devvp vnode. */
	error = vnode_getwithref(LH_VNODE(lhp));
	if (error) {
		dprintf("%s vnode_getwithref error %d\n",
		    __func__, error);
		vfs_context_rele(context);
		return (ENODEV);
	}
	/* All code paths from here must vnode_put. */

	/* We need to convert illumos' dkioc_free_list_t to dk_unmap_t */
	/* We only support 1 entry now */
	dk_unmap_t dkun = { 0 };
	dk_extent_t ext;
	dkun.extentsCount = 1;
	dkun.extents = &ext;
	ext.offset = dkm->dfle_start;
	ext.length = dkm->dfle_length;

	/*
	 * dkm->dfl_flags vs dkun.options
	 * #define DF_WAIT_SYNC 0x00000001 Wait for full write-out of free.
	 * #define _DK_UNMAP_INITIALIZE    0x00000100
	 */

	/* issue unmap */
	error = VNOP_IOCTL(LH_VNODE(lhp), DKIOCUNMAP,
	    (caddr_t)&dkun, 0, context);

	if (error) {
		dprintf("%s unmap: 0x%x for off %llx size %llx\n", __func__,
		    error, ext.offset, ext.length);
	}

	/* Release iocount on vnode (still has usecount) */
	vnode_put(LH_VNODE(lhp));
	/* Drop vfs_context */
	vfs_context_rele(context);

	return (error);
}
