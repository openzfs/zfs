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

//#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * Generic XDR routines implementation.
 *
 * These are the "generic" xdr routines used to serialize and de-serialize
 * most common data items.  See xdr.h for more info on the interface to
 * xdr.
 */
//#include "mt.h"
#include <stdlib.h>
#include <sys/types.h>
#include <sys/isa_defs.h>
//#include <syslog.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/debug.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <inttypes.h>
#include <sys/sysmacros.h>
#include <sys/types.h>

// warning C4133 : 'function' : incompatible types - from 'XDR *' to 'XDR *'
#pragma warning( disable: 4133 )

#ifndef _WIN32
#pragma weak xdr_int64_t = xdr_hyper
#pragma weak xdr_uint64_t = xdr_u_hyper
#pragma weak xdr_int32_t = xdr_int
#pragma weak xdr_uint32_t = xdr_u_int
#pragma weak xdr_int16_t = xdr_short
#pragma weak xdr_uint16_t = xdr_u_short
#pragma weak xdr_int8_t = xdr_char
#pragma weak xdr_uint8_t = xdr_u_char

/*
 * The following routine was part of a workaround for an rpcgen
 * that was fixed, this routine should be removed sometime.
 */
#pragma weak xdr_ulonglong_t = xdr_u_longlong_t
#endif

/*
 * constants specific to the xdr "protocol"
 */
#define	XDR_FALSE	((uint_t)0)
#define	XDR_TRUE	((uint_t)1)
#define	LASTUNSIGNED	((uint_t)0-1)

/* fragment size to use when doing an xdr_string() */
#define	FRAGMENT	65536

/*
 * for unit alignment
 */
#define	BYTES_PER_XDR_UNIT	(4)

static const char xdr_zero[BYTES_PER_XDR_UNIT]	= { 0 };

/*
 * Free a data structure using XDR
 * Not a filter, but a convenient utility nonetheless
 */
void
xdr_free(xdrproc_t proc, char *objp)
{
	XDR x;

	x.x_op = XDR_FREE;
	(*proc)(&x, objp);
}

/*
 * XDR nothing
 */
bool_t
xdr_void(void)
{
	return (B_TRUE);
}

/*
 * xdr_time_t  sends time_t value over the wire.
 * Due to RPC Protocol limitation, it can only send
 * up to 32-bit integer quantity over the wire.
 *
 */
bool_t
xdr_time_t(XDR *xdrs, time_t *tp)
{
	int32_t i;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
	/*
	 * Check for the time overflow, when encoding it.
	 * Don't want to send OTW the time value too large to
	 * handle by the protocol.
	 */
#if defined(_LP64)
	if (*tp > INT32_MAX)
		*tp = INT32_MAX;
	else if (*tp < INT32_MIN)
		*tp = INT32_MIN;
#endif
		i =  (int32_t)*tp;
		return (XDR_PUTINT32(xdrs, &i));

	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, &i))
			return (FALSE);
		*tp = (time_t)i;
		return (TRUE);

	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR integers
 */
bool_t
xdr_int(XDR *xdrs, int *ip)
{
	switch (xdrs->x_op) {
	case XDR_ENCODE:
		return (XDR_PUTINT32(xdrs, ip));
	case XDR_DECODE:
		return (XDR_GETINT32(xdrs, ip));
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR unsigned integers
 */
bool_t
xdr_u_int(XDR *xdrs, uint_t *up)
{
	switch (xdrs->x_op) {
	case XDR_ENCODE:
		return (XDR_PUTINT32(xdrs, (int *)up));
	case XDR_DECODE:
		return (XDR_GETINT32(xdrs, (int *)up));
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * The definition of xdr_long()/xdr_u_long() is kept for backward
 * compatibitlity.
 * XDR long integers, same as xdr_u_long
 */
bool_t
xdr_long(XDR *xdrs, long *lp)
{
	int32_t i;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
#if defined(_LP64)
		if ((*lp > INT32_MAX) || (*lp < INT32_MIN))
			return (FALSE);
#endif
		i = (int32_t)*lp;
		return (XDR_PUTINT32(xdrs, &i));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, &i))
			return (FALSE);
		*lp = (long)i;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR unsigned long integers
 * same as xdr_long
 */
bool_t
xdr_u_long(XDR *xdrs, ulong_t *ulp)
{
	uint32_t ui;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
#if defined(_LP64)
		if (*ulp > UINT32_MAX)
			return (FALSE);
#endif
		ui = (uint32_t)*ulp;
		return (XDR_PUTINT32(xdrs, (int32_t *)&ui));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, (int32_t *)&ui))
			return (FALSE);
		*ulp = (ulong_t)ui;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR short integers
 */
bool_t
xdr_short(XDR *xdrs, short *sp)
{
	int32_t l;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		l = (int32_t)*sp;
		return (XDR_PUTINT32(xdrs, &l));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, &l))
			return (FALSE);
		*sp = (short)l;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR unsigned short integers
 */
bool_t
xdr_u_short(XDR *xdrs, ushort_t *usp)
{
	uint_t i;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		i = (uint_t)*usp;
		return (XDR_PUTINT32(xdrs, (int *)&i));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, (int *)&i))
			return (FALSE);
		*usp = (ushort_t)i;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}


