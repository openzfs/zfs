/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
	UIO_USERSPACE =	0,
	UIO_SYSSPACE =	1,
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
	return (IS_P2ALIGNED(offset, blksz));
}

static inline boolean_t
zfs_dio_size_aligned(uint64_t size, uint64_t blksz)
{
	return (IS_P2ALIGNED(size, blksz));
}

static inline boolean_t
zfs_dio_aligned(uint64_t offset, uint64_t size, uint64_t blksz)
{
	return (zfs_dio_offset_aligned(offset, blksz) &&
	    zfs_dio_size_aligned(size, blksz));
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
