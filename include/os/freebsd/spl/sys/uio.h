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
#include <sys/_uio.h>
#include <sys/debug.h>

typedef	struct iovec	iovec_t;
typedef	enum uio_seg	zfs_uio_seg_t;
typedef	enum uio_rw	zfs_uio_rw_t;

typedef struct zfs_uio {
	struct uio	*uio;
} zfs_uio_t;

#define	GET_UIO_STRUCT(u)	(u)->uio
#define	zfs_uio_segflg(u)	GET_UIO_STRUCT(u)->uio_segflg
#define	zfs_uio_offset(u)	GET_UIO_STRUCT(u)->uio_offset
#define	zfs_uio_resid(u)	GET_UIO_STRUCT(u)->uio_resid
#define	zfs_uio_iovcnt(u)	GET_UIO_STRUCT(u)->uio_iovcnt
#define	zfs_uio_iovlen(u, idx)	GET_UIO_STRUCT(u)->uio_iov[(idx)].iov_len
#define	zfs_uio_iovbase(u, idx)	GET_UIO_STRUCT(u)->uio_iov[(idx)].iov_base
#define	zfs_uio_td(u)		GET_UIO_STRUCT(u)->uio_td
#define	zfs_uio_rw(u)		GET_UIO_STRUCT(u)->uio_rw
#define	zfs_uio_fault_disable(u, set)
#define	zfs_uio_prefaultpages(size, u)	(0)


static __inline void
zfs_uio_init(zfs_uio_t *uio, struct uio *uio_s)
{
	GET_UIO_STRUCT(uio) = uio_s;
}

static __inline void
zfs_uio_setoffset(zfs_uio_t *uio, offset_t off)
{
	zfs_uio_offset(uio) = off;
}

static __inline int
zfs_uiomove(void *cp, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio)
{
	ASSERT(zfs_uio_rw(uio) == dir);
	return (uiomove(cp, (int)n, GET_UIO_STRUCT(uio)));
}

int zfs_uiocopy(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *uio,
    size_t *cbytes);
void zfs_uioskip(zfs_uio_t *uiop, size_t n);
int zfs_uio_fault_move(void *p, size_t n, zfs_uio_rw_t dir, zfs_uio_t *uio);

static inline void
zfs_uio_iov_at_index(zfs_uio_t *uio, uint_t idx, void **base, uint64_t *len)
{
	*base = zfs_uio_iovbase(uio, idx);
	*len = zfs_uio_iovlen(uio, idx);
}

static inline void
zfs_uio_advance(zfs_uio_t *uio, size_t size)
{
	zfs_uio_resid(uio) -= size;
	zfs_uio_offset(uio) += size;
}

static inline offset_t
zfs_uio_index_at_offset(zfs_uio_t *uio, offset_t off, uint_t *vec_idx)
{
	*vec_idx = 0;
	while (*vec_idx < zfs_uio_iovcnt(uio) &&
	    off >= zfs_uio_iovlen(uio, *vec_idx)) {
		off -= zfs_uio_iovlen(uio, *vec_idx);
		(*vec_idx)++;
	}

	return (off);
}

#endif /* !_STANDALONE */

#endif	/* !_OPENSOLARIS_SYS_UIO_H_ */