/*
 * XDR a char
 */
bool_t
xdr_char(XDR *xdrs, char *cp)
{
	int i;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		i = (*cp);
		return (XDR_PUTINT32(xdrs, &i));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, &i))
			return (FALSE);
		*cp = (char)i;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR an unsigned char
 */
bool_t
xdr_u_char(XDR *xdrs, uchar_t *cp)
{
	int i;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		i = (*cp);
		return (XDR_PUTINT32(xdrs, &i));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, &i))
			return (FALSE);
		*cp = (uchar_t)i;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR booleans
 */
bool_t
xdr_bool(XDR *xdrs, bool_t *bp)
{
	int i;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		i = *bp ? XDR_TRUE : XDR_FALSE;
		return (XDR_PUTINT32(xdrs, &i));
	case XDR_DECODE:
		if (!XDR_GETINT32(xdrs, &i))
			return (FALSE);
		*bp = (i == XDR_FALSE) ? FALSE : TRUE;
		return (TRUE);
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR enumerations
 */
bool_t
xdr_enum(XDR *xdrs, int *ep)
{
	enum sizecheck { SIZEVAL };	/* used to find the size of an enum */

	/*
	 * enums are treated as ints
	 */
	/* CONSTCOND */
	assert(sizeof (enum sizecheck) == sizeof (int32_t));
	return (xdr_int(xdrs, (int *)ep));
}

/*
 * XDR opaque data
 * Allows the specification of a fixed size sequence of opaque bytes.
 * cp points to the opaque object and cnt gives the byte length.
 */
bool_t
xdr_opaque(XDR *xdrs, caddr_t cp, const uint_t cnt)
{
	uint_t rndup;
	char crud[BYTES_PER_XDR_UNIT];

	/*
	 * if no data we are done
	 */
	if (cnt == 0)
		return (TRUE);

	/*
	 * round byte count to full xdr units
	 */
	rndup = cnt % BYTES_PER_XDR_UNIT;
	if ((int)rndup > 0)
		rndup = BYTES_PER_XDR_UNIT - rndup;

	switch (xdrs->x_op) {
	case XDR_DECODE:
		if (!XDR_GETBYTES(xdrs, cp, cnt))
			return (FALSE);
		if (rndup == 0)
			return (TRUE);
		return (XDR_GETBYTES(xdrs, crud, rndup));
	case XDR_ENCODE:
		if (!XDR_PUTBYTES(xdrs, cp, cnt))
			return (FALSE);
		if (rndup == 0)
			return (TRUE);
		return (XDR_PUTBYTES(xdrs, (caddr_t)&xdr_zero[0], rndup));
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

/*
 * XDR counted bytes
 * *cpp is a pointer to the bytes, *sizep is the count.
 * If *cpp is NULL maxsize bytes are allocated
 */

static const char xdr_err[] = "xdr_%s: out of memory";

bool_t
xdr_bytes(XDR *xdrs, char **cpp, uint_t *sizep, const uint_t maxsize)
{
	char *sp = *cpp;  /* sp is the actual string pointer */
	uint_t nodesize;

	/*
	 * first deal with the length since xdr bytes are counted
	 * We decided not to use MACRO XDR_U_INT here, because the
	 * advantages here will be miniscule compared to xdr_bytes.
	 * This saved us 100 bytes in the library size.
	 */
	if (!xdr_u_int(xdrs, sizep))
		return (FALSE);
	nodesize = *sizep;
	if ((nodesize > maxsize) && (xdrs->x_op != XDR_FREE))
		return (FALSE);

	/*
	 * now deal with the actual bytes
	 */
	switch (xdrs->x_op) {
	case XDR_DECODE:
		if (nodesize == 0)
			return (TRUE);
		if (sp == NULL)
			*cpp = sp = malloc(nodesize);
		if (sp == NULL) {
//			(void) syslog(LOG_ERR, xdr_err, (const char *)"bytes");
			return (FALSE);
		}
		/*FALLTHROUGH*/
	case XDR_ENCODE:
		return (xdr_opaque(xdrs, sp, nodesize));
	case XDR_FREE:
		if (sp != NULL) {
			free(sp);
			*cpp = NULL;
		}
		return (TRUE);
	}
	return (FALSE);
}

/*
 * Implemented here due to commonality of the object.
 */
bool_t
xdr_netobj(XDR *xdrs, struct netobj *np)
{
	return (xdr_bytes(xdrs, &np->n_bytes, &np->n_len, MAX_NETOBJ_SZ));
}

/*
 * XDR a descriminated union
 * Support routine for discriminated unions.
 * You create an array of xdrdiscrim structures, terminated with
 * an entry with a null procedure pointer.  The routine gets
 * the discriminant value and then searches the array of xdrdiscrims
 * looking for that value.  It calls the procedure given in the xdrdiscrim
 * to handle the discriminant.  If there is no specific routine a default
 * routine may be called.
 * If there is no specific or default routine an error is returned.
 */
bool_t
xdr_union(XDR *xdrs, int *dscmp, char *unp,
		const struct xdr_discrim *choices, const xdrproc_t dfault)
{
	int dscm;

	/*
	 * we deal with the discriminator;  it's an enum
	 */
	if (!xdr_enum(xdrs, dscmp))
		return (FALSE);
	dscm = *dscmp;

	/*
	 * search choices for a value that matches the discriminator.
	 * if we find one, execute the xdr routine for that value.
	 */
	for (; choices->proc != NULL_xdrproc_t; choices++) {
		if (choices->value == dscm)
			return ((*(choices->proc))(xdrs, unp, LASTUNSIGNED));
	}

	/*
	 * no match - execute the default xdr routine if there is one
	 */
	return ((dfault == NULL_xdrproc_t) ? FALSE :
	    (*dfault)(xdrs, unp, LASTUNSIGNED));
}


/*
 * Non-portable xdr primitives.
 * Care should be taken when moving these routines to new architectures.
 */


/*
 * XDR null terminated ASCII strings
 * xdr_string deals with "C strings" - arrays of bytes that are
 * terminated by a NULL character.  The parameter cpp references a
 * pointer to storage; If the pointer is null, then the necessary
 * storage is allocated.  The last parameter is the max allowed length
 * of the string as specified by a protocol.
 */
bool_t
xdr_string(XDR *xdrs, char **cpp, const uint_t maxsize)
{
	char *newsp, *sp = *cpp;  /* sp is the actual string pointer */
	uint_t size, block;
	uint64_t bytesread;

	/*
	 * first deal with the length since xdr strings are counted-strings
	 */
	switch (xdrs->x_op) {
	case XDR_FREE:
		if (sp == NULL)
			return (TRUE);	/* already free */
		/*FALLTHROUGH*/
	case XDR_ENCODE:
		size = (sp != NULL) ? (uint_t)strlen(sp) : 0;
		break;
	}
	/*
	 * We decided not to use MACRO XDR_U_INT here, because the
	 * advantages here will be miniscule compared to xdr_string.
	 * This saved us 100 bytes in the library size.
	 */
	if (!xdr_u_int(xdrs, &size))
		return (FALSE);
	if (size > maxsize)
		return (FALSE);

	/*
	 * now deal with the actual bytes
	 */
	switch (xdrs->x_op) {
	case XDR_DECODE:
		/* if buffer is already given, call xdr_opaque() directly */
		if (sp != NULL) {
			if (!xdr_opaque(xdrs, sp, size))
				return (FALSE);
			sp[size] = 0;
			return (TRUE);
		}

		/*
		 * We have to allocate a buffer of size 'size'. To avoid
		 * malloc()ing one huge chunk, we'll read the bytes in max
		 * FRAGMENT size blocks and keep realloc()ing. 'block' is
		 * the number of bytes to read in each xdr_opaque() and
		 * 'bytesread' is what we have already read. sp is NULL
		 * when we are in the loop for the first time.
		 */
		bytesread = 0;
		do {
			block = MIN(size - bytesread, FRAGMENT);
			/*
			 * allocate enough for 'bytesread + block' bytes and
			 * one extra for the terminating NULL.
			 */
			newsp = realloc(sp, bytesread + block + 1);
			if (newsp == NULL) {
				if (sp != NULL)
					free(sp);
				return (FALSE);
			}
			sp = newsp;
			if (!xdr_opaque(xdrs, &sp[bytesread], block)) {
				free(sp);
				return (FALSE);
			}
			bytesread += block;
		} while (bytesread < size);

		sp[bytesread] = 0; /* terminate the string with a NULL */
		*cpp = sp;
		return (TRUE);
	case XDR_ENCODE:
		return (xdr_opaque(xdrs, sp, size));
	case XDR_FREE:
		free(sp);
		*cpp = NULL;
		return (TRUE);
	}
	return (FALSE);
}

bool_t
xdr_hyper(XDR *xdrs, longlong_t *hp)
{
	if (xdrs->x_op == XDR_ENCODE) {
#if defined(_LONG_LONG_HTOL)
		if (XDR_PUTINT32(xdrs, (int *)hp) == TRUE)
			/* LINTED pointer cast */
			return (XDR_PUTINT32(xdrs, (int *)((char *)hp +
				BYTES_PER_XDR_UNIT)));
#else
		/* LINTED pointer cast */
		if (XDR_PUTINT32(xdrs, (int *)((char *)hp +
				BYTES_PER_XDR_UNIT)) == TRUE)
			return (XDR_PUTINT32(xdrs, (int32_t *)hp));
#endif
		return (FALSE);
	}

	if (xdrs->x_op == XDR_DECODE) {
#if defined(_LONG_LONG_HTOL)
		if (XDR_GETINT32(xdrs, (int *)hp) == FALSE ||
		    /* LINTED pointer cast */
		    (XDR_GETINT32(xdrs, (int *)((char *)hp +
				BYTES_PER_XDR_UNIT)) == FALSE))
			return (FALSE);
#else
		/* LINTED pointer cast */
		if ((XDR_GETINT32(xdrs, (int *)((char *)hp +
				BYTES_PER_XDR_UNIT)) == FALSE) ||
				(XDR_GETINT32(xdrs, (int *)hp) == FALSE))
			return (FALSE);
#endif
		return (TRUE);
	}
	return (TRUE);
}

bool_t
xdr_u_hyper(XDR *xdrs, u_longlong_t *hp)
{
	return (xdr_hyper(xdrs, (longlong_t *)hp));
}

bool_t
xdr_longlong_t(XDR *xdrs, longlong_t *hp)
{
	return (xdr_hyper(xdrs, hp));
}

bool_t
xdr_u_longlong_t(XDR *xdrs, u_longlong_t *hp)
{
	return (xdr_hyper(xdrs, (longlong_t *)hp));
}

/*
 * Wrapper for xdr_string that can be called directly from
 * routines like clnt_call
 */
bool_t
xdr_wrapstring(XDR *xdrs, char **cpp)
{
	return (xdr_string(xdrs, cpp, LASTUNSIGNED));
}

#if 0
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
		if (xdrs->x_handy < sizeof(int32_t))
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
		if (xdrs->x_handy < len)
			return (FALSE);
		xdrs->x_handy -= len;
		xdrs->x_private += len;
		return (TRUE);

	}
	return (FALSE);
}
#endif
