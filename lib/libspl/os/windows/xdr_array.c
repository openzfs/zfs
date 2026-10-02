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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * Portions of this source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

/*
 * Generic XDR routines impelmentation.
 *
 * These are the "non-trivial" xdr primitives used to serialize and
 * de-serialize arrays.  See xdr.h for more info on the interface to xdr.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>

#include <sys/debug.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <memory.h>

#define	LASTUNSIGNED	((uint_t)0-1)

char mem_err_msg_arr[] = "xdr_array: out of memory";

/*
 * XDR an array of arbitrary elements
 * *addrp is a pointer to the array, *sizep is the number of elements.
 * If *addrp is NULL (*sizep * elsize) bytes are allocated.
 * elsize is the size (in bytes) of each element, and elproc is the
 * xdr procedure to call to handle each element of the array.
 */
bool_t
xdr_array(XDR *xdrs, caddr_t *addrp, uint_t *sizep, const uint_t maxsize,
    const uint_t elsize, const xdrproc_t elproc)
{
	uint_t i;
	caddr_t target = *addrp;
	uint_t c;  /* the actual element count */
	bool_t stat = TRUE;
	uint_t nodesize;

	/* like strings, arrays are really counted arrays */
	if (!xdr_u_int(xdrs, sizep))
		return (FALSE);
	c = *sizep;
	if ((c > maxsize || LASTUNSIGNED / elsize < c) &&
	    xdrs->x_op != XDR_FREE)
		return (FALSE);
	nodesize = c * elsize;

	/*
	 * if we are deserializing, we may need to allocate an array.
	 * We also save time by checking for a null array if we are freeing.
	 */
	if (target == NULL)
		switch (xdrs->x_op) {
		case XDR_DECODE:
			if (c == 0)
				return (TRUE);
			*addrp = target = malloc(nodesize);
			if (target == NULL) {
				// (void) syslog(LOG_ERR, mem_err_msg_arr);
				return (FALSE);
			}
			(void) memset(target, 0, nodesize);
			break;

		case XDR_FREE:
			return (TRUE);
	}

	/*
	 * now we xdr each element of array
	 */
	for (i = 0; (i < c) && stat; i++) {
		stat = (*elproc)(xdrs, target);
		target += elsize;
	}

	/*
	 * the array may need freeing
	 */
	if (xdrs->x_op == XDR_FREE) {
		free(*addrp);
		*addrp = NULL;
	}
	return (stat);
}

/*
 * xdr_vector():
 *
 * XDR a fixed length array. Unlike variable-length arrays,
 * the storage of fixed length arrays is static and unfreeable.
 * > basep: base of the array
 * > size: size of the array
 * > elemsize: size of each element
 * > xdr_elem: routine to XDR each element
 */
bool_t
xdr_vector(XDR *xdrs, char *basep, const uint_t nelem,
    const uint_t elemsize, const xdrproc_t xdr_elem)
{
	uint_t i;
	char *elptr;

	elptr = basep;
	for (i = 0; i < nelem; i++) {
		if (!(*xdr_elem)(xdrs, elptr, LASTUNSIGNED))
			return (FALSE);
		elptr += elemsize;
	}
	return (TRUE);
}
