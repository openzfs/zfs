// SPDX-License-Identifier: BSD-2-Clause
/*
 * NetBSD kfpu probe + PowerPC FPU/AltiVec/VSX switch.
 *
 * AArch64 uses fpu_kern_enter/leave; this file only caches the
 * AdvSIMD intersection so kfpu_allowed() stays a load on the
 * raidz/fletcher hot path.
 *
 * PowerPC has no fpu_kern_enter.  Save whatever PCU state is
 * live on this CPU, pin with splvm, and enable the MSR bits the
 * SIMD impls need.  Leave zeros FP/VSX/AltiVec (same as
 * fpu_kern_leave on x86/AArch64) then restores the old MSR
 * so the next user FPU fault reloads from the PCB.
 *
 * Zero + mtmsr live in one asm block so no C prologue/epilogue
 * can reload a nonvolatile FPR/VR after the sanitize.  This TU
 * is not built with -maltivec/-mvsx; .machine directives make
 * the opcodes legal for gas.  xxlxor is 64-bit only.
 */

#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpu.h>
#include <sys/simd.h>

int zfs_kfpu_hw_ok;

int
zfs_kfpu_init_netbsd(void)
{
#if defined(__aarch64__) && !defined(__AARCH64EB__)
	zfs_kfpu_hw_ok = zfs_neon_available() ? 1 : 0;
#elif defined(__powerpc__)
	zfs_kfpu_hw_ok = (zfs_altivec_available() ||
	    zfs_vsx_available()) ? 1 : 0;
#else
	/*
	 * aarch64eb builds no AdvSIMD blake3/raidz asm; x86 routes
	 * through fpu_kern_enter and ignores this flag.
	 */
	zfs_kfpu_hw_ok = 0;
#endif
	return (0);
}

#if defined(__powerpc__)

#include <sys/pcu.h>
#include <powerpc/psl.h>
#include <powerpc/cpu.h>

/*
 * POWER ISA MSR bit 23 (IBM bit 40).  Not in NetBSD psl.h.
 * Only set when zfs_vsx_available() is true.
 */
#ifndef PSL_VSX
#define	PSL_VSX		0x00800000
#endif

#if !defined(PCU_UNIT_COUNT) || PCU_UNIT_COUNT < 1
#error "PowerPC kfpu needs pcu_save_all_on_cpu (machine/types.h PCU_UNIT_COUNT)"
#endif

#ifdef __powerpc64__
#define	ZFS_MTMSR	"mtmsrd %0\n\t"
#else
#define	ZFS_MTMSR	"mtmsr %0\n\t"
#endif

static int		zfs_kfpu_spl[MAXCPUS];
static register_t	zfs_kfpu_msr[MAXCPUS];
static uint8_t		zfs_kfpu_on[MAXCPUS];

/*
 * x86 / AArch64 fpu_kern_leave zeros SIMD so kernel leftovers
 * cannot leak to userland.  Do the same here while PSL_FP /
 * PSL_VEC / PSL_VSX are still on, then drop those bits in the
 * same asm so the compiler cannot restore a callee-saved FPR
 * or VR afterwards.  User state already lives in the PCB.
 */
#define	ZFS_FMR4(a, b, c, d) \
	"fmr " #a ",0\n\t" \
	"fmr " #b ",0\n\t" \
	"fmr " #c ",0\n\t" \
	"fmr " #d ",0\n\t"
#define	ZFS_VXOR4(a, b, c, d) \
	"vxor " #a "," #a "," #a "\n\t" \
	"vxor " #b "," #b "," #b "\n\t" \
	"vxor " #c "," #c "," #c "\n\t" \
	"vxor " #d "," #d "," #d "\n\t"
#if defined(__powerpc64__)
#define	ZFS_XXLXOR4(a, b, c, d) \
	"xxlxor " #a "," #a "," #a "\n\t" \
	"xxlxor " #b "," #b "," #b "\n\t" \
	"xxlxor " #c "," #c "," #c "\n\t" \
	"xxlxor " #d "," #d "," #d "\n\t"
#endif

