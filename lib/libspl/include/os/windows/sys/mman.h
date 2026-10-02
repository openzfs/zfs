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


#ifndef _LIBSPL_WINDOWS_SYS_MMAN_H
#define	_LIBSPL_WINDOWS_SYS_MMAN_H

#define	PROT_READ	0x1	/* pages can be read */
#define	PROT_WRITE	0x2	/* pages can be written */
#define	PROT_EXEC	0x4	/* pages can be executed */

#define	MAP_SHARED	1	/* share changes */
#define	MAP_PRIVATE	2	/* changes are private */

#define	MAP_FAILED	((void *) -1)


int mprotect(void *addr, size_t len, int prot);

#endif
