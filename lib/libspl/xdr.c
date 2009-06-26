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

#include <rpc/xdr.h>

/*
 * As of glibc-2.5-25 there is not support for xdr_control().  The
 * xdrmem implementation from OpenSolaris is used here.
 *
 * FIXME: Not well tested it may not work as expected.
 */
bool_t
xdr_control(XDR *xdrs, int request, void *info)
{
	xdr_bytesrec_t *xptr;
	int32_t *int32p;
	int len;

	switch (request) {
	case XDR_GET_BYTES_AVAIL:
		xptr = (xdr_bytesrec_t *)info;
		xptr->xc_is_last_record = TRUE;
		xptr->xc_num_avail = xdrs->x_handy;
		return (TRUE);

	case XDR_PEEK:
		/*
		 * Return the next 4 byte unit in the XDR stream.
		 */
		if (xdrs->x_handy < sizeof (int32_t))
			return (FALSE);
		int32p = (int32_t *)info;
		*int32p = (int32_t)ntohl((uint32_t)
		    (*((int32_t *)(xdrs->x_private))));
		return (TRUE);

	case XDR_SKIPBYTES:
		/*
		 * Skip the next N bytes in the XDR stream.
		 */
		int32p = (int32_t *)info;
		len = RNDUP((int)(*int32p));
		if ((xdrs->x_handy -= len) < 0)
			return (FALSE);
		xdrs->x_private += len;
		return (TRUE);

	}
	return (FALSE);
}
