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

#ifndef LIBSPL_RPC_XDR_H
#define	LIBSPL_RPC_XDR_H

#include_next <rpc/xdr.h>

/*
 * These are XDR control operators
 */

#define	XDR_GET_BYTES_AVAIL 1

typedef struct xdr_bytesrec {
	bool_t xc_is_last_record;
	size_t xc_num_avail;
} xdr_bytesrec_t;

/*
 * These are the request arguments to XDR_CONTROL.
 *
 * XDR_PEEK - returns the contents of the next XDR unit on the XDR stream.
 * XDR_SKIPBYTES - skips the next N bytes in the XDR stream.
 * XDR_RDMAGET - for xdr implementation over RDMA, gets private flags from
 *		 the XDR stream being moved over RDMA
 * XDR_RDMANOCHUNK - for xdr implementaion over RDMA, sets private flags in
 *                   the XDR stream moving over RDMA.
 */
#define	XDR_PEEK	2
#define	XDR_SKIPBYTES	3
#define	XDR_RDMAGET	4
#define	XDR_RDMASET	5

extern bool_t xdr_control(XDR *xdrs, int request, void *info);

#endif
