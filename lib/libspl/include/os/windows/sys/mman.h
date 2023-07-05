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