static void
zfs_kfpu_leave_ppc(register_t msr)
{
	static const double zfs_kfpu_dzero = 0.0;

#if defined(__powerpc64__)
	if (zfs_vsx_available()) {
		/*
		 * vs0-vs63 are the full VSX bank.  vs0-vs31 overlay
		 * f0-f31 / v0-v31; vs32-vs63 are VSX-only.
		 * .machine power7 makes xxlxor legal without -mvsx
		 * on this TU (which must stay soft-float).
		 */
		__asm volatile(
		    ".machine push\n\t"
		    ".machine power7\n\t"
		    ZFS_XXLXOR4(0, 1, 2, 3)
		    ZFS_XXLXOR4(4, 5, 6, 7)
		    ZFS_XXLXOR4(8, 9, 10, 11)
		    ZFS_XXLXOR4(12, 13, 14, 15)
		    ZFS_XXLXOR4(16, 17, 18, 19)
		    ZFS_XXLXOR4(20, 21, 22, 23)
		    ZFS_XXLXOR4(24, 25, 26, 27)
		    ZFS_XXLXOR4(28, 29, 30, 31)
		    ZFS_XXLXOR4(32, 33, 34, 35)
		    ZFS_XXLXOR4(36, 37, 38, 39)
		    ZFS_XXLXOR4(40, 41, 42, 43)
		    ZFS_XXLXOR4(44, 45, 46, 47)
		    ZFS_XXLXOR4(48, 49, 50, 51)
		    ZFS_XXLXOR4(52, 53, 54, 55)
		    ZFS_XXLXOR4(56, 57, 58, 59)
		    ZFS_XXLXOR4(60, 61, 62, 63)
		    "mtvscr 0\n\t"
		    "mtfsf 255,0\n\t"
		    ".machine pop\n\t"
		    ZFS_MTMSR
		    "isync\n\t"
		    :
		    : "r" (msr)
		    : "memory");
		return;
	}
#endif

	if (zfs_altivec_available()) {
		__asm volatile(
		    "lfd 0,%1\n\t"
		    ZFS_FMR4(1, 2, 3, 4)
		    ZFS_FMR4(5, 6, 7, 8)
		    ZFS_FMR4(9, 10, 11, 12)
		    ZFS_FMR4(13, 14, 15, 16)
		    ZFS_FMR4(17, 18, 19, 20)
		    ZFS_FMR4(21, 22, 23, 24)
		    ZFS_FMR4(25, 26, 27, 28)
		    "fmr 29,0\n\t"
		    "fmr 30,0\n\t"
		    "fmr 31,0\n\t"
		    "mtfsf 255,0\n\t"
		    ".machine push\n\t"
		    ".machine altivec\n\t"
		    ZFS_VXOR4(0, 1, 2, 3)
		    ZFS_VXOR4(4, 5, 6, 7)
		    ZFS_VXOR4(8, 9, 10, 11)
		    ZFS_VXOR4(12, 13, 14, 15)
		    ZFS_VXOR4(16, 17, 18, 19)
		    ZFS_VXOR4(20, 21, 22, 23)
		    ZFS_VXOR4(24, 25, 26, 27)
		    ZFS_VXOR4(28, 29, 30, 31)
		    "mtvscr 0\n\t"
		    ".machine pop\n\t"
		    ZFS_MTMSR
		    "isync\n\t"
		    :
		    : "r" (msr), "m" (zfs_kfpu_dzero)
		    : "memory");
		return;
	}

	__asm volatile(
	    "lfd 0,%1\n\t"
	    ZFS_FMR4(1, 2, 3, 4)
	    ZFS_FMR4(5, 6, 7, 8)
	    ZFS_FMR4(9, 10, 11, 12)
	    ZFS_FMR4(13, 14, 15, 16)
	    ZFS_FMR4(17, 18, 19, 20)
	    ZFS_FMR4(21, 22, 23, 24)
	    ZFS_FMR4(25, 26, 27, 28)
	    "fmr 29,0\n\t"
	    "fmr 30,0\n\t"
	    "fmr 31,0\n\t"
	    "mtfsf 255,0\n\t"
	    ZFS_MTMSR
	    "isync\n\t"
	    :
	    : "r" (msr), "m" (zfs_kfpu_dzero)
	    : "memory");
}

#undef	ZFS_FMR4
#undef	ZFS_VXOR4
#if defined(__powerpc64__)
#undef	ZFS_XXLXOR4
#endif
#undef	ZFS_MTMSR

void
zfs_kfpu_begin_ppc(void)
{
	int idx;
	int s;
	register_t msr;
	register_t enable;

	s = splvm();
	idx = cpu_index(curcpu());
	KASSERT(idx >= 0 && idx < MAXCPUS);
	KASSERT(zfs_kfpu_on[idx] == 0);
	zfs_kfpu_spl[idx] = s;
	zfs_kfpu_on[idx] = 1;

	pcu_save_all_on_cpu();

	msr = mfmsr();
	zfs_kfpu_msr[idx] = msr;
	enable = PSL_FP;
	if (zfs_altivec_available())
		enable |= PSL_VEC;
	if (zfs_vsx_available())
		enable |= PSL_VSX;
	mtmsr(msr | enable);
	__asm volatile("isync" ::: "memory");
}

void
zfs_kfpu_end_ppc(void)
{
	int idx;
	int s;

	idx = cpu_index(curcpu());
	KASSERT(idx >= 0 && idx < MAXCPUS);
	KASSERT(zfs_kfpu_on[idx] != 0);

	zfs_kfpu_leave_ppc(zfs_kfpu_msr[idx]);

	s = zfs_kfpu_spl[idx];
	zfs_kfpu_on[idx] = 0;
	splx(s);
}

#endif /* __powerpc__ */
