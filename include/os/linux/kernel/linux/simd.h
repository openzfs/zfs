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
