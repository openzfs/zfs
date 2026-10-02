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

struct iovec {
	void *iov_base; /* Base address. */
	uint32_t iov_len; /* Length. */
};

typedef struct iovec iovec_t;

#include_next <sys/uio.h>

extern ssize_t readv(int, const struct iovec *, int);
extern ssize_t writev(int fd, struct iovec *iov, unsigned iov_cnt);
extern ssize_t pwritev(int fd, const struct iovec *iov, int iov_cnt,
    off_t offset);

#endif	/* _WINDOWS_SYS_UIO_H */
