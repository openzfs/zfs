/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (C) 2016 Romain Dolbeau <romain@dolbeau.org>.
 */

/*
 * USER API:
 *
 * Kernel fpu methods:
 *	kfpu_allowed()
 *	kfpu_begin()
 *	kfpu_end()
 *	kfpu_init()
 *	kfpu_fini()
 */

#ifndef _LINUX_SIMD_AARCH64_H
#define	_LINUX_SIMD_AARCH64_H

#include <sys/isa_defs.h>

#if defined(__aarch64__)

#include <sys/types.h>
#include <asm/neon.h>

#define	kfpu_allowed()		1
#define	kfpu_begin()		kernel_neon_begin()
#define	kfpu_end()		kernel_neon_end()
#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

#endif /* __aarch64__ */

#endif /* _LINUX_SIMD_AARCH64_H */
