// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright 2010 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */

/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */


#ifndef _SPL_UIO_H
#define	_SPL_UIO_H

#include_next <sys/uio.h>
#include <sys/types.h>
#include <sys/debug.h>

#ifdef  __cplusplus
extern "C" {
#endif

/*
 * uio_extflg: extended flags
 */
#define	UIO_DIRECT		(1ULL << 0) /* Direct I/O request */
#define	UIO_UNCACHED	(1ULL << 1) /* Caller will not reuse data */
/*
 * UIO_DIO_DENY: the zpl caller declines Direct I/O for this request (e.g. a
 * file handle that already hit a benign DIO read verify failure).
 * UIO_DIO_CKSUM_RETRIED: set by zfs_read when a DIO read verify failed but
 * the buffered re-read succeeded -- a recycled O_DIRECT buffer, not an
 * on-disk error.
 */
#define	UIO_DIO_DENY	(1ULL << 2)
#define	UIO_DIO_CKSUM_RETRIED (1ULL << 3)

typedef struct iovec iovec_t;

typedef enum uio_seg zfs_uio_seg_t;
typedef enum uio_rw zfs_uio_rw_t;

/*
 * Invent a 3rd kind of uio for iokit.
 * Used by zvol_os.c to issue IO to an IOMemoryDescriptor*,
 * Where we, in spl-uio.c's uiomove, issue iomem->writeBytes
 * (readBytes) instead. Offset is always from 0, counting up.
 * and iovbase is the iomem void *.
 */
#define	UIO_FUNCSPACE 99

typedef size_t (*zfs_uio_func)(char *addr, uint64_t offset, size_t len,
    zfs_uio_rw_t rw, const void *privptr);

/*
 * This structure is used when doing Direct I/O.
 */
typedef void vm_page_t;
typedef struct {
	vm_page_t	*pages;
	int		npages;
} zfs_uio_dio_t;

/*
 * Hybrid uio, use OS uio for IO and communicating with XNU
 * and internal uio for ZFS / crypto. The default mode is
 * ZFS style, as zio_crypt.c creates uios on the stack, and
 * they are uninitialised. However, all XNU entries will use
 * ZFS_UIO_INIT_XNU(), so we can set uio_iov = NULL, to signify
 * that it is a XNU uio. ZFS uio will always set uio_iov before
 * it can use them.
 */
typedef struct zfs_uio {
	/* Type A: XNU uio. */
	struct uio		*uio_xnu;
	/*
	 * Type A only: uio_getiov() copies out rather than exposing the
	 * XNU iovec, so zfs_uio_iovbase()/zfs_uio_iovlen() refresh this
	 * shadow copy of the requested iovec and point at it.
	 */
	struct iovec		uio_xnu_iov;
	/* Type B: Internal uio */
	struct iovec		*uio_iov;
	int			uio_iovcnt;
	off_t			uio_loffset;
	off_t			uio_soffset;
	zfs_uio_seg_t		uio_segflg;
	boolean_t		uio_fault_disable;
	uint16_t		uio_fmode;
	uint16_t		uio_extflg;
	ssize_t			uio_resid;
	size_t			uio_skip;
	zfs_uio_func		uio_iofunc;
	zfs_uio_dio_t	uio_dio;
} zfs_uio_t;


/*
 * Given a XNU "uio", we wrap it in a ZFS "uio", and set iov to NULL
 * to indicate we should call XNU methods. However, sometimes, XNU
 * passes a NULL uio (e.g. lookup size in listxattr) so we need to
 * make the uio look like a ZFS uio for methods like setoffset() to
 * work.
 */
extern struct iovec empty_iov;

#define	ZFS_UIO_INIT_XNU(U, X) \
	zfs_uio_t _U = { 0 }; \
	zfs_uio_t *U = &_U; \
	if ((X) != NULL) { \
		(U)->uio_iov = NULL; \
		(U)->uio_xnu = X; \
	} else { \
		(U)->uio_iov = &empty_iov; \
	}

/*
 * zfs_uio_iov(), zfs_uio_iovcnt(), zfs_uio_segflg(), zfs_uio_iovbase() and
 * zfs_uio_iovlen() are plain field macros on every other platform, and the
 * shared zio_crypt.c / zio_crypt_os_icp.c assign through them:
 *
 *	zfs_uio_iov(u) = kmem_zalloc(...);
 *	zfs_uio_iovcnt(u) = n;  zfs_uio_segflg(u) = UIO_SYSSPACE;
 *	zfs_uio_iovbase(u, i) = p;  zfs_uio_iovlen(u, i) = len;
 *
 * Only Type B (internal) uios are ever assigned through; Type A (XNU) uios
 * are only read. So each accessor dereferences a pointer-returning helper:
 * for Type B it points at the real field (assignable), for Type A it
 * refreshes a shadow of the value from the XNU uio and points at that
 * (readable; a write would just be lost).
 */
#define	zfs_uio_iov(u)		((u)->uio_iov)

static inline zfs_uio_seg_t *
zfs_uio_segflg_p(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		uio->uio_segflg = uio_isuserspace(uio->uio_xnu) ?
		    UIO_USERSPACE : UIO_SYSSPACE;
	return (&uio->uio_segflg);
}
#define	zfs_uio_segflg(u)	(*zfs_uio_segflg_p(u))

static inline int *
zfs_uio_iovcnt_p(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		uio->uio_iovcnt = uio_iovcnt(uio->uio_xnu);
	return (&uio->uio_iovcnt);
}
#define	zfs_uio_iovcnt(u)	(*zfs_uio_iovcnt_p(u))

static inline struct iovec *
zfs_uio_iovec_p(zfs_uio_t *uio, unsigned int idx)
{
	if (uio->uio_iov == NULL) {
		user_addr_t base = 0;
		user_size_t len = 0;
		if (uio_getiov(uio->uio_xnu, idx, &base, &len) < 0) {
			base = 0;
			len = 0;
		}
		uio->uio_xnu_iov.iov_base = (void *)base;
		uio->uio_xnu_iov.iov_len = len;
		return (&uio->uio_xnu_iov);
	}
	return (&uio->uio_iov[idx]);
}
/* zfs_uio_iovlen(uio, 0) = uio_curriovlen() */
#define	zfs_uio_iovlen(u, idx)	(zfs_uio_iovec_p((u), (idx))->iov_len)
#define	zfs_uio_iovbase(u, idx)	(zfs_uio_iovec_p((u), (idx))->iov_base)

static inline void
zfs_uio_setrw(zfs_uio_t *uio, zfs_uio_rw_t inout)
{
	if (uio->uio_iov == NULL)
		uio_setrw(uio->uio_xnu, inout);
}

static inline off_t
zfs_uio_offset(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		return (uio_offset(uio->uio_xnu));
	return (uio->uio_loffset);
}

static inline off_t
zfs_uio_soffset(zfs_uio_t *uio)
{
	return (uio->uio_soffset);
}

static inline size_t
zfs_uio_resid(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		return (uio_resid(uio->uio_xnu));
	return (uio->uio_resid);
}

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, off_t off)
{
	if (uio->uio_iov == NULL) {
		uio_setoffset(uio->uio_xnu, off);
		return;
	}
	uio->uio_loffset = off;
}

