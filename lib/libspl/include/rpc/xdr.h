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
 *
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 *	Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T
 *	  All Rights Reserved
 *
 * Portions of this source code were derived from Berkeley 4.3 BSD
 * under license from the Regents of the University of California.
 */

#ifndef LIBSPL_RPC_XDR_H
#define	LIBSPL_RPC_XDR_H

#include_next <rpc/xdr.h>

#ifdef xdr_control /* if e.g. using tirpc */
#undef xdr_control
#endif

#define	XDR_GET_BYTES_AVAIL 1

#ifndef HAVE_XDR_BYTESREC
struct xdr_bytesrec {
	bool_t xc_is_last_record;
	size_t xc_num_avail;
};
#endif
typedef struct xdr_bytesrec  xdr_bytesrec_t;

/*
 * This functionality is not required and is disabled in user space.
 */
static inline bool_t
xdr_control(XDR *xdrs, int request, void *info)
{
	xdr_bytesrec_t *xptr;

	ASSERT3U(request, ==, XDR_GET_BYTES_AVAIL);

	xptr = (xdr_bytesrec_t *)info;
	xptr->xc_is_last_record = TRUE;
	xptr->xc_num_avail = xdrs->x_handy;

	return (TRUE);
}

#endif /* LIBSPL_RPC_XDR_H */
