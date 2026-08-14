// SPDX-License-Identifier: CDDL-1.0

#include <sys/param.h>
#include <sys/proc.h>		/* struct vmspace; after param.h */
#include <sys/systm.h>
#include <sys/uio_impl.h>
#include <sys/vnode.h>
#include <sys/zfs_znode.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/sysmacros.h>
#include <sys/vm.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_loan.h>
#include <uvm/uvm_pmap.h>
#include "spl_uvm_pin.h"

int
zfs_uiomove(void *cp, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio)
{
	ASSERT3U(zfs_uio_rw(uio), ==, dir);
	return (uiomove(cp, n, GET_UIO_STRUCT(uio)));
}

int
zfs_uiocopy(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio, size_t *cbytes)
{
	struct uio *uo = GET_UIO_STRUCT(uio);
	struct iovec small_iov[8];
	struct iovec *iovs;
	struct uio clone;
	size_t resid;
	int error;
	int i;

	ASSERT3U(zfs_uio_rw(uio), ==, rw);
	if (uo->uio_iovcnt <= 8) {
		iovs = small_iov;
	} else {
		iovs = kmem_alloc(sizeof (struct iovec) * uo->uio_iovcnt,
		    KM_SLEEP);
	}
	for (i = 0; i < uo->uio_iovcnt; i++)
		iovs[i] = uo->uio_iov[i];
	clone = *uo;
	clone.uio_iov = iovs;
	resid = clone.uio_resid;
	error = uiomove(p, n, &clone);
	*cbytes = resid - clone.uio_resid;
	if (iovs != small_iov)
		kmem_free(iovs, sizeof (struct iovec) * uo->uio_iovcnt);
	return (error);
}

void
zfs_uioskip(zfs_uio_t *uio, size_t n)
{
	struct uio *uo = GET_UIO_STRUCT(uio);

	if (n > uo->uio_resid)
		return;
	uo->uio_resid -= n;
	uo->uio_offset += n;
	while (n > 0 && uo->uio_iovcnt > 0) {
		if (n < uo->uio_iov->iov_len) {
			uo->uio_iov->iov_base =
			    (char *)uo->uio_iov->iov_base + n;
			uo->uio_iov->iov_len -= n;
			break;
		}
		n -= uo->uio_iov->iov_len;
		uo->uio_iov++;
		uo->uio_iovcnt--;
	}
}

boolean_t
zfs_uio_page_aligned(zfs_uio_t *uio)
{
	const struct iovec *iov;
	int i;

	if (GET_UIO_STRUCT(uio) == NULL ||
	    zfs_uio_segflg(uio) != UIO_USERSPACE)
		return (B_FALSE);
	iov = GET_UIO_STRUCT(uio)->uio_iov;
	for (i = zfs_uio_iovcnt(uio); i > 0; iov++, i--) {
		if (!IS_P2ALIGNED(iov->iov_base, PAGE_SIZE) ||
		    !IS_P2ALIGNED(iov->iov_len, PAGE_SIZE))
			return (B_FALSE);
	}
	return (B_TRUE);
}

void
zfs_uio_free_dio_pages(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	size_t bytes;

	if ((uio->uio_extflg & UIO_DIRECT) == 0)
		return;
	if (uio->uio_dio.pins != NULL) {
		zfs_nb_vsunpin(uio->uio_dio.pins, uio->uio_dio.npages);
		kmem_free(uio->uio_dio.pins,
		    (size_t)uio->uio_dio.npages * sizeof (struct zfs_nb_pin));
		uio->uio_dio.pins = NULL;
	} else if (uio->uio_dio.pages != NULL && uio->uio_dio.npages > 0 &&
	    rw == UIO_WRITE) {
		uvm_unloan(uio->uio_dio.pages, uio->uio_dio.npages,
		    UVM_LOAN_TOPAGE);
	}
	if (uio->uio_dio.pages != NULL) {
		bytes = (size_t)uio->uio_dio.npages * sizeof (vm_page_t);
		kmem_free(uio->uio_dio.pages, bytes);
		uio->uio_dio.pages = NULL;
		uio->uio_dio.npages = 0;
	}
	uio->uio_extflg &= ~UIO_DIRECT;
}

