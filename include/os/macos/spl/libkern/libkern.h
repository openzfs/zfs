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

#endif
