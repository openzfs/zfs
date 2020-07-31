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

#ifndef _SPL_ERR_H
#define	_SPL_ERR_H

#include <sys/debug.h>

void err(int, const char *, ...) _Noreturn __printf0like(2, 3);
void errx(int, const char *, ...) _Noreturn __printf0like(2, 3);
void warnx(const char *, ...) __printflike(1, 2);

inline static void
err(int x, const char *f, ...)
{
}

inline static void
errx(int x, const char *f, ...)
{
	exit(x);
}

inline static void
warnx(const char *, ...)
{
	exit(1);
}

#endif
