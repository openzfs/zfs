/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_UIO_H
#define	_SPL_UIO_H

#include <sys/debug.h>
#include <linux/uio.h>
#include <linux/blkdev.h>
#include <linux/blkdev_compat.h>
#include <linux/mm.h>
#include <linux/bio.h>
#include <asm/uaccess.h>
#include <sys/types.h>

#if defined(HAVE_VFS_IOV_ITER) && defined(HAVE_FAULT_IN_IOV_ITER_READABLE)
#define	iov_iter_fault_in_readable(a, b)	fault_in_iov_iter_readable(a, b)
#endif

typedef struct iovec iovec_t;

typedef enum zfs_uio_rw {
	UIO_READ =		0,
	UIO_WRITE =		1,
} zfs_uio_rw_t;

typedef enum zfs_uio_seg {
	UIO_USERSPACE =		0,
	UIO_SYSSPACE =		1,
	UIO_BVEC =		2,
#if defined(HAVE_VFS_IOV_ITER)
	UIO_ITER =		3,
#endif
} zfs_uio_seg_t;

typedef struct zfs_uio {
	union {
		const struct iovec	*uio_iov;
		const struct bio_vec	*uio_bvec;
#if defined(HAVE_VFS_IOV_ITER)
		struct iov_iter		*uio_iter;
#endif
	};
	int		uio_iovcnt;
	offset_t	uio_loffset;
	zfs_uio_seg_t	uio_segflg;
	boolean_t	uio_fault_disable;
	uint16_t	uio_fmode;
	uint16_t	uio_extflg;
	ssize_t		uio_resid;
	size_t		uio_skip;
} zfs_uio_t;

#define	zfs_uio_segflg(u)		(u)->uio_segflg
#define	zfs_uio_offset(u)		(u)->uio_loffset
#define	zfs_uio_resid(u)		(u)->uio_resid
#define	zfs_uio_iovcnt(u)		(u)->uio_iovcnt
#define	zfs_uio_iovlen(u, idx)		(u)->uio_iov[(idx)].iov_len
#define	zfs_uio_iovbase(u, idx)		(u)->uio_iov[(idx)].iov_base
#define	zfs_uio_fault_disable(u, set)	(u)->uio_fault_disable = set
#define	zfs_uio_rlimit_fsize(z, u)	(0)
#define	zfs_uio_fault_move(p, n, rw, u)	zfs_uiomove((p), (n), (rw), (u))

extern int zfs_uio_prefaultpages(ssize_t, zfs_uio_t *);

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, offset_t off)
{
	uio->uio_loffset = off;
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, size_t size)
{
	uio->uio_resid -= size;
	uio->uio_loffset += size;
}

static inline void
zfs_uio_iovec_init(zfs_uio_t *uio, const struct iovec *iov,
    unsigned long nr_segs, offset_t offset, zfs_uio_seg_t seg, ssize_t resid,
    size_t skip)
{
	ASSERT(seg == UIO_USERSPACE || seg == UIO_SYSSPACE);

	uio->uio_iov = iov;
	uio->uio_iovcnt = nr_segs;
	uio->uio_loffset = offset;
	uio->uio_segflg = seg;
	uio->uio_fault_disable = B_FALSE;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_resid = resid;
	uio->uio_skip = skip;
}

static inline void
zfs_uio_bvec_init(zfs_uio_t *uio, struct bio *bio)
{
	uio->uio_bvec = &bio->bi_io_vec[BIO_BI_IDX(bio)];
	uio->uio_iovcnt = bio->bi_vcnt - BIO_BI_IDX(bio);
	uio->uio_loffset = BIO_BI_SECTOR(bio) << 9;
	uio->uio_segflg = UIO_BVEC;
	uio->uio_fault_disable = B_FALSE;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_resid = BIO_BI_SIZE(bio);
	uio->uio_skip = BIO_BI_SKIP(bio);
}

#if defined(HAVE_VFS_IOV_ITER)
static inline void
zfs_uio_iov_iter_init(zfs_uio_t *uio, struct iov_iter *iter, offset_t offset,
    ssize_t resid, size_t skip)
{
	uio->uio_iter = iter;
	uio->uio_iovcnt = iter->nr_segs;
	uio->uio_loffset = offset;
	uio->uio_segflg = UIO_ITER;
	uio->uio_fault_disable = B_FALSE;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_resid = resid;
	uio->uio_skip = skip;
}
#endif

#endif /* SPL_UIO_H */
