/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#ifndef	_LIBSPL_SYS_UIO_H
#define	_LIBSPL_SYS_UIO_H

#include <sys/types.h>
#include_next <sys/uio.h>

#ifdef __APPLE__
#include <sys/_types/_iovec_t.h>
#endif

#include <stdint.h>
typedef struct iovec iovec_t;

#if defined(__linux__) || defined(__APPLE__)
typedef enum uio_rw {
	UIO_READ =	0,
	UIO_WRITE =	1,
} uio_rw_t;

typedef enum uio_seg {
	UIO_USERSPACE =	0,
	UIO_SYSSPACE =	1,
	UIO_USERISPACE = 2,
} uio_seg_t;

#elif defined(__FreeBSD__)
typedef enum uio_seg  uio_seg_t;
#endif

typedef struct uio {
	struct iovec	*uio_iov;	/* pointer to array of iovecs */
	int		uio_iovcnt;	/* number of iovecs */
	offset_t	uio_loffset;	/* file offset */
	uio_seg_t	uio_segflg;	/* address space (kernel or user) */
	uint16_t	uio_fmode;	/* file mode flags */
	uint16_t	uio_extflg;	/* extended flags */
	offset_t	uio_limit;	/* u-limit (maximum byte offset) */
	ssize_t		uio_resid;	/* residual count */
} uio_t;

typedef enum xuio_type {
	UIOTYPE_ASYNCIO,
	UIOTYPE_ZEROCOPY,
} xuio_type_t;

#define	UIOA_IOV_MAX	16

typedef struct uioa_page_s {		/* locked uio_iov state */
	int	uioa_pfncnt;		/* count of pfn_t(s) in *uioa_ppp */
	void	**uioa_ppp;		/* page_t or pfn_t array */
	caddr_t	uioa_base;		/* address base */
	size_t	uioa_len;		/* span length */
} uioa_page_t;

typedef struct xuio {
	uio_t xu_uio;				/* embedded UIO structure */

	/* Extended uio fields */
	enum xuio_type xu_type;			/* uio type */
	union {
		struct {
			uint32_t xu_a_state;	/* state of async i/o */
			ssize_t xu_a_mbytes;	/* bytes moved */
			uioa_page_t *xu_a_lcur;	/* uioa_locked[] pointer */
			void **xu_a_lppp;	/* lcur->uioa_pppp[] pointer */
			void *xu_a_hwst[4];	/* opaque hardware state */
			uioa_page_t xu_a_locked[UIOA_IOV_MAX];
		} xu_aio;

		struct {
			int xu_zc_rw;		/* read or write buffer */
			void *xu_zc_priv;	/* fs specific */
		} xu_zc;
	} xu_ext;
} xuio_t;

#define	XUIO_XUZC_PRIV(xuio)	xuio->xu_ext.xu_zc.xu_zc_priv
#define	XUIO_XUZC_RW(xuio)	xuio->xu_ext.xu_zc.xu_zc_rw

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
	while (*vec_idx < (uint_t)uio_iovcnt(uio) &&
	    off >= (offset_t)uio_iovlen(uio, *vec_idx)) {
		off -= uio_iovlen(uio, *vec_idx);
		(*vec_idx)++;
	}

	return (off);
}

#endif	/* _SYS_UIO_H */
