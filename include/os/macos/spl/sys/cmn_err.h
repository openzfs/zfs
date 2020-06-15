/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License"). You may not use this file except in compliance
 * with the License.
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
/* Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T */
/* All Rights Reserved */


/*
 * Copyright 2004 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright 2012 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef _SPL_CMN_ERR_H
#define	_SPL_CMN_ERR_H

#include <stdarg.h>
#include <sys/varargs.h>

#define	CE_CONT		0 /* continuation	*/
#define	CE_NOTE		1 /* notice		*/
#define	CE_WARN		2 /* warning		*/
#define	CE_PANIC	3 /* panic		*/
#define	CE_IGNORE	4 /* print nothing	*/

#ifdef _KERNEL

extern void vcmn_err(int, const char *, __va_list);
extern void cmn_err(int, const char *, ...);

#endif /* _KERNEL */

#define	fm_panic	panic

#endif /* SPL_CMN_ERR_H */
