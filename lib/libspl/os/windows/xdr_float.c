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
 * Copyright 2011 Jason King.  All rights reserved
 */

/*
 * Generic XDR routines impelmentation.
 *
 * These are the "floating point" xdr routines used to (de)serialize
 * most common data items.  See xdr.h for more info on the interface to
 * xdr.
 */

//#include "mt.h"
#include <sys/types.h>
#include <stdio.h>
#include <sys/debug.h>
//#include <values.h>
#include <rpc/types.h>
#include <rpc/xdr.h>
#include <sys/byteorder.h>

// incompatible types - from 'XDR *' to 'XDR *'
#pragma warning (disable: 4133)

#define _IEEE_754
#ifdef _IEEE_754

/*
 * The OTW format is IEEE 754 with big endian ordering.
 */
bool_t
xdr_float(XDR *xdrs, float *fp)
{
	switch (xdrs->x_op) {

	case XDR_ENCODE:
		return (XDR_PUTINT32(xdrs, (int *)fp));

	case XDR_DECODE:
		return (XDR_GETINT32(xdrs, (int *)fp));

	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
}

bool_t
xdr_double(XDR *xdrs, double *dp)
{
	int64_t *i64p = (int64_t *)dp;
	int64_t val;
	bool_t ret;

	switch (xdrs->x_op) {

	case XDR_ENCODE:
		val = BE_64(*i64p);
		return (XDR_PUTBYTES(xdrs, (char *)&val, sizeof (val)));

	case XDR_DECODE:
		ret = XDR_GETBYTES(xdrs, (char *)dp, sizeof (double));
		if (ret)
			*i64p = BE_64(*i64p);
		return (ret);

	case XDR_FREE:
		return (TRUE);
	}

	return (FALSE);
}

/* ARGSUSED */
bool_t
xdr_quadruple(XDR *xdrs, long double *fp)
{
/*
 * The Sparc uses IEEE FP encoding, so just do a byte copy
 */

#if !defined(sparc)
	return (FALSE);
#else
	switch (xdrs->x_op) {
	case XDR_ENCODE:
		return (XDR_PUTBYTES(xdrs, (char *)fp, sizeof (long double)));
	case XDR_DECODE:
		return (XDR_GETBYTES(xdrs, (char *)fp, sizeof (long double)));
	case XDR_FREE:
		return (TRUE);
	}
	return (FALSE);
#endif
}

#else

#warn No platform specific implementation defined for floats

bool_t
xdr_float(XDR *xdrs, float *fp)
{
	/*
	 * Every machine can do this, its just not very efficient.
	 * In addtion, some rounding errors may occur do to the
	 * calculations involved.
	 */
	float f;
	int neg = 0;
	int exp = 0;
	int32_t val;

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		f = *fp;
		if (f == 0) {
			val = 0;
			return (XDR_PUTINT32(xdrs, &val));
		}
		if (f < 0) {
			f = 0 - f;
			neg = 1;
		}
		while (f < 1) {
			f = f * 2;
			--exp;
		}
		while (f >= 2) {
			f = f/2;
			++exp;
		}
		if ((exp > 128) || (exp < -127)) {
			/* over or under flowing ieee exponent */
			return (FALSE);
		}
		val = neg;
		val = val << 8;		/* for the exponent */
		val += 127 + exp;	/* 127 is the bias */
		val = val << 23;	/* for the mantissa */
		val += (int32_t)((f - 1) * 8388608);	/* 2 ^ 23 */
		return (XDR_PUTINT32(xdrs, &val));

	case XDR_DECODE:
		/*
		 * It assumes that the decoding machine's float can represent
		 * any value in the range of
		 *	ieee largest  float  = (2 ^ 128)  * 0x1.fffff
		 *	to
		 *	ieee smallest float  = (2 ^ -127) * 0x1.00000
		 * In addtion, some rounding errors may occur do to the
		 * calculations involved.
		 */

		if (!XDR_GETINT32(xdrs, (int32_t *)&val))
			return (FALSE);
		neg = val & 0x80000000;
		exp = (val & 0x7f800000) >> 23;
		exp -= 127;		/* subtract exponent base */
		f = (val & 0x007fffff) * 0.00000011920928955078125;
		/* 2 ^ -23 */
		f++;

		while (exp != 0) {
			if (exp < 0) {
				f = f/2.0;
				++exp;
			} else {
				f = f * 2.0;
				--exp;
			}
		}

		if (neg)
			f = 0 - f;

		*fp = f;
		return (TRUE);

	case XDR_FREE:
		return (TRUE);
	}

	return (FALSE);
}

bool_t
xdr_double(XDR *xdrs, double *dp)
{
	/*
	 * Every machine can do this, its just not very efficient.
	 * In addtion, some rounding errors may occur do to the
	 * calculations involved.
	 */

	int *lp;
	double d;
	int neg = 0;
	int exp = 0;
	int32_t val[2];

	switch (xdrs->x_op) {
	case XDR_ENCODE:
		d = *dp;
		if (d == 0) {
			val[0] = 0;
			val[1] = 0;
			lp = val;
			return (XDR_PUTINT32(xdrs, lp++) &&
			    XDR_PUTINT32(xdrs, lp));
		}
		if (d < 0) {
			d = 0 - d;
			neg = 1;
		}
		while (d < 1) {
			d = d * 2;
			--exp;
		}
		while (d >= 2) {
			d = d/2;
			++exp;
		}
		if ((exp > 1024) || (exp < -1023)) {
			/* over or under flowing ieee exponent */
			return (FALSE);
		}
		val[0] = (neg << 11);	/* for the exponent */
		val[0] += 1023 + exp;	/* 1023 is the bias */
		val[0] = val[0] << 20;	/* for the mantissa */
		val[0] += (int32_t)((d - 1) * 1048576);	/* 2 ^ 20 */
		val[1] += (uint32_t)((((d - 1) * 1048576) - val[0]) *
		    4294967296); /* 2 ^ 32 */
		lp = val;

		return (XDR_PUTINT32(xdrs, lp++) && XDR_PUTINT32(xdrs, lp));

	case XDR_DECODE:
		/*
		 * It assumes that the decoding machine's
		 * double can represent any value in the range of
		 *	ieee largest  double  = (2 ^ 1024)  * 0x1.fffffffffffff
		 *	to
		 *	ieee smallest double  = (2 ^ -1023) * 0x1.0000000000000
		 * In addtion, some rounding errors may occur do to the
		 * calculations involved.
		 */

		lp = val;
		if (!XDR_GETINT32(xdrs, lp++) || !XDR_GETINT32(xdrs, lp))
			return (FALSE);
		neg = val[0] & 0x80000000;
		exp = (val[0] & 0x7ff00000) >> 20;
		exp -= 1023;		/* subtract exponent base */
		d = (val[0] & 0x000fffff) * 0.00000095367431640625;
		/* 2 ^ -20 */
		d += (val[1] * 0.0000000000000002220446049250313);
		/* 2 ^ -52 */
		d++;
		while (exp != 0) {
			if (exp < 0) {
				d = d/2.0;
				++exp;
			} else {
				d = d * 2.0;
				--exp;
			}
		}
		if (neg)
			d = 0 - d;

		*dp = d;
		return (TRUE);

	case XDR_FREE:
		return (TRUE);
	}

	return (FALSE);
}

bool_t
xdr_quadruple(XDR *xdrs, long double *fp)
{
	return (FALSE);
}

#endif /* _IEEE_754 */
