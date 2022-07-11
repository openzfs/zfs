/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
