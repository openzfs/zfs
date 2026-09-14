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
 *
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_SYSTEMINFO_H
#define	_SPL_SYSTEMINFO_H

#define	HW_INVALID_HOSTID	0xFFFFFFFF	/* an invalid hostid */
#define	HW_HOSTID_LEN		11		/* minimum buffer size needed */
						/* to hold a decimal or hex */
						/* hostid string */

const char *spl_panicstr(void);
int spl_system_inshutdown(void);


#endif /* SPL_SYSTEMINFO_H */
