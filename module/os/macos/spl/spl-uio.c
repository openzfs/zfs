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
 *
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 * Copyright (C) 2021 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/debug.h>
#include <sys/sysmacros.h>
#include <sys/uio.h>

struct iovec empty_iov = { 0 };

static int
zfs_uiomove_iov(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio)
{
	const struct iovec *iov = uio->uio_iov;
	size_t skip = uio->uio_skip;
	int cnt;

	while (n && uio->uio_resid) {
		cnt = MIN(iov->iov_len - skip, n);
		switch ((int)uio->uio_segflg) {
			case UIO_SYSSPACE:
				if (rw == UIO_READ)
					memcpy(iov->iov_base + skip, p, cnt);
				else
					memcpy((void *)p, iov->iov_base + skip,
					    cnt);
				break;

			case UIO_FUNCSPACE:
				VERIFY3P(uio->uio_iofunc, !=, NULL);
				cnt = uio->uio_iofunc(p, skip, cnt, rw,
				    iov->iov_base);
				break;

			default:
				VERIFY(0);
				return (-1);
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

int
zfs_uiomove(const char *p, size_t n, enum uio_rw rw, zfs_uio_t *uio)
{
	int result;

	if (uio->uio_iov == NULL) {
		uio_setrw(uio->uio_xnu, rw);
		result = uiomove(p, n, uio->uio_xnu);
		return (SET_ERROR(result));
	}

	result = zfs_uiomove_iov((void *)p, n, rw, uio);
	return (SET_ERROR(result));
}

/*
 * same as uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
int
zfs_uiocopy(const char *p, size_t n, enum uio_rw rw, zfs_uio_t *uio,
    size_t *cbytes)
{
	int result;
	if (uio->uio_iov == NULL) {
		struct uio *nuio = uio_duplicate(uio->uio_xnu);
		unsigned long long x = uio_resid(uio->uio_xnu);
		if (!nuio)
			return (ENOMEM);
		uio_setrw(nuio, rw);
		result = uiomove(p, n, nuio);
		*cbytes = x-uio_resid(nuio);
		uio_free(nuio);
		return (result);
	}

	zfs_uio_t uio_copy;

	memcpy(&uio_copy, uio, sizeof (zfs_uio_t));
	result = zfs_uiomove_iov((void *)p, n, rw, &uio_copy);

	*cbytes = uio->uio_resid - uio_copy.uio_resid;

	return (result);
}

void
zfs_uioskip(zfs_uio_t *uio, size_t n)
{
	if (uio->uio_iov == NULL) {
		uio_update(uio->uio_xnu, n);
	} else {
		if (n > uio->uio_resid)
			return;
		uio->uio_skip += n;
		while (uio->uio_iovcnt &&
		    uio->uio_skip >= uio->uio_iov->iov_len) {
			uio->uio_skip -= uio->uio_iov->iov_len;
			uio->uio_iov++;
			uio->uio_iovcnt--;
		}
		uio->uio_loffset += n;
		uio->uio_resid -= n;
	}
}

int
zfs_uio_prefaultpages(ssize_t n, zfs_uio_t *uio)
{
	return (0);
}
