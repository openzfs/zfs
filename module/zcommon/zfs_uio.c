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
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

#ifdef _KERNEL

#include <sys/types.h>
#include <sys/uio_impl.h>
#include <sys/sysmacros.h>
#include <sys/strings.h>
#include <linux/kmap_compat.h>
#include <linux/uaccess.h>

/*
 * Move "n" bytes at byte address "p"; "rw" indicates the direction
 * of the move, and the I/O parameters are provided in "uio", which is
 * update to reflect the data which was moved.  Returns 0 on success or
 * a non-zero errno on failure.
 */
static int
uiomove_iov(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	const struct iovec *iov = uio->uio_iov;
	size_t skip = uio->uio_skip;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		cnt = MIN(iov->iov_len - skip, n);
		switch (uio->uio_segflg) {
		case UIO_USERSPACE:
			/*
			 * p = kernel data pointer
			 * iov->iov_base = user data pointer
			 */
			if (rw == UIO_READ) {
				if (copy_to_user(iov->iov_base+skip, p, cnt))
					return (EFAULT);
			} else {
				unsigned long b_left = 0;
				if (uio->uio_fault_disable) {
					if (!zfs_access_ok(VERIFY_READ,
					    (iov->iov_base + skip), cnt)) {
						return (EFAULT);
					}
					pagefault_disable();
					b_left =
					    __copy_from_user_inatomic(p,
					    (iov->iov_base + skip), cnt);
					pagefault_enable();
				} else {
					b_left =
					    copy_from_user(p,
					    (iov->iov_base + skip), cnt);
				}
				if (b_left > 0) {
					unsigned long c_bytes =
					    cnt - b_left;
					uio->uio_skip += c_bytes;
					ASSERT3U(uio->uio_skip, <,
					    iov->iov_len);
					uio->uio_resid -= c_bytes;
					uio->uio_loffset += c_bytes;
					return (EFAULT);
				}
			}
			break;
		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				bcopy(p, iov->iov_base + skip, cnt);
			else
				bcopy(iov->iov_base + skip, p, cnt);
			break;
		default:
			ASSERT(0);
		}
		skip += cnt;
		if (skip == iov->iov_len) {
			skip = 0;
			uio->uio_iov = (++iov);
			uio->uio_iovcnt--;
		}
		uio->uio_skip = skip;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		p = (caddr_t)p + cnt;
		n -= cnt;
	}
	return (0);
}

static int
uiomove_bvec(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	const struct bio_vec *bv = uio->uio_bvec;
	size_t skip = uio->uio_skip;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		void *paddr;
		cnt = MIN(bv->bv_len - skip, n);

		paddr = zfs_kmap_atomic(bv->bv_page, KM_USER1);
		if (rw == UIO_READ)
			bcopy(p, paddr + bv->bv_offset + skip, cnt);
		else
			bcopy(paddr + bv->bv_offset + skip, p, cnt);
		zfs_kunmap_atomic(paddr, KM_USER1);

		skip += cnt;
		if (skip == bv->bv_len) {
			skip = 0;
			uio->uio_bvec = (++bv);
			uio->uio_iovcnt--;
		}
		uio->uio_skip = skip;
		uio->uio_resid -= cnt;
		uio->uio_loffset += cnt;
		p = (caddr_t)p + cnt;
		n -= cnt;
	}
	return (0);
}

