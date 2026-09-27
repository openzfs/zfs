// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2015, Joyent, Inc.  All rights reserved.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

/*
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef _SYS_UIO_H
#define	_SYS_UIO_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/types.h>

/*
 * uio_extflg: extended flags
 */
#define	UIO_DIRECT		(1ULL << 0)	/* Direct I/O request */
#define	UIO_SKIP_CHANGETIME	(1ULL << 1)
#define	UIO_SKIP_WRITETIME	(1ULL << 2)
#define	UIO_SKIP_SIZE_UPDATE	(1ULL << 3)	/* skip z_size update */
#define	UIO_UNCACHED		(1ULL << 4)	/* Caller will not reuse data */
/*
 * UIO_DIO_DENY: the zpl caller declines Direct I/O for this request (e.g. a
 * file handle that already hit a benign DIO read verify failure).
 * UIO_DIO_CKSUM_RETRIED: set by zfs_read when a DIO read verify failed but
 * the buffered re-read succeeded -- a recycled O_DIRECT buffer, not an
 * on-disk error.
 */
#define	UIO_DIO_DENY		(1ULL << 5)
#define	UIO_DIO_CKSUM_RETRIED	(1ULL << 6)

/*
 * I/O parameter information.  A uio structure describes the I/O which
 * is to be performed by an operation.  Typically the data movement will
 * be performed by a routine such as uiomove(), which updates the uio
 * structure to reflect what was done.
 */

typedef struct iovec {
	void	*iov_base;
	size_t	iov_len;
} iovec_t;


/*
 * I/O direction.
 */
typedef enum zfs_uio_rw { UIO_READ, UIO_WRITE } zfs_uio_rw_t;

/*
 * Segment flag values.
 */
typedef enum zfs_uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_USERISPACE }
    zfs_uio_seg_t;

/*
 * This structure is used when doing Direct I/O.
 */
typedef void vm_page_t;
typedef struct {
	vm_page_t	*pages;
	int		npages;
} zfs_uio_dio_t;

typedef struct zfs_uio {
	struct iovec	*uio_iov;
	int		uio_iovcnt;
	int		uio_index;
	off_t		uio_loffset;
	off_t		uio_soffset;
	zfs_uio_seg_t	uio_segflg;
	boolean_t	uio_fault_disable;
	uint16_t	uio_fmode;
	uint16_t	uio_extflg;
	ssize_t		uio_resid;
	size_t		uio_skip;
	zfs_uio_dio_t	uio_dio;
} zfs_uio_t;

/*
 * Not functions: zio_crypt_os_icp.c assigns through these
 * (zfs_uio_segflg(u) = ..., zfs_uio_iovcnt(u) = ...,
 * zfs_uio_iov(u) = kmem_zalloc(...)), so they have to stay lvalues.
 */
#define	zfs_uio_segflg(uio)	((uio)->uio_segflg)
#define	zfs_uio_iovcnt(uio)	((uio)->uio_iovcnt)
#define	zfs_uio_iov(uio)	((uio)->uio_iov)

static inline off_t
zfs_uio_offset(zfs_uio_t *uio)
{
	return (uio->uio_loffset);
}

static inline off_t
zfs_uio_soffset(zfs_uio_t *uio)
{
	return (uio->uio_soffset);
}

static inline size_t
zfs_uio_resid(zfs_uio_t *uio)
{
	return (uio->uio_resid);
}

static inline int
zfs_uio_skip(zfs_uio_t *uio)
{
	return (uio->uio_skip);
}

static inline int /* lundman extension */
zfs_uio_index(zfs_uio_t *uio)
{
	return (uio->uio_index);
}

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, off_t off)
{
	uio->uio_loffset = off;
}

static inline void
zfs_uio_setsoffset(zfs_uio_t *uio, off_t off)
{
	uio->uio_soffset = off;
}

static inline void
zfs_uio_setskip(zfs_uio_t *uio, int skip)
{
	uio->uio_skip = skip;
}

static inline void /* lundman extension */
zfs_uio_setindex(zfs_uio_t *uio, int index)
{
	uio->uio_index = index;
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, size_t size)
{
	uio->uio_resid -= size;
	uio->uio_loffset += size;
}

/*
 * Not functions: zio_crypt.c assigns through these
 * (zfs_uio_iovbase(u, i) = ..., zfs_uio_iovlen(u, i) = ...), so they
 * have to stay lvalues.
 */
#define	zfs_uio_iovlen(uio, idx)	((uio)->uio_iov[(idx)].iov_len)
#define	zfs_uio_iovbase(uio, idx)	((uio)->uio_iov[(idx)].iov_base)

static inline void
zfs_uio_iovec_init(zfs_uio_t *uio, struct iovec *iov,
    unsigned long nr_segs, off_t offset, zfs_uio_seg_t seg, ssize_t resid,
    size_t skip)
{
	uio->uio_iov = iov;
	uio->uio_iovcnt = nr_segs;
	uio->uio_loffset = offset;
	uio->uio_soffset = offset;
	uio->uio_segflg = seg;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_index = 0;
	uio->uio_resid = resid;
	uio->uio_skip = skip;
}

extern int zfs_uio_prefaultpages(ssize_t, zfs_uio_t *);
#define	zfs_uio_fault_disable(uio, set)
#define	zfs_uio_fault_move(p, n, rw, u) zfs_uiomove((p), (n), (rw), (u))

extern ssize_t readv(int, const struct iovec *, int);
extern ssize_t writev(int fd, struct iovec *iov, unsigned iov_cnt);

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UIO_H */
