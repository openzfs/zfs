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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
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

#ifndef	_LIBSPL_SYS_UIO_H
#define	_LIBSPL_SYS_UIO_H

#include <sys/sysmacros.h>
#include <sys/types.h>
#include_next <sys/uio.h>

#ifdef __APPLE__
#include <sys/_types/_iovec_t.h>
#endif

#include <stdint.h>
typedef struct iovec iovec_t;

#if defined(__linux__) || defined(__APPLE__)
typedef enum zfs_uio_rw {
	UIO_READ =	0,
	UIO_WRITE =	1,
} zfs_uio_rw_t;

typedef enum zfs_uio_seg {
	UIO_SYSSPACE =	0,
} zfs_uio_seg_t;

#elif defined(__FreeBSD__)
typedef enum uio_seg  zfs_uio_seg_t;
#endif

typedef struct zfs_uio {
	struct iovec	*uio_iov;	/* pointer to array of iovecs */
	int		uio_iovcnt;	/* number of iovecs */
	offset_t	uio_loffset;	/* file offset */
	zfs_uio_seg_t	uio_segflg;	/* address space (kernel or user) */
	uint16_t	uio_fmode;	/* file mode flags */
	uint16_t	uio_extflg;	/* extended flags */
	ssize_t		uio_resid;	/* residual count */
} zfs_uio_t;

#define	zfs_uio_segflg(uio)		(uio)->uio_segflg
#define	zfs_uio_offset(uio)		(uio)->uio_loffset
#define	zfs_uio_resid(uio)		(uio)->uio_resid
#define	zfs_uio_iovcnt(uio)		(uio)->uio_iovcnt
#define	zfs_uio_iovlen(uio, idx)	(uio)->uio_iov[(idx)].iov_len
#define	zfs_uio_iovbase(uio, idx)	(uio)->uio_iov[(idx)].iov_base

static inline boolean_t
zfs_dio_page_aligned(void *buf)
{
	return ((((unsigned long)(buf) & (PAGESIZE - 1)) == 0) ?
	    B_TRUE : B_FALSE);
}

static inline boolean_t
zfs_dio_offset_aligned(uint64_t offset, uint64_t blksz)
{
	return ((IS_P2ALIGNED(offset, blksz)) ? B_TRUE : B_FALSE);
}

static inline boolean_t
zfs_dio_size_aligned(uint64_t size, uint64_t blksz)
{
	return (((size % blksz) == 0) ? B_TRUE : B_FALSE);
}

static inline boolean_t
zfs_dio_aligned(uint64_t offset, uint64_t size, uint64_t blksz)
{
	return ((zfs_dio_offset_aligned(offset, blksz) &&
	    zfs_dio_size_aligned(size, blksz)) ? B_TRUE : B_FALSE);
}

static inline void
zfs_uio_iov_at_index(zfs_uio_t *uio, uint_t idx, void **base, uint64_t *len)
{
	*base = zfs_uio_iovbase(uio, idx);
	*len = zfs_uio_iovlen(uio, idx);
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, ssize_t size)
{
	uio->uio_resid -= size;
	uio->uio_loffset += size;
}

static inline offset_t
zfs_uio_index_at_offset(zfs_uio_t *uio, offset_t off, uint_t *vec_idx)
{
	*vec_idx = 0;
	while (*vec_idx < (uint_t)zfs_uio_iovcnt(uio) &&
	    off >= (offset_t)zfs_uio_iovlen(uio, *vec_idx)) {
		off -= zfs_uio_iovlen(uio, *vec_idx);
		(*vec_idx)++;
	}

	return (off);
}

#endif	/* _SYS_UIO_H */