#if defined(HAVE_VFS_IOV_ITER)
static int
uiomove_iter(void *p, size_t n, enum uio_rw rw, struct uio *uio,
    boolean_t revert)
{
	size_t cnt = MIN(n, uio->uio_resid);

	if (uio->uio_skip)
		iov_iter_advance(uio->uio_iter, uio->uio_skip);

	if (rw == UIO_READ)
		cnt = copy_to_iter(p, cnt, uio->uio_iter);
	else
		cnt = copy_from_iter(p, cnt, uio->uio_iter);

	/*
	 * When operating on a full pipe no bytes are processed.
	 * In which case return EFAULT which is converted to EAGAIN
	 * by the kernel's generic_file_splice_read() function.
	 */
	if (cnt == 0)
		return (EFAULT);

	/*
	 * Revert advancing the uio_iter.  This is set by uiocopy()
	 * to avoid consuming the uio and its iov_iter structure.
	 */
	if (revert)
		iov_iter_revert(uio->uio_iter, cnt);

	uio->uio_resid -= cnt;
	uio->uio_loffset += cnt;

	return (0);
}
#endif

int
uiomove(void *p, size_t n, enum uio_rw rw, struct uio *uio)
{
	if (uio->uio_segflg == UIO_BVEC)
		return (uiomove_bvec(p, n, rw, uio));
#if defined(HAVE_VFS_IOV_ITER)
	else if (uio->uio_segflg == UIO_ITER)
		return (uiomove_iter(p, n, rw, uio, B_FALSE));
#endif
	else
		return (uiomove_iov(p, n, rw, uio));
}
EXPORT_SYMBOL(uiomove);

int
uio_prefaultpages(ssize_t n, struct uio *uio)
{
	struct iov_iter iter, *iterp = NULL;

#if defined(HAVE_IOV_ITER_FAULT_IN_READABLE)
	if (uio->uio_segflg == UIO_USERSPACE) {
		iterp = &iter;
		iov_iter_init_compat(iterp, READ, uio->uio_iov,
		    uio->uio_iovcnt, uio->uio_resid);
#if defined(HAVE_VFS_IOV_ITER)
	} else if (uio->uio_segflg == UIO_ITER) {
		iterp = uio->uio_iter;
#endif
	}

	if (iterp && iov_iter_fault_in_readable(iterp, n))
		return (EFAULT);
#endif
	return (0);
}
EXPORT_SYMBOL(uio_prefaultpages);

/*
 * The same as uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
int
uiocopy(void *p, size_t n, enum uio_rw rw, struct uio *uio, size_t *cbytes)
{
	struct uio uio_copy;
	int ret;

	bcopy(uio, &uio_copy, sizeof (struct uio));

	if (uio->uio_segflg == UIO_BVEC)
		ret = uiomove_bvec(p, n, rw, &uio_copy);
#if defined(HAVE_VFS_IOV_ITER)
	else if (uio->uio_segflg == UIO_ITER)
		ret = uiomove_iter(p, n, rw, &uio_copy, B_TRUE);
#endif
	else
		ret = uiomove_iov(p, n, rw, &uio_copy);

	*cbytes = uio->uio_resid - uio_copy.uio_resid;

	return (ret);
}
EXPORT_SYMBOL(uiocopy);

/*
 * Drop the next n chars out of *uio.
 */
void
uioskip(uio_t *uio, size_t n)
{
	if (n > uio->uio_resid)
		return;

	if (uio->uio_segflg == UIO_BVEC) {
		uio->uio_skip += n;
		while (uio->uio_iovcnt &&
		    uio->uio_skip >= uio->uio_bvec->bv_len) {
			uio->uio_skip -= uio->uio_bvec->bv_len;
			uio->uio_bvec++;
			uio->uio_iovcnt--;
		}
#if defined(HAVE_VFS_IOV_ITER)
	} else if (uio->uio_segflg == UIO_ITER) {
		iov_iter_advance(uio->uio_iter, n);
#endif
	} else {
		uio->uio_skip += n;
		while (uio->uio_iovcnt &&
		    uio->uio_skip >= uio->uio_iov->iov_len) {
			uio->uio_skip -= uio->uio_iov->iov_len;
			uio->uio_iov++;
			uio->uio_iovcnt--;
		}
	}
	uio->uio_loffset += n;
	uio->uio_resid -= n;
}
EXPORT_SYMBOL(uioskip);
#endif /* _KERNEL */
