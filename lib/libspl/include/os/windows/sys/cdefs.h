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

#ifndef _LIBSPL_SYS_CDEFS_H
#define	_LIBSPL_SYS_CDEFS_H

#undef BEGIN_C_DECLS
#undef END_C_DECLS
#ifdef __cplusplus
#define	BEGIN_C_DECLS extern "C" {
#define	END_C_DECLS }
#else
#define	BEGIN_C_DECLS /* empty */
#define	END_C_DECLS /* empty */
#endif

/* FreeBSD still uses the legacy defines */
#define	__BEGIN_DECLS BEGIN_C_DECLS
#define	__END_DECLS END_C_DECLS

#define	__unused __maybe_unused

#ifndef	__DECONST
#define	__DECONST(type, var) ((type)(uintptr_t)(const void *)(var))
#endif

#endif
