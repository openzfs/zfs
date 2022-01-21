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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */


/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/


#ifndef _SYS_VTOC_H
#define	_SYS_VTOC_H

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Partition identification tags
 */
#define	V_UNASSIGNED	0x00		/* unassigned partition */
#define	V_USR		0x04		/* Usr filesystem */
#define	V_RESERVED	0x0b		/* SMI reserved data */

/*
 * error codes for reading & writing vtoc
 */
#define	VT_ERROR	(-2)		/* errno supplies specific error */
#define	VT_EIO		(-3)		/* I/O error accessing vtoc */
#define	VT_EINVAL	(-4)		/* illegal value in vtoc or request */
#define	VT_ENOSPC	(-6)		/* requested space not found */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_VTOC_H */
