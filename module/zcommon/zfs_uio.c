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

/*
 * The uio support from OpenSolaris has been added as a short term
 * work around.  The hope is to adopt native Linux type and drop the
 * use of uio's entirely.  Under Linux they only add overhead and
 * when possible we want to use native APIs for the ZPL layer.
 */
#ifdef _KERNEL

#include <sys/types.h>
#include <sys/uio_impl.h>

/*
 * Move "n" bytes at byte address "p"; "rw" indicates the direction
 * of the move, and the I/O parameters are provided in "uio", which is
 * update to reflect the data which was moved.  Returns 0 on success or
 * a non-zero errno on failure.
 */
int
uiomove(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	struct iovec *iov;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		iov = uio->uio_iov;
		cnt = MIN(iov->iov_len, n);
		if (cnt == 0l) {
			uio->uio_iov++;
			uio->uio_iovcnt--;
			continue;
		}
		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
		case UIO_USERISPACE:
			/* p = kernel data pointer
			 * iov->iov_base = user data pointer */

			if (rw == UIO_READ) {
				if (copy_to_user(iov->iov_base, p, cnt))
					return EFAULT;
				/* error = xcopyout_nta(p, iov->iov_base, cnt,
				 * (uio->uio_extflg & UIO_COPY_CACHED)); */
			} else {
				/* error = xcopyin_nta(iov->iov_base, p, cnt,
				 * (uio->uio_extflg & UIO_COPY_CACHED)); */
				if (copy_from_user(p, iov->iov_base, cnt))
					return EFAULT;
			}
			break;
		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				bcopy(p, iov->iov_base, cnt);
			else
				bcopy(iov->iov_base, p, cnt);
			break;
		}
		iov->iov_base += cnt;
		iov->iov_len -= cnt;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		p = (caddr_t)p + cnt;
		n -= cnt;
	}
	return (0);
}
EXPORT_SYMBOL(uiomove);
#endif /* _KERNEL */
