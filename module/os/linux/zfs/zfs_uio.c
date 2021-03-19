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
zfs_uiomove_iov(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio)
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
zfs_uiomove_bvec(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio)
{
	const struct bio_vec *bv = uio->uio_bvec;
	size_t skip = uio->uio_skip;
	ulong_t cnt;

	while (n && uio->uio_resid) {
		void *paddr;
		cnt = MIN(bv->bv_len - skip, n);

		paddr = zfs_kmap_atomic(bv->bv_page);
		if (rw == UIO_READ)
			bcopy(p, paddr + bv->bv_offset + skip, cnt);
		else
			bcopy(paddr + bv->bv_offset + skip, p, cnt);
		zfs_kunmap_atomic(paddr);

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
zfs_uiomove_iter(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio,
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
	 * Revert advancing the uio_iter.  This is set by zfs_uiocopy()
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
zfs_uiomove(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio)
{
	if (uio->uio_segflg == UIO_BVEC)
		return (zfs_uiomove_bvec(p, n, rw, uio));
#if defined(HAVE_VFS_IOV_ITER)
	else if (uio->uio_segflg == UIO_ITER)
		return (zfs_uiomove_iter(p, n, rw, uio, B_FALSE));
#endif
	else
		return (zfs_uiomove_iov(p, n, rw, uio));
}
EXPORT_SYMBOL(zfs_uiomove);

/*
 * Fault in the pages of the first n bytes specified by the uio structure.
 * 1 byte in each page is touched and the uio struct is unmodified. Any
 * error will terminate the process as this is only a best attempt to get
 * the pages resident.
 */
int
zfs_uio_prefaultpages(ssize_t n, zfs_uio_t *uio)
{
	if (uio->uio_segflg == UIO_SYSSPACE || uio->uio_segflg == UIO_BVEC) {
		/* There's never a need to fault in kernel pages */
		return (0);
#if defined(HAVE_VFS_IOV_ITER)
	} else if (uio->uio_segflg == UIO_ITER) {
		/*
		 * At least a Linux 4.9 kernel, iov_iter_fault_in_readable()
		 * can be relied on to fault in user pages when referenced.
		 */
		if (iov_iter_fault_in_readable(uio->uio_iter, n))
			return (EFAULT);
#endif
	} else {
		/* Fault in all user pages */
		ASSERT3S(uio->uio_segflg, ==, UIO_USERSPACE);
		const struct iovec *iov = uio->uio_iov;
		int iovcnt = uio->uio_iovcnt;
		size_t skip = uio->uio_skip;
		uint8_t tmp;
		caddr_t p;

		for (; n > 0 && iovcnt > 0; iov++, iovcnt--, skip = 0) {
			ulong_t cnt = MIN(iov->iov_len - skip, n);
			/* empty iov */
			if (cnt == 0)
				continue;
			n -= cnt;
			/* touch each page in this segment. */
			p = iov->iov_base + skip;
			while (cnt) {
				if (get_user(tmp, (uint8_t *)p))
					return (EFAULT);
				ulong_t incr = MIN(cnt, PAGESIZE);
				p += incr;
				cnt -= incr;
			}
			/* touch the last byte in case it straddles a page. */
			p--;
			if (get_user(tmp, (uint8_t *)p))
				return (EFAULT);
		}
	}

	return (0);
}
EXPORT_SYMBOL(zfs_uio_prefaultpages);

/*
 * The same as zfs_uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
int
zfs_uiocopy(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio, size_t *cbytes)
{
	zfs_uio_t uio_copy;
	int ret;

	bcopy(uio, &uio_copy, sizeof (zfs_uio_t));

	if (uio->uio_segflg == UIO_BVEC)
		ret = zfs_uiomove_bvec(p, n, rw, &uio_copy);
#if defined(HAVE_VFS_IOV_ITER)
	else if (uio->uio_segflg == UIO_ITER)
		ret = zfs_uiomove_iter(p, n, rw, &uio_copy, B_TRUE);
#endif
	else
		ret = zfs_uiomove_iov(p, n, rw, &uio_copy);

	*cbytes = uio->uio_resid - uio_copy.uio_resid;

	return (ret);
}
EXPORT_SYMBOL(zfs_uiocopy);

/*
 * Drop the next n chars out of *uio.
 */
void
zfs_uioskip(zfs_uio_t *uio, size_t n)
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
EXPORT_SYMBOL(zfs_uioskip);

#endif /* _KERNEL */
