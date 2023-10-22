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
	/* Type B: Internal uio */
	const struct iovec	*uio_iov;
	int			uio_iovcnt;
	off_t			uio_loffset;
	zfs_uio_seg_t		uio_segflg;
	boolean_t		uio_fault_disable;
	uint16_t		uio_fmode;
	uint16_t		uio_extflg;
	ssize_t			uio_resid;
	size_t			uio_skip;
	zfs_uio_func		uio_iofunc;
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

static inline zfs_uio_seg_t
zfs_uio_segflg(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		return (uio_isuserspace(uio->uio_xnu) ?
		    UIO_USERSPACE : UIO_SYSSPACE);
	return (uio->uio_segflg);
}

static inline void
zfs_uio_setrw(zfs_uio_t *uio, zfs_uio_rw_t inout)
{
	if (uio->uio_iov == NULL)
		uio_setrw(uio->uio_xnu, inout);
}

static inline int
zfs_uio_iovcnt(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		return (uio_iovcnt(uio->uio_xnu));
	return (uio->uio_iovcnt);
}

static inline off_t
zfs_uio_offset(zfs_uio_t *uio)
{
	if (uio->uio_iov == NULL)
		return (uio_offset(uio->uio_xnu));
	return (uio->uio_loffset);
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
zfs_uio_advance(zfs_uio_t *uio, size_t size)
{
	if (uio->uio_iov == NULL) {
		uio_update(uio->uio_xnu, size);
	} else {
		uio->uio_resid -= size;
		uio->uio_loffset += size;
	}
}

/* zfs_uio_iovlen(uio, 0) = uio_curriovlen() */
static inline uint64_t
zfs_uio_iovlen(zfs_uio_t *uio, unsigned int idx)
{
	if (uio->uio_iov == NULL) {
		user_size_t iov_len;
		if (uio_getiov(uio->uio_xnu, idx, NULL, &iov_len) < 0)
			return (0ULL);
		return (iov_len);
	}
	return (uio->uio_iov[idx].iov_len);
}

static inline void *
zfs_uio_iovbase(zfs_uio_t *uio, unsigned int idx)
{
	if (uio->uio_iov == NULL) {
		user_addr_t iov_base;
		if (uio_getiov(uio->uio_xnu, idx, &iov_base, NULL) < 0)
			return (NULL);
		return ((void *)iov_base);
	}
	return (uio->uio_iov[(idx)].iov_base);
}

static inline void
zfs_uio_iovec_init(zfs_uio_t *uio, const struct iovec *iov,
    unsigned long nr_segs, off_t offset, zfs_uio_seg_t seg, ssize_t resid,
    size_t skip)
{
	uio->uio_iov = iov;
	uio->uio_iovcnt = nr_segs;
	uio->uio_loffset = offset;
	uio->uio_segflg = seg;
	uio->uio_fmode = 0;
	uio->uio_extflg = 0;
	uio->uio_resid = resid;
	uio->uio_skip = skip;
	uio->uio_iofunc = NULL;
}

static inline void
zfs_uio_iovec_func_init(zfs_uio_t *uio, const struct iovec *iov,
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
