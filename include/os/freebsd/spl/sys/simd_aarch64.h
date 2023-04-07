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
 *   zfs_neon_available()
 *   zfs_sha256_available()
 *   zfs_sha512_available()
 */

#ifndef _FREEBSD_SIMD_AARCH64_H
#define	_FREEBSD_SIMD_AARCH64_H

#include <sys/types.h>
#include <sys/ucontext.h>
#include <machine/elf.h>
#include <machine/fpu.h>
#include <machine/md_var.h>
#include <machine/pcb.h>

#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin() do {						\
	if (__predict_false(!is_fpu_kern_thread(0)))			\
		fpu_kern_enter(curthread, NULL, FPU_KERN_NOCTX);	\
} while (0)

#define	kfpu_end() do {							\
	if (__predict_false(curthread->td_pcb->pcb_fpflags & PCB_FP_NOSAVE)) \
		fpu_kern_leave(curthread, NULL);			\
} while (0)
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)

/*
 * Check if NEON is available
 */
static inline boolean_t
zfs_neon_available(void)
{
	return (elf_hwcap & HWCAP_FP);
}

/*
 * Check if SHA256 is available
 */
static inline boolean_t
zfs_sha256_available(void)
{
	return (elf_hwcap & HWCAP_SHA2);
}

/*
 * Check if SHA512 is available
 */
static inline boolean_t
zfs_sha512_available(void)
{
	return (elf_hwcap & HWCAP_SHA512);
}

#endif /* _FREEBSD_SIMD_AARCH64_H */
