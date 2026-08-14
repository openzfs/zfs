// SPDX-License-Identifier: CDDL-1.0
#ifndef	_LIBSPL_OS_NETBSD_SYS_SIMD_H
#define	_LIBSPL_OS_NETBSD_SYS_SIMD_H

/*
 * NetBSD <sys/dkbad.h> (via disklabel) defines SSE/ECC/BSE/CONT as
 * bad144 error-type constants.  Drop them so the CPUID feature enum
 * in the common header can use those identifiers.
 */
#ifdef SSE
#undef SSE
#endif
#ifdef ECC
#undef ECC
#endif
#ifdef BSE
#undef BSE
#endif
#ifdef CONT
#undef CONT
#endif
#ifdef AES
#undef AES
#endif
#ifdef AVX
#undef AVX
#endif
#ifdef AVX2
#undef AVX2
#endif
#ifdef BMI1
#undef BMI1
#endif
#ifdef BMI2
#undef BMI2
#endif

#if defined(__arm__) || defined(__aarch64__) || defined(__powerpc__)
#define	ZFS_SIMD_OS_PROVIDED	1
#endif

#include_next <sys/simd.h>

#if defined(__arm__)
/*
 * No system-wide ARM32 feature intersection.  Stay on scalar rather
 * than pick NEON on one core and later run on a core without it.
 */
static inline boolean_t
zfs_neon_available(void)
{
	return (B_FALSE);
}

static inline boolean_t
zfs_sha256_available(void)
{
	return (B_FALSE);
}

#elif defined(__aarch64__)
#include <stddef.h>
#include <sys/sysctl.h>

/*
 * Layout of machdep.cpu_id (sys/arch/aarch64/include/armreg.h).
 * Kept local so this header does not pull kernel-only armreg.h.
 */
struct zfs_nb_aa64_cpu_id {
	uint64_t	ac_midr;
	uint64_t	ac_revidr;
	uint64_t	ac_mpidr;
	uint64_t	ac_aa64dfr0;
	uint64_t	ac_aa64dfr1;
	uint64_t	ac_aa64isar0;
	uint64_t	ac_aa64isar1;
	uint64_t	ac_aa64mmfr0;
	uint64_t	ac_aa64mmfr1;
	uint64_t	ac_aa64mmfr2;
	uint64_t	ac_aa64pfr0;
	uint64_t	ac_aa64pfr1;
	uint64_t	ac_aa64zfr0;
	uint32_t	ac_mvfr0;
	uint32_t	ac_mvfr1;
	uint32_t	ac_mvfr2;
	uint32_t	ac_pad;
	uint64_t	ac_clidr;
	uint64_t	ac_ctr;
};

static inline int
zfs_nb_aa64_cpu_id(struct zfs_nb_aa64_cpu_id *id)
{
	size_t len = sizeof (*id);

	if (sysctlbyname("machdep.cpu_id", id, &len, NULL, 0) != 0)
		return (-1);
	if (len < offsetof(struct zfs_nb_aa64_cpu_id, ac_aa64pfr0) +
	    sizeof (uint64_t))
		return (-1);
	return (0);
}

/*
 * AArch64 PCS requires ASIMD.  Confirm via ID_AA64PFR0 when
 * machdep.cpu_id is there; if sysctl fails, still allow NEON.
 */
static inline boolean_t
zfs_neon_available(void)
{
	struct zfs_nb_aa64_cpu_id id;
	uint64_t simd;

	if (zfs_nb_aa64_cpu_id(&id) != 0)
		return (B_TRUE);
	simd = (id.ac_aa64pfr0 >> 20) & 0xf;
	return (simd != 0xf);
}

static inline boolean_t
zfs_sha256_available(void)
{
	struct zfs_nb_aa64_cpu_id id;

	if (zfs_nb_aa64_cpu_id(&id) != 0)
		return (B_FALSE);
	return (((id.ac_aa64isar0 >> 12) & 0xf) != 0);
}

static inline boolean_t
zfs_sha512_available(void)
{
	struct zfs_nb_aa64_cpu_id id;

	if (zfs_nb_aa64_cpu_id(&id) != 0)
		return (B_FALSE);
	return (((id.ac_aa64isar0 >> 12) & 0xf) >= 2);
}

#elif defined(__powerpc__)
#include <sys/sysctl.h>

static inline boolean_t
zfs_altivec_available(void)
{
	int v = 0;
	size_t len = sizeof (v);

	if (sysctlbyname("machdep.altivec", &v, &len, NULL, 0) != 0)
		return (B_FALSE);
	return (v != 0);
}

static inline boolean_t
zfs_vsx_available(void)
{
	/*
	 * NetBSD has no machdep.vsx.  VSX userland impls stay
	 * off; AltiVec still runs when machdep.altivec is set.
	 */
	return (B_FALSE);
}

static inline boolean_t
zfs_isa207_available(void)
{
	return (B_FALSE);
}
#endif /* __arm__ / __aarch64__ / __powerpc__ */

#endif /* _LIBSPL_OS_NETBSD_SYS_SIMD_H */
