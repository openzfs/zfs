/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
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
#ifndef _SPL_STRING_H
#define	_SPL_STRING_H

/*
 * strcmp() has been deprecated in macOS 11, but case is needed to change
 * to strncmp(). For now, we just create a simple spl_strcmp() until
 * upstream can be changed.
 */
#ifdef __cplusplus
extern "C" {
#endif

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>
#if defined(MAC_OS_VERSION_11_0) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0)
#define	strcmp XNU_strcmp
#endif /* MAC_OS */

#include_next <string.h>

#if defined(MAC_OS_VERSION_11_0) &&	\
	(MAC_OS_X_VERSION_MIN_REQUIRED >= MAC_OS_VERSION_11_0)
extern int spl_strcmp(const char *, const char *);
#undef strcmp
#define	strcmp spl_strcmp
#endif /* MAC_OS */

#ifdef __cplusplus
}
#endif

#endif
