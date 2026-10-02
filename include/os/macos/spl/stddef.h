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
 * Copyright (C) 2021 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_STDDEF_H
#define	_SPL_STDDEF_H

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#if defined(MAC_OS_X_VERSION_10_12) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_12)
#include_next <stddef.h>
#endif

/* Older macOS does not have size_t in stddef.h */
#include <sys/types.h>

#endif
