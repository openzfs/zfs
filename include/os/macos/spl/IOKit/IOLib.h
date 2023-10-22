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
 *
 * Copyright (C) 2023 Sean Doran <smd@use.net>
 *
 */

#ifndef _SPL_IOLIB_H
#define	_SPL_IOLIB_H

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#include_next <IOKit/IOLib.h>

#ifndef IOMallocType
#define	IOMallocType(T) (T *)IOMallocAligned(sizeof (T), _Alignof(T))
/*
 * Do a compile-time check that pointer P is of type T *.
 * Any kind of optimization eliminates the declaration and
 * assignment, leaving only the free itself and setting
 * the pointer to NULL to frustrate use-after-free.
 */
#define	IOFreeType(P, T) do { IOFreeAligned(P, sizeof (T));		\
		T tmp;							\
		P = &tmp;						\
		P = NULL; } while (0)
#endif

#endif
