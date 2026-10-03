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
 * Copyright (C) 2020 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_LIBKERN_H
#define	_SPL_LIBKERN_H

/*
 * We wrap this header to handle that copyinstr()'s final argument is
 * mandatory on OSX. Wrap it to call our ddi_copyinstr to make it optional.
 */

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#include_next <libkern/libkern.h>
#undef copyinstr
#define	copyinstr(U, K, L, D) ddi_copyinstr((U), (K), (L), (D))

#if defined(MAC_OS_X_VERSION_10_11) && \
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_X_VERSION_10_11)
#else
#define	IOSleepWithLeeway(S, D) IOSleep((S))
#endif

#endif