int
zfs_uio_get_dio_pages_alloc(zfs_uio_t *uio, zfs_uio_rw_t rw)
{
	struct uio *su = GET_UIO_STRUCT(uio);
	struct vmspace *vm;
	const struct iovec *iovp;
	vm_page_t *pages;
	size_t resid, iovlen, bytes;
	int i, n, npages, np, error;
	vaddr_t va;

	ASSERT(zfs_uio_rw(uio) == rw);
	if (su == NULL || zfs_uio_segflg(uio) != UIO_USERSPACE)
		return (EOPNOTSUPP);

	vm = zfs_uio_vmspace(uio);
	if (vm == NULL)
		return (EOPNOTSUPP);
	npages = DIV_ROUND_UP(zfs_uio_resid(uio), PAGE_SIZE);
	if (npages <= 0)
		return (EINVAL);

	bytes = (size_t)npages * sizeof (vm_page_t);
	pages = kmem_alloc(bytes, KM_SLEEP);

	/*
	 * Writes: uvm_loan is a read-only loan.  If the user scribbles
	 * on the buffer mid-I/O the loan breaks and ZFS keeps the
	 * original bytes.  Reads must not loan: the loan can be the
	 * shared zero page, another file's cache page, or an anon
	 * still shared with a child.  Pin a private anon instead.
	 * EOPNOTSUPP means this mapping cannot be pinned (MAP_SHARED);
	 * zfs_setup_direct() then runs the read uncached.
	 */
	if (rw == UIO_READ) {
		struct zfs_nb_pin *pins;

		pins = kmem_zalloc((size_t)npages * sizeof (*pins), KM_SLEEP);
		iovp = su->uio_iov;
		resid = zfs_uio_resid(uio);
		n = 0;
		for (i = 0; i < zfs_uio_iovcnt(uio) && resid > 0; i++, iovp++) {
			if (iovp->iov_len == 0)
				continue;
			iovlen = MIN(resid, iovp->iov_len);
			va = (vaddr_t)iovp->iov_base;
			if (!IS_P2ALIGNED(va, PAGE_SIZE) ||
			    !IS_P2ALIGNED(iovlen, PAGE_SIZE)) {
				zfs_nb_vsunpin(pins, n);
				kmem_free(pins,
				    (size_t)npages * sizeof (*pins));
				kmem_free(pages, bytes);
				return (EFAULT);
			}
			np = iovlen >> PAGE_SHIFT;
			error = zfs_nb_vspin(vm, va, np, &pins[n]);
			if (error != 0) {
				zfs_nb_vsunpin(pins, n);
				kmem_free(pins,
				    (size_t)npages * sizeof (*pins));
				kmem_free(pages, bytes);
				return (error);
			}
			int pi;

			for (pi = 0; pi < np; pi++)
				pages[n + pi] = pins[n + pi].np_pg;
			n += np;
			resid -= iovlen;
		}
		if (n != npages) {
			zfs_nb_vsunpin(pins, n);
			kmem_free(pins, (size_t)npages * sizeof (*pins));
			kmem_free(pages, bytes);
			return (EFAULT);
		}
		uio->uio_dio.pages = pages;
		uio->uio_dio.npages = n;
		uio->uio_dio.pins = pins;
		uio->uio_extflg |= UIO_DIRECT;
		return (0);
	}

	iovp = su->uio_iov;
	resid = zfs_uio_resid(uio);
	n = 0;
	for (i = 0; i < zfs_uio_iovcnt(uio) && resid > 0; i++, iovp++) {
		if (iovp->iov_len == 0)
			continue;
		iovlen = MIN(resid, iovp->iov_len);
		va = (vaddr_t)iovp->iov_base;
		if (!IS_P2ALIGNED(va, PAGE_SIZE) ||
		    !IS_P2ALIGNED(iovlen, PAGE_SIZE)) {
			if (n > 0)
				uvm_unloan(pages, n, UVM_LOAN_TOPAGE);
			kmem_free(pages, bytes);
			return (EFAULT);
		}
		np = iovlen >> PAGE_SHIFT;
		error = uvm_loan(&vm->vm_map, va, iovlen, &pages[n],
		    UVM_LOAN_TOPAGE);
		if (error != 0) {
			if (n > 0)
				uvm_unloan(pages, n, UVM_LOAN_TOPAGE);
			kmem_free(pages, bytes);
			return (EFAULT);
		}
		n += np;
		resid -= iovlen;
	}
	if (n != npages) {
		if (n > 0)
			uvm_unloan(pages, n, UVM_LOAN_TOPAGE);
		kmem_free(pages, bytes);
		return (EFAULT);
	}
	uio->uio_dio.pages = pages;
	uio->uio_dio.npages = n;
	uio->uio_dio.pins = NULL;
	uio->uio_extflg |= UIO_DIRECT;
	return (0);
}
