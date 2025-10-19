// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
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
 */
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_TRACE_H
#define	_SYS_TRACE_H

/*
 * DTrace SDT probes have different signatures in userland than they do in
 * the kernel.  If they're being used in kernel code, re-define them out of
 * existence for their counterparts in libzpool.
 *
 * Here's an example of how to use the set-error probes in userland:
 * zfs$target:::set-error /arg0 == EBUSY/ {stack();}
 *
 * Here's an example of how to use DTRACE_PROBE probes in userland:
 * If there is a probe declared as follows:
 * DTRACE_PROBE2(zfs__probe_name, uint64_t, blkid, dnode_t *, dn);
 * Then you can use it as follows:
 * zfs$target:::probe2 /copyinstr(arg0) == "zfs__probe_name"/
 *     {printf("%u %p\n", arg1, arg2);}
 */

#ifdef DTRACE_PROBE
#undef	DTRACE_PROBE
#endif	/* DTRACE_PROBE */
#define	DTRACE_PROBE(a)

#ifdef DTRACE_PROBE1
#undef	DTRACE_PROBE1
#endif	/* DTRACE_PROBE1 */
#define	DTRACE_PROBE1(a, b, c)

#ifdef DTRACE_PROBE2
#undef	DTRACE_PROBE2
#endif	/* DTRACE_PROBE2 */
#define	DTRACE_PROBE2(a, b, c, d, e)

#ifdef DTRACE_PROBE3
#undef	DTRACE_PROBE3
#endif	/* DTRACE_PROBE3 */
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)

#ifdef DTRACE_PROBE4
#undef	DTRACE_PROBE4
#endif	/* DTRACE_PROBE4 */
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)

#endif /* _SYS_TRACE_H */
