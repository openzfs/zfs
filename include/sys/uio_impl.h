// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#ifndef _SYS_UIO_IMPL_H
#define	_SYS_UIO_IMPL_H

#include <sys/uio.h>
#include <sys/sysmacros.h>

extern int zfs_uiomove(void *, size_t, zfs_uio_rw_t, zfs_uio_t *);
extern int zfs_uiocopy(void *, size_t, zfs_uio_rw_t, zfs_uio_t *, size_t *);
extern void zfs_uioskip(zfs_uio_t *, size_t);
extern void zfs_uio_free_dio_pages(zfs_uio_t *, zfs_uio_rw_t);
extern int zfs_uio_get_dio_pages_alloc(zfs_uio_t *, zfs_uio_rw_t);
extern boolean_t zfs_uio_page_aligned(zfs_uio_t *);

static inline boolean_t
zfs_dio_page_aligned(void *buf)
{
	return ((((uintptr_t)(buf) & (PAGESIZE - 1)) == 0) ?
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
	return ((size % blksz) == 0);
}

static inline boolean_t
zfs_dio_aligned(uint64_t offset, uint64_t size, uint64_t blksz)
{
	return (zfs_dio_offset_aligned(offset, blksz) &&
	    zfs_dio_size_aligned(size, blksz));
}

static inline boolean_t
zfs_uio_aligned(zfs_uio_t *uio, uint64_t blksz)
{
	return (zfs_dio_aligned(zfs_uio_offset(uio), zfs_uio_resid(uio),
	    blksz));
}

static inline void
zfs_uio_iov_at_index(zfs_uio_t *uio, uint_t idx, void **base, uint64_t *len)
{
	*base = zfs_uio_iovbase(uio, idx);
	*len = zfs_uio_iovlen(uio, idx);
}

static inline offset_t
zfs_uio_index_at_offset(zfs_uio_t *uio, offset_t off, uint_t *vec_idx)
{
	*vec_idx = 0;
	while (*vec_idx < zfs_uio_iovcnt(uio) &&
	    off >= zfs_uio_iovlen(uio, *vec_idx)) {
		off -= zfs_uio_iovlen(uio, *vec_idx);
		(*vec_idx)++;
	}

	return (off);
}

#endif	/* _SYS_UIO_IMPL_H */
