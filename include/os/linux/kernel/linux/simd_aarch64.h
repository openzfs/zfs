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
 * Copyright (C) 2022 Tino Reichardt <milky-zfs@mcmilk.de>
 * Copyright (C) 2022 Sebastian Gottschall <s.gottschall@dd-wrt.com>
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
 *
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
 *   zfs_aes_available()
 *   zfs_pmull_available()
 */

#ifndef _LINUX_SIMD_AARCH64_H
#define	_LINUX_SIMD_AARCH64_H

#include <sys/types.h>
#include <asm/neon.h>
#include <asm/elf.h>
#include <asm/hwcap.h>
#include <linux/version.h>
#include <asm/sysreg.h>

#define	ID_AA64PFR0_EL1		sys_reg(3, 0, 0, 1, 0)
#define	ID_AA64ISAR0_EL1	sys_reg(3, 0, 0, 6, 0)

#if (defined(HAVE_KERNEL_NEON) && defined(CONFIG_KERNEL_MODE_NEON))
#define	kfpu_allowed()		1
#define	kfpu_begin()		kernel_neon_begin()
#define	kfpu_end()		kernel_neon_end()
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#else
#ifndef HAVE_KERNEL_FPU_INTERNAL
#error Should have one of HAVE_KERNEL_FPU_INTERNAL or HAVE KERNEL_NEON
#endif
#define	kfpu_allowed()		1

extern uint8_t **zfs_kfpu_fpregs;


/*
 * Free buffer to store FPU state.
 */
static inline void
kfpu_fini(void)
{
	int cpu;

	if (zfs_kfpu_fpregs == NULL)
		return;

	for_each_possible_cpu(cpu) {
		if (zfs_kfpu_fpregs[cpu] != NULL) {
			kfree(zfs_kfpu_fpregs[cpu]);
			zfs_kfpu_fpregs[cpu] = NULL;
		}
	}

	kfree(zfs_kfpu_fpregs);

	zfs_kfpu_fpregs = NULL;
}

/*
 * Alloc buffer to store FPU state.
 */
static inline int
kfpu_init(void)
{
	int cpu;

	zfs_kfpu_fpregs = kzalloc(num_possible_cpus() * sizeof (uint8_t *),
	    GFP_KERNEL);

	if (zfs_kfpu_fpregs == NULL)
		return (-ENOMEM);

	for_each_possible_cpu(cpu) {
		// 32 vector registers + 2 status registers
		zfs_kfpu_fpregs[cpu] = kzalloc((16 * 32) + (2 * 8), GFP_KERNEL);

		if (zfs_kfpu_fpregs[cpu] == NULL) {
			kfpu_fini();
			return (-ENOMEM);
		}
	}

	return (0);
}

static inline void
store_neon_state(uint8_t *buffer) {
	asm volatile(
		"st1 {v0.16b, v1.16b, v2.16b, v3.16b}, [%[buf]], #64\n"
		"st1 {v4.16b, v5.16b, v6.16b, v7.16b}, [%[buf]], #64\n"
		"st1 {v8.16b, v9.16b, v10.16b, v11.16b}, [%[buf]], #64\n"
		"st1 {v12.16b, v13.16b, v14.16b, v15.16b}, [%[buf]], #64\n"
		"st1 {v16.16b, v17.16b, v18.16b, v19.16b}, [%[buf]], #64\n"
		"st1 {v20.16b, v21.16b, v22.16b, v23.16b}, [%[buf]], #64\n"
		"st1 {v24.16b, v25.16b, v26.16b, v27.16b}, [%[buf]], #64\n"
		"st1 {v28.16b, v29.16b, v30.16b, v31.16b}, [%[buf]], #64\n"
		"mrs x1, fpsr\n"
		"mrs x2, fpcr\n"
		"stp x1, x2, [%[buf]]\n"
		: // no outputs
		: [buf] "r" (buffer)
		: "x1", "x2");
}

static inline void
restore_neon_state(const uint8_t *buffer) {
	asm volatile(
		"ld1 {v0.16b, v1.16b, v2.16b, v3.16b}, [%[buf]], #64\n"
		"ld1 {v4.16b, v5.16b, v6.16b, v7.16b}, [%[buf]], #64\n"
		"ld1 {v8.16b, v9.16b, v10.16b, v11.16b}, [%[buf]], #64\n"
		"ld1 {v12.16b, v13.16b, v14.16b, v15.16b}, [%[buf]], #64\n"
		"ld1 {v16.16b, v17.16b, v18.16b, v19.16b}, [%[buf]], #64\n"
		"ld1 {v20.16b, v21.16b, v22.16b, v23.16b}, [%[buf]], #64\n"
		"ld1 {v24.16b, v25.16b, v26.16b, v27.16b}, [%[buf]], #64\n"
		"ld1 {v28.16b, v29.16b, v30.16b, v31.16b}, [%[buf]], #64\n"
		"ldp x1, x2, [%[buf]]\n"
		"msr fpsr, x1\n"
		"msr fpcr, x2\n"
		: // no outputs
		: [buf] "r" (buffer)
		: "x1", "x2");
}

static inline void
kfpu_begin(void)
{
	/*
	 * Preemption and interrupts must be disabled for the critical
	 * region where the FPU state is being modified.
	 */
	preempt_disable();
	local_irq_disable();

	store_neon_state(zfs_kfpu_fpregs[smp_processor_id()]);
}

static inline void
kfpu_end(void)
{
	restore_neon_state(zfs_kfpu_fpregs[smp_processor_id()]);

	local_irq_enable();
	preempt_enable();
}
#endif


#define	get_ftr(id) {				\
	unsigned long __val;			\
	asm("mrs %0, "#id : "=r" (__val));	\
	__val;					\
}

/*
 * Check if NEON is available
 */
static inline boolean_t
zfs_neon_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64PFR0_EL1)) >> 16) & 0xf;
	return (ftr == 0 || ftr == 1);
}

/*
 * Check if SHA256 is available
 */
static inline boolean_t
zfs_sha256_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64ISAR0_EL1)) >> 12) & 0x3;
	return (ftr & 0x1);
}

/*
 * Check if SHA512 is available
 */
static inline boolean_t
zfs_sha512_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64ISAR0_EL1)) >> 12) & 0x3;
	return (ftr & 0x2);
}

/*
 * Check if AES is available
 */
static inline boolean_t
zfs_aes_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64ISAR0_EL1)) >> 4) & 0x3;
	return (ftr & 0b10 || ftr & 0b01);
}

/*
 * Check if PMULL is available
 */
static inline boolean_t
zfs_pmull_available(void)
{
	unsigned long ftr = ((get_ftr(ID_AA64ISAR0_EL1)) >> 4) & 0x3;
	return (ftr & 0b10);
}



#endif /* _LINUX_SIMD_AARCH64_H */
