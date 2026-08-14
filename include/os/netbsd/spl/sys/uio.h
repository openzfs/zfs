// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2010 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_UIO_H_
#define	_OPENSOLARIS_SYS_UIO_H_

#ifndef _STANDALONE

#include_next <sys/uio.h>
#include <sys/debug.h>
#include <sys/sysmacros.h>

#ifdef _KERNEL
typedef struct vm_page *vm_page_t;
#include <sys/proc.h>
#include <sys/systm.h>
struct vmspace;
#endif

/*
 * uio_extflg: extended flags
 *
 * UIO_DIRECT / UIO_UNCACHED match Linux and FreeBSD (openzfs/zfs
 * amotin uncached-I/O).  Common zfs_vnops.c tests
 * (ioflag & O_DIRECT) || (uio->uio_extflg & UIO_UNCACHED).
 * NetBSD has no native uncached-uio bit; define the constant so
 * that path compiles.
 */
#define	UIO_DIRECT	0x0001	/* Direct I/O request */
#define	UIO_UNCACHED	0x0002	/* Caller will not reuse the data */

typedef	struct iovec	iovec_t;
typedef	enum uio_seg	zfs_uio_seg_t;
typedef	enum uio_rw	zfs_uio_rw_t;

/*
 * Direct I/O user pages.  Same shape as FreeBSD: the page array is
 * the hold.  Do not snapshot iov / vmspace — native uio has those,
 * and uvm_loan/unloan holds by vm_page_t like vm_page_unhold_pages.
 */
typedef struct {
	vm_page_t	*pages;
	int		npages;
	/*
	 * Non-NULL for a direct read: struct zfs_nb_pin[npages],
	 * wired anons.  NULL for a direct write, which is a
	 * uvm_loan hold on pages[] instead.
	 */
	void		*pins;
} zfs_uio_dio_t;

#ifdef _KERNEL
void zfs_nb_dio_read_fallback_add(void);
#endif

/*
 * NetBSD struct uio has no uio_segflg / uio_td; keep those on the wrapper.
 */
typedef struct zfs_uio {
	struct uio	*uio;
	offset_t	uio_soffset;
	uint16_t	uio_extflg;
	zfs_uio_seg_t	uio_segflg;
	void		*uio_lwp;
	zfs_uio_dio_t	uio_dio;
} zfs_uio_t;

#define	GET_UIO_STRUCT(u)	(u)->uio
#define	zfs_uio_segflg(u)	((u)->uio_segflg)
#define	zfs_uio_offset(u)	GET_UIO_STRUCT(u)->uio_offset
#define	zfs_uio_resid(u)	GET_UIO_STRUCT(u)->uio_resid
#define	zfs_uio_iovcnt(u)	GET_UIO_STRUCT(u)->uio_iovcnt
#define	zfs_uio_iovlen(u, idx)	GET_UIO_STRUCT(u)->uio_iov[(idx)].iov_len
#define	zfs_uio_iovbase(u, idx)	GET_UIO_STRUCT(u)->uio_iov[(idx)].iov_base
#define	zfs_uio_td(u)		((u)->uio_lwp)
#define	zfs_uio_rw(u)		GET_UIO_STRUCT(u)->uio_rw
#define	zfs_uio_soffset(u)	(u)->uio_soffset
#define	zfs_uio_vmspace(u)	GET_UIO_STRUCT(u)->uio_vmspace

/*
 * As on FreeBSD, zfs_write() neither prefaults nor retries a faulted
 * copy.  zfs_netbsd_write() runs it with ZFS page-ins refused and,
 * on EFAULT, finishes the write from a kernel buffer filled with no
 * ZFS locks held.  See zfs_nb_write_user().
 */
#define	zfs_uio_fault_disable(u, set)
#define	zfs_uio_prefaultpages(size, u)	(0)
#define	zfs_uio_fault_move(p, n, rw, u)	zfs_uiomove((p), (n), (rw), (u))

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, offset_t off)
{
	zfs_uio_offset(uio) = off;
}

static inline void
zfs_uio_setsoffset(zfs_uio_t *uio, offset_t off)
{
	ASSERT3U(zfs_uio_offset(uio), ==, off);
	zfs_uio_soffset(uio) = off;
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, ssize_t size)
{
	zfs_uio_resid(uio) -= size;
	zfs_uio_offset(uio) += size;
}

static __inline void
zfs_uio_init(zfs_uio_t *uio, struct uio *uio_s)
{
	memset(uio, 0, sizeof (zfs_uio_t));
	if (uio_s != NULL) {
		GET_UIO_STRUCT(uio) = uio_s;
		zfs_uio_soffset(uio) = zfs_uio_offset(uio);
#ifdef _KERNEL
		if (zfs_uio_vmspace(uio) == vmspace_kernel())
			zfs_uio_segflg(uio) = UIO_SYSSPACE;
		else
			zfs_uio_segflg(uio) = UIO_USERSPACE;
#endif
	}
}

#endif /* !_STANDALONE */

#endif	/* _OPENSOLARIS_SYS_UIO_H_ */
