// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_NETBSD_SIMD_AARCH64_H
#define	_NETBSD_SIMD_AARCH64_H

/*
 * NetBSD aarch64 SIMD.
 *
 * Kernel FPU: fpu_kern_enter/leave (sys/arch/aarch64/aarch64/fpu.c)
 * save PCU state, enable CPACR, and zero on leave.  Safe from
 * syscall context and from kthreads (LW_SYSTEM|LW_SYSTEM_FPU is
 * a no-op).  kthread_fpu_enter(9) is kthread-only -- do not use it
 * here.
 *
 * kfpu_allowed() is the intersection of AdvSIMD across every CPU.
 * RAIDZ neon is_supported is only kfpu_allowed(), so a little core
 * with ID_AA64PFR0.AdvSIMD == NONE disables every SIMD impl.
 */

#include <sys/types.h>

#ifdef _KERNEL
#include <sys/cpu.h>
#ifdef __aarch64__
#include <aarch64/cpu.h>
#include <aarch64/armreg.h>
/*
 * fpu_kern_enter/leave live in arch/aarch64/aarch64/fpu.c and
 * are declared in <arm/fpu.h>.  Declare them here so the kmod
 * does not depend on the arm/ include path.
 */
void	fpu_kern_enter(void);
void	fpu_kern_leave(void);
#endif

#ifndef ID_AA64PFR0_EL1_ADVSIMD
#define	ID_AA64PFR0_EL1_ADVSIMD		__BITS(23, 20)
#endif
#ifndef ID_AA64PFR0_EL1_ADV_SIMD_NONE
#define	ID_AA64PFR0_EL1_ADV_SIMD_NONE	0xf
#endif

extern int zfs_kfpu_hw_ok;
int	zfs_kfpu_init_netbsd(void);

#define	kfpu_init()		zfs_kfpu_init_netbsd()
#define	kfpu_fini()		do { zfs_kfpu_hw_ok = 0; } while (0)
#define	kfpu_allowed()		(zfs_kfpu_hw_ok)
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		fpu_kern_enter()
#define	kfpu_end()		fpu_kern_leave()

#else /* !_KERNEL */
/*
 * Userland checksum dispatch uses lib/libspl/include/sys/simd.h.
 * This branch is only hit if something includes this header
 * without _KERNEL.  NetBSD has neither <sys/auxv.h> nor
 * elf_aux_info(3).  AArch64 ABI requires ASIMD; SHA comes
 * from machdep.cpu_id (same probe as libspl).
 */
#include <stddef.h>
#include <sys/sysctl.h>
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		zfs_neon_available()
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#endif /* _KERNEL */

static inline uint64_t
zfs_id_aa64isar0(void)
{
#ifdef _KERNEL
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;
	uint64_t isar0 = ~(uint64_t)0;

	/*
	 * Intersection of every CPU's cached ID_AA64ISAR0 so
	 * checksum dispatch never picks SHA-256 on a core that
	 * lacks it.  curcpu() MRS would be wrong on a big.LITTLE
	 * system if zio later runs on a different CPU.
	 */
	for (CPU_INFO_FOREACH(cii, ci))
		isar0 &= ci->ci_id.ac_aa64isar0;
	return (isar0);
#else
	/*
	 * Userland cannot MRS.  machdep.cpu_id is the same
	 * ID_AA64ISAR0 the kernel caches.  No AT_HWCAP on NetBSD.
	 */
	struct {
		uint64_t	ac_midr, ac_revidr, ac_mpidr;
		uint64_t	ac_aa64dfr0, ac_aa64dfr1;
		uint64_t	ac_aa64isar0, ac_aa64isar1;
		uint64_t	ac_aa64mmfr0, ac_aa64mmfr1, ac_aa64mmfr2;
		uint64_t	ac_aa64pfr0;
	} id;
	size_t len = sizeof (id);

	if (sysctlbyname("machdep.cpu_id", &id, &len, NULL, 0) != 0 ||
	    len < 6 * sizeof (uint64_t))
		return (0);
	return (id.ac_aa64isar0);
#endif
}

static inline boolean_t
zfs_neon_available(void)
{
#ifdef _KERNEL
	CPU_INFO_ITERATOR cii;
	struct cpu_info *ci;

	/*
	 * Intersection of AdvSIMD.  0x0 = implemented, 0x1 = +FP16,
	 * 0xf = none.  A little core without ASIMD must disable
	 * every SIMD impl -- RAIDZ neon only checks kfpu_allowed().
	 */
	for (CPU_INFO_FOREACH(cii, ci)) {
		uint64_t simd = __SHIFTOUT(ci->ci_id.ac_aa64pfr0,
		    ID_AA64PFR0_EL1_ADVSIMD);
		if (simd == ID_AA64PFR0_EL1_ADV_SIMD_NONE)
			return (B_FALSE);
	}
	return (B_TRUE);
#else
	/*
	 * AArch64 ABI requires ASIMD.  Confirm via machdep.cpu_id
	 * when present; if sysctl fails, still allow NEON.
	 */
	struct {
		uint64_t	ac_midr, ac_revidr, ac_mpidr;
		uint64_t	ac_aa64dfr0, ac_aa64dfr1;
		uint64_t	ac_aa64isar0, ac_aa64isar1;
		uint64_t	ac_aa64mmfr0, ac_aa64mmfr1, ac_aa64mmfr2;
		uint64_t	ac_aa64pfr0;
	} id;
	size_t len = sizeof (id);
	uint64_t simd;

	if (sysctlbyname("machdep.cpu_id", &id, &len, NULL, 0) != 0 ||
	    len < sizeof (id))
		return (B_TRUE);
	simd = (id.ac_aa64pfr0 >> 20) & 0xf;
	return (simd != 0xf);
#endif
}

static inline boolean_t
zfs_sha256_available(void)
{
	return (((zfs_id_aa64isar0() >> 12) & 0xf) != 0);
}

static inline boolean_t
zfs_sha512_available(void)
{
	return (((zfs_id_aa64isar0() >> 12) & 0xf) >= 2);
}

#endif /* _NETBSD_SIMD_AARCH64_H */
