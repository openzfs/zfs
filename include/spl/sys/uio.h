/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
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

#include <linux/uio.h>
#include <linux/blkdev.h>
#include <asm/uaccess.h>
#include <sys/types.h>

typedef struct iovec iovec_t;

typedef enum uio_rw {
	UIO_READ =		0,
	UIO_WRITE =		1,
} uio_rw_t;

typedef enum uio_seg {
	UIO_USERSPACE =		0,
	UIO_SYSSPACE =		1,
	UIO_BVEC =		2,
#if defined(HAVE_VFS_IOV_ITER)
	UIO_ITER =		3,
#endif
} uio_seg_t;

typedef struct uio {
	union {
		const struct iovec	*uio_iov;
		const struct bio_vec	*uio_bvec;
#if defined(HAVE_VFS_IOV_ITER)
		struct iov_iter		*uio_iter;
#endif
	};
	int		uio_iovcnt;
	offset_t	uio_loffset;
	uio_seg_t	uio_segflg;
	boolean_t	uio_fault_disable;
	uint16_t	uio_fmode;
	uint16_t	uio_extflg;
	offset_t	uio_limit;
	ssize_t		uio_resid;
	size_t		uio_skip;
} uio_t;

typedef struct aio_req {
	uio_t		*aio_uio;
	void		*aio_private;
} aio_req_t;

typedef enum xuio_type {
	UIOTYPE_ASYNCIO,
	UIOTYPE_ZEROCOPY,
} xuio_type_t;


#define	UIOA_IOV_MAX    16

typedef struct uioa_page_s {
	int	uioa_pfncnt;
	void	**uioa_ppp;
	caddr_t	uioa_base;
	size_t	uioa_len;
} uioa_page_t;

typedef struct xuio {
	uio_t xu_uio;
	enum xuio_type xu_type;
	union {
		struct {
			uint32_t xu_a_state;
			ssize_t xu_a_mbytes;
			uioa_page_t *xu_a_lcur;
			void **xu_a_lppp;
			void *xu_a_hwst[4];
			uioa_page_t xu_a_locked[UIOA_IOV_MAX];
		} xu_aio;

		struct {
			int xu_zc_rw;
			void *xu_zc_priv;
		} xu_zc;
	} xu_ext;
} xuio_t;

#define	XUIO_XUZC_PRIV(xuio)	xuio->xu_ext.xu_zc.xu_zc_priv
#define	XUIO_XUZC_RW(xuio)	xuio->xu_ext.xu_zc.xu_zc_rw

static inline void
iov_iter_init_compat(struct iov_iter *iter, unsigned int dir,
    const struct iovec *iov, unsigned long nr_segs, size_t count)
{
#if defined(HAVE_IOV_ITER_INIT)
	iov_iter_init(iter, dir, iov, nr_segs, count);
#elif defined(HAVE_IOV_ITER_INIT_LEGACY)
	iov_iter_init(iter, iov, nr_segs, count, 0);
#else
#error "Unsupported kernel"
#endif
}

static inline void
uio_iovec_init(uio_t *uio, const struct iovec *iov, unsigned long nr_segs,
    offset_t offset, uio_seg_t seg, ssize_t resid, size_t skip)
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
uio_bvec_init(uio_t *uio, struct bio *bio)
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
uio_iov_iter_init(uio_t *uio, struct iov_iter *iter, offset_t offset,
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
