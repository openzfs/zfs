// SPDX-License-Identifier: BSD-2-Clause
#ifndef	_NETBSD_SIMD_POWERPC_H
#define	_NETBSD_SIMD_POWERPC_H

/*
 * NetBSD PowerPC SIMD.
 *
 * There is no fpu_kern_enter.  kthread_fpu_enter(9) is kthread-only
 * (KASSERT LW_SYSTEM) and kthread_fpu_enter_md is a weak no-op on
 * this port.  zfs_kfpu_begin_ppc / zfs_kfpu_end_ppc save PCU state,
 * raise IPL_VM, and enable PSL_FP|PSL_VEC|PSL_VSX.  End zeros
 * FP/VSX/AltiVec then restores MSR in one asm block (same as
 * fpu_kern_leave).  VSX zero (xxlxor) is 64-bit only; gas sees
 * those opcodes via .machine, not -maltivec on the C TU.
 *
 * Homogeneous PPC is assumed; cpu_altivec / PVR are system-wide.
 */

#include <sys/types.h>

#ifdef _KERNEL
#ifdef __powerpc__
#include <powerpc/cpu.h>
#endif
/*
 * powerpc/cpu.h wraps extern int cpu_altivec in
 * !PPC_IBM4XX && !PPC_BOOKE && !_MODULE.  Kmods are built with
 * -D_MODULE, so the extern is hidden.  GENERIC still exports it.
 */
extern int cpu_altivec;
extern int zfs_kfpu_hw_ok;
int	zfs_kfpu_init_netbsd(void);
void	zfs_kfpu_begin_ppc(void);
void	zfs_kfpu_end_ppc(void);

#define	kfpu_init()		zfs_kfpu_init_netbsd()
#define	kfpu_fini()		do { zfs_kfpu_hw_ok = 0; } while (0)
#define	kfpu_allowed()		(zfs_kfpu_hw_ok)
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		zfs_kfpu_begin_ppc()
#define	kfpu_end()		zfs_kfpu_end_ppc()
#else
#define	kfpu_init()		(0)
#define	kfpu_fini()		do {} while (0)
#define	kfpu_allowed()		1
#define	kfpu_initialize(tsk)	do {} while (0)
#define	kfpu_begin()		do {} while (0)
#define	kfpu_end()		do {} while (0)
#endif

#ifdef _KERNEL
/*
 * PVR version field is a family id, not an ISA rank.
 * e6500 is 0x8040 (ISA 2.06, no VSX) -- ">= 0x004b" would lie.
 * POWER boxes are homogeneous; mfpvr() is the system value.
 */
static inline boolean_t
zfs_pvr_power7_plus(uint32_t ver)
{
	switch (ver) {
	case 0x003f:	/* POWER7 */
	case 0x004a:	/* POWER7+ */
	case 0x004b:	/* POWER8E */
	case 0x004c:	/* POWER8NVL */
	case 0x004d:	/* POWER8 */
	case 0x004e:	/* POWER9 */
	case 0x004f:
	case 0x0080:	/* POWER10 */
	case 0x0081:
	case 0x0082:	/* POWER11 */
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}

static inline boolean_t
zfs_pvr_power8_plus(uint32_t ver)
{
	switch (ver) {
	case 0x004b:
	case 0x004c:
	case 0x004d:
	case 0x004e:
	case 0x004f:
	case 0x0080:
	case 0x0081:
	case 0x0082:
		return (B_TRUE);
	default:
		return (B_FALSE);
	}
}
#endif

static inline boolean_t
zfs_altivec_available(void)
{
#ifdef _KERNEL
	/*
	 * cpu_altivec is set at CPU attach (also machdep.altivec).
	 * System-wide.  NetBSD kmods resolve kernel globals.
	 */
	return (cpu_altivec != 0);
#else
	return (B_FALSE);
#endif
}

static inline boolean_t
zfs_vsx_available(void)
{
#ifdef _KERNEL
	return (zfs_pvr_power7_plus(mfpvr() >> 16));
#else
	return (B_FALSE);
#endif
}

static inline boolean_t
zfs_isa207_available(void)
{
#ifdef _KERNEL
	return (zfs_pvr_power8_plus(mfpvr() >> 16));
#else
	return (B_FALSE);
#endif
}

#endif /* _NETBSD_SIMD_POWERPC_H */
