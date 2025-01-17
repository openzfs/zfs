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

#ifndef	_SPL_SYS_CONSOLE_H
#define	_SPL_SYS_CONSOLE_H

static inline void
console_vprintf(const char *fmt, va_list args)
{
	vprintf(fmt, args);
}

static inline void
console_printf(const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	console_vprintf(fmt, args);
	va_end(args);
}

#endif /* _SPL_SYS_CONSOLE_H */
