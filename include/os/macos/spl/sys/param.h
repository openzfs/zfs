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

#ifndef _SPL_PARAM_H
#define	_SPL_PARAM_H

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

#include_next <sys/param.h>
#include <mach/vm_param.h>

/* Pages to bytes and back */
#define	ptob(pages)			(pages << PAGE_SHIFT)
#define	btop(bytes)			(bytes >> PAGE_SHIFT)

#define	PAGESHIFT			PAGE_SHIFT

#define	MAXUID				UINT32_MAX

#endif /* SPL_PARAM_H */
