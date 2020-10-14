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



#define	uio_loffset	uio_offset

typedef	struct uio	uio_t;
typedef	struct iovec	iovec_t;
typedef	enum uio_seg	uio_seg_t;

typedef enum xuio_type {
	UIOTYPE_ASYNCIO,
	UIOTYPE_ZEROCOPY
} xuio_type_t;

typedef struct xuio {
	uio_t	xu_uio;

	/* Extended uio fields */
	enum xuio_type xu_type; /* What kind of uio structure? */
	union {
		struct {
			int xu_zc_rw;
			void *xu_zc_priv;
		} xu_zc;
	} xu_ext;
} xuio_t;

#define	XUIO_XUZC_PRIV(xuio)	xuio->xu_ext.xu_zc.xu_zc_priv
#define	XUIO_XUZC_RW(xuio)	xuio->xu_ext.xu_zc.xu_zc_rw

static __inline int
zfs_uiomove(void *cp, size_t n, enum uio_rw dir, uio_t *uio)
{

	ASSERT(uio->uio_rw == dir);
	return (uiomove(cp, (int)n, uio));
}
#define	uiomove(cp, n, dir, uio)	zfs_uiomove((cp), (n), (dir), (uio))

int uiocopy(void *p, size_t n, enum uio_rw rw, struct uio *uio, size_t *cbytes);
void uioskip(uio_t *uiop, size_t n);

#define	uio_segflg(uio)			(uio)->uio_segflg
#define	uio_offset(uio)			(uio)->uio_loffset
#define	uio_resid(uio)			(uio)->uio_resid
#define	uio_iovcnt(uio)			(uio)->uio_iovcnt
#define	uio_iovlen(uio, idx)		(uio)->uio_iov[(idx)].iov_len
#define	uio_iovbase(uio, idx)		(uio)->uio_iov[(idx)].iov_base

static inline void
uio_iov_at_index(uio_t *uio, uint_t idx, void **base, uint64_t *len)
{
	*base = uio_iovbase(uio, idx);
	*len = uio_iovlen(uio, idx);
}

static inline void
uio_advance(uio_t *uio, size_t size)
{
	uio->uio_resid -= size;
	uio->uio_loffset += size;
}

static inline offset_t
uio_index_at_offset(uio_t *uio, offset_t off, uint_t *vec_idx)
{
	*vec_idx = 0;
	while (*vec_idx < uio_iovcnt(uio) && off >= uio_iovlen(uio, *vec_idx)) {
		off -= uio_iovlen(uio, *vec_idx);
		(*vec_idx)++;
	}

	return (off);
}

#endif /* !_STANDALONE */

#endif	/* !_OPENSOLARIS_SYS_UIO_H_ */
