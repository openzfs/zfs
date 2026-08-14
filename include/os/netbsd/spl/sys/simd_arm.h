// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_NETBSD_SIMD_ARM_H
#define	_NETBSD_SIMD_ARM_H

/*
 * NetBSD 32-bit ARM.  No syscall-context kfpu; only kthreads may
 * use NEON via kthread_fpu_enter(9).  Feature bits stay conservative.
 */

#include <sys/types.h>

#ifdef _KERNEL
#include <sys/kthread.h>
#include <sys/lwp.h>
#include <sys/cpu.h>
#ifdef __arm__
#include <arm/cpu.h>
#include <arm/armreg.h>
#endif

#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		0
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#else
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#endif

static inline boolean_t
zfs_neon_available(void)
{
	/*
	 * No system-wide ARM32 feature intersection.  A curcpu()
	 * MVFR/ISAR read would pick an impl that later runs on a
	 * core without NEON/SHA.  Scalar + integer armv7 SHA stay.
	 */
	return (B_FALSE);
}

static inline boolean_t
zfs_sha256_available(void)
{
	return (B_FALSE);
}

#endif /* _NETBSD_SIMD_ARM_H */
