/*	$NetBSD: xdr_mem.c,v 1.2 2019/06/05 16:25:43 hannken Exp $	*/

/*
 * Copyright (c) 2010, Oracle America, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above
 *       copyright notice, this list of conditions and the following
 *       disclaimer in the documentation and/or other materials
 *       provided with the distribution.
 *     * Neither the name of the "Oracle America, Inc." nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *   FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *   COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 *   INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 *   DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 *   GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *   INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 *   WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *   NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
#if defined(LIBC_SCCS) && !defined(lint)
#if 0
static char *sccsid = "@(#)xdr_mem.c 1.19 87/08/11 Copyr 1984 Sun Micro";
static char *sccsid = "@(#)xdr_mem.c	2.1 88/07/29 4.0 RPCSRC";
#else
__RCSID("$NetBSD: xdr_mem.c,v 1.2 2019/06/05 16:25:43 hannken Exp $");
#endif
#endif

/*
 * xdr_mem.h, XDR implementation using memory buffers.
 *
 * Copyright (C) 1984, Sun Microsystems, Inc.
 *
 * If you have some data to be interpreted as external data representation
 * or to be converted to external data representation in a memory buffer,
 * then this is the package for you.
 *
 */

#if defined(_KERNEL) || defined(_STANDALONE)

#include <lib/libkern/libkern.h>
#include <rpc/types.h>
#include <rpc/xdr.h>

#else /* _KERNEL || _STANDALONE */

#include "namespace.h"

#include <sys/types.h>

#include <netinet/in.h>

#include <string.h>

#include <rpc/types.h>
#include <rpc/xdr.h>

#ifdef __weak_alias
__weak_alias(xdrmem_create, _xdrmem_create)
#endif

#endif /* _KERNEL || _STANDALONE */

static void xdrmem_destroy(XDR *);
static bool_t xdrmem_getlong_aligned(XDR *, long *);
static bool_t xdrmem_putlong_aligned(XDR *, const long *);
static bool_t xdrmem_getlong_unaligned(XDR *, long *);
static bool_t xdrmem_putlong_unaligned(XDR *, const long *);
static bool_t xdrmem_getbytes(XDR *, char *, uint_t);
static bool_t xdrmem_putbytes(XDR *, const char *, uint_t);
/* XXX: w/64-bit pointers, uint_t not enough! */
static uint_t xdrmem_getpos(XDR *);
static bool_t xdrmem_setpos(XDR *, uint_t);
static int32_t *xdrmem_inline_aligned(XDR *, uint_t);
static int32_t *xdrmem_inline_unaligned(XDR *, uint_t);
static bool_t xdrmem_control(XDR *xdrs, int request, void *info);

static const struct	xdr_ops xdrmem_ops_aligned = {
	xdrmem_getlong_aligned,
	xdrmem_putlong_aligned,
	xdrmem_getbytes,
	xdrmem_putbytes,
	xdrmem_getpos,
	xdrmem_setpos,
	xdrmem_inline_aligned,
	xdrmem_destroy,
	xdrmem_control
};

static const struct	xdr_ops xdrmem_ops_unaligned = {
	xdrmem_getlong_unaligned,
	xdrmem_putlong_unaligned,
	xdrmem_getbytes,
	xdrmem_putbytes,
	xdrmem_getpos,
	xdrmem_setpos,
	xdrmem_inline_unaligned,
	xdrmem_destroy,
	xdrmem_control
};

/*
 * The procedure xdrmem_create initializes a stream descriptor for a
 * memory buffer.
 */
void
xdrmem_create(XDR *xdrs, char *addr, uint_t size, enum xdr_op op)
{

	xdrs->x_op = op;
	xdrs->x_ops = ((unsigned long)addr & (sizeof (int32_t) - 1))
	    ? &xdrmem_ops_unaligned : &xdrmem_ops_aligned;
	xdrs->x_private = xdrs->x_base = addr;
	xdrs->x_handy = size;
}

