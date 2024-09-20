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
 * Copyright (C) 2019 Lawrence Livermore National Security, LLC.
 */

#ifndef _LINUX_SIMD_H
#define	_LINUX_SIMD_H

#if defined(__x86)
#include <linux/simd_x86.h>

#elif defined(__arm__)
#include <linux/simd_arm.h>

#elif defined(__aarch64__)
#include <linux/simd_aarch64.h>

#elif defined(__powerpc__)
#include <linux/simd_powerpc.h>

#else
#define	kfpu_allowed()		0
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		0
#define	kfpu_fini()		((void) 0)

#endif

void simd_stat_init(void);
void simd_stat_fini(void);

#endif /* _LINUX_SIMD_H */
