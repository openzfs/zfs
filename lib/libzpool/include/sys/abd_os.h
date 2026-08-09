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
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016, 2019 by Delphix. All rights reserved.
 */

#ifndef _ABD_OS_H
#define	_ABD_OS_H

#ifdef __cplusplus
extern "C" {
#endif

struct abd_scatter {
	uint_t		abd_offset;
	uint_t		abd_iovcnt;
	struct iovec	abd_iov[1]; /* actually variable-length */
};

struct abd_linear {
	void		*abd_buf;
};

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
