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

#ifndef _LIBSPL_SYS_SYSTEMINFO_H
#define	_LIBSPL_SYS_SYSTEMINFO_H

#define	HOSTID_MASK		0xFFFFFFFF
#define	HW_INVALID_HOSTID	0xFFFFFFFF	/* an invalid hostid */
#define	HW_HOSTID_LEN		11		/* minimum buffer size needed */
						/* to hold a decimal or hex */
						/* hostid string */

unsigned long get_system_hostid(void);

#endif