static inline void
zfs_uio_setsoffset(zfs_uio_t *uio, offset_t off)
{
	uio->uio_soffset = off;
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, size_t size)
{
	if (uio->uio_iov == NULL) {
		uio_update(uio->uio_xnu, size);
	} else {
		uio->uio_resid -= size;
		uio->uio_loffset += size;
	}
}

static inline void
zfs_uio_iovec_init(zfs_uio_t *uio, struct iovec *iov,
    unsigned long nr_segs, off_t offset, zfs_uio_seg_t seg, ssize_t resid,
    size_t skip)
{
	uio->uio_iov = iov;
	uio->uio_iovcnt = nr_segs;
	uio->uio_loffset = offset;
	uio->uio_soffset = offset;
	uio->uio_segflg = seg;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_resid = resid;
	uio->uio_skip = skip;
	uio->uio_iofunc = NULL;
}

static inline void
zfs_uio_iovec_func_init(zfs_uio_t *uio, struct iovec *iov,
    unsigned long nr_segs, off_t offset, zfs_uio_seg_t seg, ssize_t resid,
    size_t skip, zfs_uio_func func)
{
	zfs_uio_iovec_init(uio, iov, nr_segs, offset, seg, resid, skip);
	uio->uio_iofunc = func;
}

extern int zfs_uio_prefaultpages(ssize_t, zfs_uio_t *);
#define	zfs_uio_fault_disable(uio, set)
#define	zfs_uio_fault_move(p, n, rw, u) zfs_uiomove((p), (n), (rw), (u))

ssize_t readv(int, const struct iovec *, int);
ssize_t writev(int, const struct iovec *, int);

#ifdef  __cplusplus
}
#endif
#endif /* SPL_UIO_H */
