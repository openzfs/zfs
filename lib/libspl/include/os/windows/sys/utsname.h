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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_UTSNAME_H
#define	_LIBSPL_UTSNAME_H

#define	_SYS_NMLN 257
struct utsname {
	char    sysname[_SYS_NMLN];
	char    nodename[_SYS_NMLN];
	char    release[_SYS_NMLN];
	char    version[_SYS_NMLN];
	char    machine[_SYS_NMLN];
};

typedef struct utsname utsname_t;

extern int uname(struct utsname *buf);

#endif  /* _LIBSPL_UTSNAME_H */
