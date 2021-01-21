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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved   */

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
 * $FreeBSD$
 */

#include <sys/param.h>
#include <sys/uio.h>
#include <sys/vnode.h>
#include <sys/zfs_znode.h>

/*
 * same as zfs_uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
int
zfs_uiocopy(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio, size_t *cbytes)
{
	struct iovec small_iovec[1];
	struct uio small_uio_clone;
	struct uio *uio_clone;
	int error;

	ASSERT3U(zfs_uio_rw(uio), ==, rw);
	if (zfs_uio_iovcnt(uio) == 1) {
		small_uio_clone = *(GET_UIO_STRUCT(uio));
		small_iovec[0] = *(GET_UIO_STRUCT(uio)->uio_iov);
		small_uio_clone.uio_iov = small_iovec;
		uio_clone = &small_uio_clone;
	} else {
		uio_clone = cloneuio(GET_UIO_STRUCT(uio));
	}

	error = vn_io_fault_uiomove(p, n, uio_clone);
	*cbytes = zfs_uio_resid(uio) - uio_clone->uio_resid;
	if (uio_clone != &small_uio_clone)
		free(uio_clone, M_IOV);
	return (error);
}

/*
 * Drop the next n chars out of *uiop.
 */
void
zfs_uioskip(zfs_uio_t *uio, size_t n)
{
	zfs_uio_seg_t segflg;

	/* For the full compatibility with illumos. */
	if (n > zfs_uio_resid(uio))
		return;

	segflg = zfs_uio_segflg(uio);
	zfs_uio_segflg(uio) = UIO_NOCOPY;
	zfs_uiomove(NULL, n, zfs_uio_rw(uio), uio);
	zfs_uio_segflg(uio) = segflg;
}

int
zfs_uio_fault_move(void *p, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio)
{
	ASSERT(zfs_uio_rw(uio) == dir);
	return (vn_io_fault_uiomove(p, n, GET_UIO_STRUCT(uio)));
}
