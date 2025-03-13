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
#include <sys/uio_impl.h>
#include <sys/vnode.h>
#include <sys/zfs_znode.h>
#include <sys/byteorder.h>
#include <sys/lock.h>
#include <sys/vm.h>
#include <vm/vm_map.h>

static void
zfs_freeuio(struct uio *uio)
{
#if __FreeBSD_version > 1500013
	freeuio(uio);
#else
	free(uio, M_IOV);
#endif
}

int
zfs_uiomove(void *cp, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio)
{
	ASSERT3U(zfs_uio_rw(uio), ==, dir);
	return (uiomove(cp, (int)n, GET_UIO_STRUCT(uio)));
}

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
		zfs_freeuio(uio_clone);
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
	ASSERT3U(zfs_uio_rw(uio), ==, dir);
	return (vn_io_fault_uiomove(p, n, GET_UIO_STRUCT(uio)));
}

/*
 * Check if the uio is page-aligned in memory.
 */
boolean_t
zfs_uio_page_aligned(zfs_uio_t *uio)
{
	const struct iovec *iov = GET_UIO_STRUCT(uio)->uio_iov;

	for (int i = zfs_uio_iovcnt(uio); i > 0; iov++, i--) {
		uintptr_t addr = (uintptr_t)iov->iov_base;
		size_t size = iov->iov_len;
		if ((addr & (PAGE_SIZE - 1)) || (size & (PAGE_SIZE - 1))) {
				return (B_FALSE);
		}
	}

	return (B_TRUE);
}

static void
zfs_uio_set_pages_to_stable(zfs_uio_t *uio)
{
	ASSERT3P(uio->uio_dio.pages, !=, NULL);
	ASSERT3S(uio->uio_dio.npages, >, 0);

	for (int i = 0; i < uio->uio_dio.npages; i++) {
		vm_page_t page = uio->uio_dio.pages[i];
		ASSERT3P(page, !=, NULL);

		MPASS(page == PHYS_TO_VM_PAGE(VM_PAGE_TO_PHYS(page)));
		vm_page_busy_acquire(page, VM_ALLOC_SBUSY);
		pmap_remove_write(page);
	}
}

static void
zfs_uio_release_stable_pages(zfs_uio_t *uio)
{
	ASSERT3P(uio->uio_dio.pages, !=, NULL);
	for (int i = 0; i < uio->uio_dio.npages; i++) {
		vm_page_t page = uio->uio_dio.pages[i];

		ASSERT3P(page, !=, NULL);
		vm_page_sunbusy(page);
	}
}

/*
 * If the operation is marked as read, then we are stating the pages will be
 * written to and must be given write access.
 */
static int
zfs_uio_hold_pages(unsigned long start, size_t len, int nr_pages,
    zfs_uio_rw_t rw, vm_page_t *pages)
{
	vm_map_t map;
	vm_prot_t prot;
	int count;

	map = &curthread->td_proc->p_vmspace->vm_map;
	ASSERT3S(len, >, 0);

	prot = rw == UIO_READ ? (VM_PROT_READ | VM_PROT_WRITE) : VM_PROT_READ;
	count = vm_fault_quick_hold_pages(map, start, len, prot, pages,
	    nr_pages);

	return (count);
}

void
zfs_uio_free_dio_pages(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	ASSERT(uio->uio_extflg & UIO_DIRECT);
	ASSERT3P(uio->uio_dio.pages, !=, NULL);
	ASSERT(zfs_uio_rw(uio) == rw);

	if (rw == UIO_WRITE)
		zfs_uio_release_stable_pages(uio);

	vm_page_unhold_pages(&uio->uio_dio.pages[0],
	    uio->uio_dio.npages);

	kmem_free(uio->uio_dio.pages,
	    uio->uio_dio.npages * sizeof (vm_page_t));
}

static int
zfs_uio_get_user_pages(unsigned long start, int nr_pages,
    size_t len, zfs_uio_rw_t rw, vm_page_t *pages)
{
	int count;

	count = zfs_uio_hold_pages(start, len, nr_pages, rw, pages);

	if (count != nr_pages) {
		if (count > 0)
			vm_page_unhold_pages(pages, count);
		return (0);
	}

	ASSERT3S(count, ==, nr_pages);

	return (count);
}

static int
zfs_uio_iov_step(struct iovec v, zfs_uio_t *uio, int *numpages)
{
	unsigned long addr = (unsigned long)(v.iov_base);
	size_t len = v.iov_len;
	int n = DIV_ROUND_UP(len, PAGE_SIZE);

	int res = zfs_uio_get_user_pages(
	    P2ALIGN_TYPED(addr, PAGE_SIZE, unsigned long), n, len,
	    zfs_uio_rw(uio), &uio->uio_dio.pages[uio->uio_dio.npages]);

	if (res != n)
		return (SET_ERROR(EFAULT));

	ASSERT3U(len, ==, res * PAGE_SIZE);
	*numpages = res;
	return (0);
}

static int
zfs_uio_get_dio_pages_impl(zfs_uio_t *uio)
{
	const struct iovec *iovp = GET_UIO_STRUCT(uio)->uio_iov;
	size_t len = zfs_uio_resid(uio);

	for (int i = 0; i < zfs_uio_iovcnt(uio); i++) {
		struct iovec iov;
		int numpages = 0;

		if (iovp->iov_len == 0) {
			iovp++;
			continue;
		}
		iov.iov_len = MIN(len, iovp->iov_len);
		iov.iov_base = iovp->iov_base;
		int error = zfs_uio_iov_step(iov, uio, &numpages);

		if (error)
			return (error);

		uio->uio_dio.npages += numpages;
		len -= iov.iov_len;
		iovp++;
	}

	ASSERT0(len);

	return (0);
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
	int error = 0;
	int npages = DIV_ROUND_UP(zfs_uio_resid(uio), PAGE_SIZE);
	size_t size = npages * sizeof (vm_page_t);

	ASSERT(zfs_uio_rw(uio) == rw);

	uio->uio_dio.pages = kmem_alloc(size, KM_SLEEP);

	error = zfs_uio_get_dio_pages_impl(uio);

	if (error) {
		vm_page_unhold_pages(&uio->uio_dio.pages[0],
		    uio->uio_dio.npages);
		kmem_free(uio->uio_dio.pages, size);
		return (error);
	}

	ASSERT3S(uio->uio_dio.npages, >, 0);

	/*
	 * Since we will be writing the user pages we must make sure that
	 * they are stable. That way the contents of the pages can not change
	 * while we are doing: compression, checksumming, encryption, parity
	 * calculations or deduplication.
	 */
	if (zfs_uio_rw(uio) == UIO_WRITE)
		zfs_uio_set_pages_to_stable(uio);

	uio->uio_extflg |= UIO_DIRECT;

	return (0);
}
