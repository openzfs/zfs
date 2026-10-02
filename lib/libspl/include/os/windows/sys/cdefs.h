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