static void
xdrmem_destroy(XDR *xdrs)
{

	(void) xdrs;
}

static bool_t
xdrmem_getlong_aligned(XDR *xdrs, long *lp)
{

	if (xdrs->x_handy < sizeof (int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof (int32_t);
	*lp = ntohl(*(u_int32_t *)xdrs->x_private);
	xdrs->x_private = (char *)xdrs->x_private + sizeof (int32_t);
	return (TRUE);
}

static bool_t
xdrmem_putlong_aligned(XDR *xdrs, const long *lp)
{

	if (xdrs->x_handy < sizeof (int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof (int32_t);
	*(u_int32_t *)xdrs->x_private = htonl((u_int32_t)*lp);
	xdrs->x_private = (char *)xdrs->x_private + sizeof (int32_t);
	return (TRUE);
}

static bool_t
xdrmem_getlong_unaligned(XDR *xdrs, long *lp)
{
	u_int32_t l;

	if (xdrs->x_handy < sizeof (int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof (int32_t);
	memmove(&l, xdrs->x_private, sizeof (int32_t));
	*lp = ntohl(l);
	xdrs->x_private = (char *)xdrs->x_private + sizeof (int32_t);
	return (TRUE);
}

static bool_t
xdrmem_putlong_unaligned(XDR *xdrs, const long *lp)
{
	u_int32_t l;

	if (xdrs->x_handy < sizeof (int32_t))
		return (FALSE);
	xdrs->x_handy -= sizeof (int32_t);
	l = htonl((u_int32_t)*lp);
	memmove(xdrs->x_private, &l, sizeof (int32_t));
	xdrs->x_private = (char *)xdrs->x_private + sizeof (int32_t);
	return (TRUE);
}

static bool_t
xdrmem_getbytes(XDR *xdrs, char *addr, uint_t len)
{

	if (xdrs->x_handy < len)
		return (FALSE);
	xdrs->x_handy -= len;
	memmove(addr, xdrs->x_private, len);
	xdrs->x_private = (char *)xdrs->x_private + len;
	return (TRUE);
}

static bool_t
xdrmem_putbytes(XDR *xdrs, const char *addr, uint_t len)
{

	if (xdrs->x_handy < len)
		return (FALSE);
	xdrs->x_handy -= len;
	memmove(xdrs->x_private, addr, len);
	xdrs->x_private = (char *)xdrs->x_private + len;
	return (TRUE);
}

static uint_t
xdrmem_getpos(XDR *xdrs)
{

	/* XXX w/64-bit pointers, uint_t not enough! */
	return (uint_t)((ulong_t)xdrs->x_private - (ulong_t)xdrs->x_base);
}

static bool_t
xdrmem_setpos(XDR *xdrs, uint_t pos)
{
	char *newaddr = xdrs->x_base + pos;
	char *lastaddr = (char *)xdrs->x_private + xdrs->x_handy;

	if ((long)newaddr > (long)lastaddr)
		return (FALSE);
	xdrs->x_private = newaddr;
	xdrs->x_handy = (int)((long)lastaddr - (long)newaddr);
	return (TRUE);
}

static int32_t *
xdrmem_inline_aligned(XDR *xdrs, uint_t len)
{
	int32_t *buf = 0;

	if (xdrs->x_handy >= len) {
		xdrs->x_handy -= len;
		buf = (int32_t *)xdrs->x_private;
		xdrs->x_private = (char *)xdrs->x_private + len;
	}
	return (buf);
}

static int32_t *
xdrmem_inline_unaligned(XDR *xdrs, uint_t len)
{

	(void) xdrs;
	(void) len;
	return (0);
}

static bool_t
xdrmem_control(XDR *xdrs, int request, void *info)
{
	xdr_bytesrec *xptr;

	switch (request) {

	case XDR_GET_BYTES_AVAIL:
		xptr = (xdr_bytesrec *)info;
		xptr->xc_is_last_record = TRUE;
		xptr->xc_num_avail = xdrs->x_handy;
		return (TRUE);

	}
	return (FALSE);
}
