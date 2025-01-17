/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 * Copyright (c) 1989, 2011, Oracle and/or its affiliates. All rights reserved.
 */
/* Copyright (c) 1983, 1984, 1985, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */
/*
 * Portions of this source code were derived from Berkeley
 * 4.3 BSD under license from the Regents of the University of
 * California.
 */

/*
 * xdr.h, External Data Representation Serialization Routines.
 *
 */

#ifndef _SPL_RPC_XDR_H
#define	_SPL_RPC_XDR_H


#include <sys/types.h>
#include <rpc/types.h>

/*
 * XDR enums and types.
 */
enum xdr_op {
	XDR_ENCODE,
	XDR_DECODE
};

struct xdr_ops;

typedef struct {
	const struct xdr_ops *x_ops;	/* Also used to let caller know if */
				/* xdrmem_create() succeeds (sigh..) */
	caddr_t		x_addr;	/* Current buffer addr */
	caddr_t		x_addr_end; /* End of the buffer */
	enum xdr_op	x_op;	/* Stream direction */
} XDR;

#ifdef _KERNEL
typedef bool_t (*xdrproc_t)(XDR *xdrs, void *ptr);
#else
typedef bool_t (*xdrproc_t)(XDR *xdrs, void *ptr, unsigned int u);
#endif

struct xdr_ops {
	bool_t (*xdr_control)(XDR *, int, void *);

	bool_t (*xdr_char)(XDR *, char *);
	bool_t (*xdr_u_short)(XDR *, unsigned short *);
	bool_t (*xdr_u_int)(XDR *, unsigned *);
	bool_t (*xdr_u_longlong_t)(XDR *, u_longlong_t *);

	bool_t (*xdr_opaque)(XDR *, caddr_t, const uint_t);
	bool_t (*xdr_string)(XDR *, char **, const uint_t);
	bool_t (*xdr_array)(XDR *, caddr_t *, uint_t *, const uint_t,
	    const uint_t, const xdrproc_t);
};

/*
 * XDR control operator.
 */
#define	XDR_GET_BYTES_AVAIL 1

struct xdr_bytesrec {
	bool_t xc_is_last_record;
	size_t xc_num_avail;
};

/*
 * XDR functions.
 */
void xdrmem_create(XDR *xdrs, const caddr_t addr, const uint_t size,
    const enum xdr_op op);
#define	xdr_destroy(xdrs) ((void) 0)

#define	xdr_control(xdrs, req, info) \
	(xdrs)->x_ops->xdr_control((xdrs), (req), (info))

/*
 * For precaution, the following are defined as static inlines instead of macros
 * to get some amount of type safety.
 *
 * Also, macros wouldn't work in the case where typecasting is done, because it
 * must be possible to reference the functions' addresses by these names.
 */
static inline bool_t
xdr_char(XDR *xdrs, char *cp)
{
	return (xdrs->x_ops->xdr_char(xdrs, cp));
}

static inline bool_t
xdr_u_short(XDR *xdrs, unsigned short *usp)
{
	return (xdrs->x_ops->xdr_u_short(xdrs, usp));
}

static inline bool_t
xdr_short(XDR *xdrs, short *sp)
{
	return (xdrs->x_ops->xdr_u_short(xdrs, (unsigned short *) sp));
}

static inline bool_t
xdr_u_int(XDR *xdrs, unsigned *up)
{
	return (xdrs->x_ops->xdr_u_int(xdrs, up));
}

static inline bool_t
xdr_int(XDR *xdrs, int *ip)
{
	return (xdrs->x_ops->xdr_u_int(xdrs, (unsigned *)ip));
}

static inline bool_t
xdr_u_longlong_t(XDR *xdrs, u_longlong_t *ullp)
{
	return (xdrs->x_ops->xdr_u_longlong_t(xdrs, ullp));
}

static inline bool_t
xdr_longlong_t(XDR *xdrs, longlong_t *llp)
{
	return (xdrs->x_ops->xdr_u_longlong_t(xdrs, (u_longlong_t *)llp));
}

/*
 * Fixed-length opaque data.
 */
static inline bool_t
xdr_opaque(XDR *xdrs, caddr_t cp, const uint_t cnt)
{
	return (xdrs->x_ops->xdr_opaque(xdrs, cp, cnt));
}

/*
 * Variable-length string.
 * The *sp buffer must have (maxsize + 1) bytes.
 */
static inline bool_t
xdr_string(XDR *xdrs, char **sp, const uint_t maxsize)
{
	return (xdrs->x_ops->xdr_string(xdrs, sp, maxsize));
}

/*
 * Variable-length arrays.
 */
static inline bool_t xdr_array(XDR *xdrs, caddr_t *arrp, uint_t *sizep,
    const uint_t maxsize, const uint_t elsize, const xdrproc_t elproc)
{
	return (xdrs->x_ops->xdr_array(xdrs, arrp, sizep, maxsize, elsize,
	    elproc));
}

#endif /* SPL_RPC_XDR_H */
