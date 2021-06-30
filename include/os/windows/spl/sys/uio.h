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
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2013 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2015, Joyent, Inc.  All rights reserved.
 * Copyright 2017 Jorgen Lundman <lundman@lundman.net>
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
typedef enum zfs_uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_USERISPACE } zfs_uio_seg_t;


typedef struct zfs_uio {
	const struct iovec	*uio_iov;
	int		uio_iovcnt;
	off_t		uio_loffset;
	zfs_uio_seg_t	uio_segflg;
	boolean_t	uio_fault_disable;
	uint16_t	uio_fmode;
	uint16_t	uio_extflg;
	ssize_t		uio_resid;
	size_t		uio_skip;
} zfs_uio_t;

static inline zfs_uio_seg_t
zfs_uio_segflg(zfs_uio_t *uio)
{
	return (uio->uio_segflg);
}

static inline int
zfs_uio_iovcnt(zfs_uio_t *uio)
{
	return (uio->uio_iovcnt);
}

static inline off_t
zfs_uio_offset(zfs_uio_t *uio)
{
	return (uio->uio_loffset);
}

static inline size_t
zfs_uio_resid(zfs_uio_t *uio)
{
	return (uio->uio_resid);
}

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, off_t off)
{
	uio->uio_loffset = off;
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, size_t size)
{
	uio->uio_resid -= size;
	uio->uio_loffset += size;
}

/* zfs_uio_iovlen(uio, 0) = uio_curriovlen() */
static inline uint64_t
zfs_uio_iovlen(zfs_uio_t *uio, unsigned int idx)
{
	return (uio->uio_iov[idx].iov_len);
}

static inline void *
zfs_uio_iovbase(zfs_uio_t *uio, unsigned int idx)
{
	return (uio->uio_iov[(idx)].iov_base);
}

static inline void
zfs_uio_iovec_init(zfs_uio_t *uio, const struct iovec *iov,
    unsigned long nr_segs, off_t offset, zfs_uio_seg_t seg, ssize_t resid,
    size_t skip)
{
	uio->uio_iov = iov;
	uio->uio_iovcnt = nr_segs;
	uio->uio_loffset = offset;
	uio->uio_segflg = seg;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_resid = resid;
	uio->uio_skip = skip;
}

extern int zfs_uio_prefaultpages(ssize_t, zfs_uio_t *);
#define zfs_uio_fault_disable(uio, set)
#define zfs_uio_fault_move(p, n, rw, u) zfs_uiomove((p), (n), (rw), (u))



#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UIO_H */
