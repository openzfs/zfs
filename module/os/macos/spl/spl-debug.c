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
 *
 *  Solaris Porting Layer (SPL) Debug Implementation.
 */

#include <sys/sysmacros.h>

/* Debug log support enabled */
__attribute__((noinline)) int assfail(const char *str, const char *file,
	unsigned int line) __attribute__((optnone))
{
	return (1); /* Must return true for ASSERT macro */
}
