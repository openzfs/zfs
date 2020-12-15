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

#ifndef	_LIBSPL_WINDOWS_SYS_UIO_H
#define	_LIBSPL_WINDOWS_SYS_UIO_H

#pragma message "global uio.h included"

struct iovec {
	void    *iov_base;      /* Base address. */
	uint32_t   iov_len;       /* Length. */
};

typedef struct iovec iovec_t;

typedef enum uio_rw {
	UIO_READ =	0,
	UIO_WRITE =	1,
} uio_rw_t;

typedef enum uio_seg {
	UIO_USERSPACE =	0,
	UIO_SYSSPACE =	1,
	UIO_USERISPACE = 2,
} uio_seg_t;

#include_next <sys/uio.h>

#endif	/* _WINDOWS_SYS_UIO_H */
