// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (C) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * SIMD support:
 *
 * Following functions should be called to determine whether CPU feature
 * is supported. All functions are usable in kernel and user space.
 * If a SIMD algorithm is using more than one instruction set
 * all relevant feature test functions should be called.
 *
 * Supported features:
 *   zfs_altivec_available()
 *   zfs_vsx_available()
 *   zfs_isa207_available()
 */

#ifndef _FREEBSD_SIMD_POWERPC_H
#define	_FREEBSD_SIMD_POWERPC_H

#include <sys/types.h>
#include <sys/cdefs.h>

#include <machine/pcb.h>
#include <machine/cpu.h>

#define	kfpu_allowed()		0
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)

/*
 * Check if Altivec is available
 */
static inline boolean_t
zfs_altivec_available(void)
{
	return ((cpu_features & PPC_FEATURE_HAS_ALTIVEC) != 0);
}

/*
 * Check if VSX is available
 */
static inline boolean_t
zfs_vsx_available(void)
{
	return ((cpu_features & PPC_FEATURE_HAS_VSX) != 0);
}

/*
 * Check if POWER ISA 2.07 is available (SHA2)
 */
static inline boolean_t
zfs_isa207_available(void)
{
	return ((cpu_features2 & PPC_FEATURE2_ARCH_2_07) != 0);
}

#endif
