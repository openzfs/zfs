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

#ifndef	_LIBSPL_OSX_SYS_UIO_H
#define	_LIBSPL_OSX_SYS_UIO_H

#include <sys/kernel_types.h>
#include_next <sys/uio.h>

#ifdef	__cplusplus
extern "C" {
#endif

#include <sys/_types/_iovec_t.h>

#include <sys/types.h>
#include <sys/errno.h>

/*
 * I/O parameter information.  A uio structure describes the I/O which
 * is to be performed by an operation.  Typically the data movement will
 * be performed by a routine such as uiomove(), which updates the uio
 * structure to reflect what was done.
 */

ssize_t readv(int, const struct iovec *, int);
ssize_t writev(int, const struct iovec *, int);

/*
 * I/O direction.
 */

/*
 * Segment flag values.
 */
typedef enum uio_seg { UIO_USERSPACE, UIO_SYSSPACE, UIO_USERISPACE } uio_seg_t;

typedef enum uio_seg  zfs_uio_seg_t;

struct uio {
	struct iovec	*uio_iov;	/* pointer to array of iovecs */
	int		uio_iovcnt;	/* number of iovecs */
	off_t		uio_offset;	/* file offset */
	uio_seg_t	uio_segflg;	/* address space (kernel or user) */
	off_t		uio_limit;	/* u-limit (maximum byte offset) */
	ssize_t		uio_resid;	/* residual count */
	enum uio_rw	uio_rw;
	int		uio_max_iovs; /* max iovecs this uio_t can hold */
	uint32_t	uio_index; /* Current index */
};


uio_t *uio_create(int iovcount, off_t offset, int spacetype, int iodirection);
void uio_free(uio_t *uio);
int uio_addiov(uio_t *uio, user_addr_t baseaddr, user_size_t length);
int uio_isuserspace(uio_t *uio);
int uio_getiov(uio_t *uio, int index, user_addr_t *baseaddr,
    user_size_t *length);
int uio_iovcnt(uio_t *uio);
off_t uio_offset(uio_t *uio);
void uio_update(uio_t *uio, user_size_t count);
uint64_t uio_resid(uio_t *uio);
user_addr_t uio_curriovbase(uio_t *uio);
user_size_t uio_curriovlen(uio_t *uio);
void uio_setoffset(uio_t *uio, off_t a_offset);
uio_t *uio_duplicate(uio_t *uio);
int uio_rw(uio_t *a_uio);
void uio_setrw(uio_t *a_uio, int a_value);

int	uiomove(void *, uint32_t, enum uio_rw, struct uio *);
int	spllib_uiomove(const uint8_t *, uint32_t, struct uio *);
void uioskip(struct uio *, uint32_t);
int	uiodup(struct uio *, struct uio *, struct iovec *, int);

// xuio struct is not used in this platform, but we define it
// to allow compilation and easier patching
typedef enum xuio_type {
	UIOTYPE_ASYNCIO,
	UIOTYPE_ZEROCOPY,
} xuio_type_t;


#define	UIOA_IOV_MAX    16

typedef struct uioa_page_s {
	int uioa_pfncnt;
	void **uioa_ppp;
	caddr_t uioa_base;
	size_t  uioa_len;
} uioa_page_t;

typedef struct xuio {
	uio_t *xu_uio;
	enum xuio_type xu_type;
	union {
		struct {
			uint32_t xu_a_state;
			ssize_t xu_a_mbytes;
			uioa_page_t *xu_a_lcur;
			void **xu_a_lppp;
			void *xu_a_hwst[4];
			uioa_page_t xu_a_locked[UIOA_IOV_MAX];
		} xu_aio;
		struct {
			int xu_zc_rw;
			void *xu_zc_priv;
		} xu_zc;
	} xu_ext;
} xuio_t;

#define	XUIO_XUZC_PRIV(xuio)	xuio->xu_ext.xu_zc.xu_zc_priv
#define	XUIO_XUZC_RW(xuio)		xuio->xu_ext.xu_zc.xu_zc_rw

/*
 * same as uiomove() but doesn't modify uio structure.
 * return in cbytes how many bytes were copied.
 */
static inline int uiocopy(const unsigned char *p, uint32_t n,
    enum uio_rw rw, struct uio *uio, uint64_t *cbytes)
{
	int result;
	struct uio *nuio = uio_duplicate(uio);
	unsigned long long x = uio_resid(uio);
	if (!nuio)
		return (ENOMEM);
	uio_setrw(nuio, rw);
	result = spllib_uiomove(p, n, nuio);
	*cbytes = (x - uio_resid(nuio));
	uio_free(nuio);
	return (result);
}

// Apple's uiomove puts the uio_rw in uio_create
#define	uiomove(A, B, C, D)	spllib_uiomove((A), (B), (D))
#define	uioskip(A, B)	uio_update((A), (B))

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_UIO_H */
