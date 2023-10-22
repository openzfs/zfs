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
