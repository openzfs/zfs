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
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/zfs_debug.h>

static int
zfs_uiomove_iov(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio)
{
	const struct iovec *iov = uio->uio_iov;
	size_t skip = uio->uio_skip;
	int cnt;

	while (n && uio->uio_resid) {
		cnt = MIN(iov->iov_len - skip, n);
		switch (uio->uio_segflg) {
		case UIO_SYSSPACE:
			if (rw == UIO_READ)
				memcpy(iov->iov_base + skip, p, cnt);
			else
				memcpy((void *)p, iov->iov_base + skip,
				    cnt);
			break;
		default:
		/* Probably have no uio from userland in Windows */
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
	int result = 0;
	zfs_uio_t uio_copy;

	memcpy(&uio_copy, uio, sizeof (zfs_uio_t));
	try {
		result = zfs_uiomove_iov((void *)p, n, rw, &uio_copy);
	} except(EXCEPTION_EXECUTE_HANDLER) {
		result = EIO;
	}

	*cbytes = uio->uio_resid - uio_copy.uio_resid;

	return (result);
}

void
zfs_uioskip(zfs_uio_t *uio, size_t n)
{
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

int
zfs_uio_prefaultpages(ssize_t n, zfs_uio_t *uio)
{
	return (0);
}

/*
 * Check if the uio is page-aligned in memory.
 */
boolean_t
zfs_uio_page_aligned(zfs_uio_t *uio)
{
	for (int i = zfs_uio_iovcnt(uio); i > 0; i--) {
		uintptr_t addr = (uintptr_t)zfs_uio_iovbase(uio, i);
		size_t size = zfs_uio_iovlen(uio, i);
		if ((addr & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

void
zfs_uio_free_dio_pages(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	(void) uio;
	(void) rw;
}

/*
 * This function holds user pages into the kernel. In the event that the user
 * pages are not successfully held an error value is returned.
 *
 * On success, 0 is returned.
 */
int
zfs_uio_get_dio_pages_alloc(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	(void) uio;
	(void) rw;
	return (ENOTSUP);
}
