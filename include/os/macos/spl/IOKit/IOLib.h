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
