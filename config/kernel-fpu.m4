dnl # 
dnl # Handle differences in kernel FPU code.
dnl #
dnl # Kernel
dnl # 5.0:	Wrappers have been introduced to save/restore the FPU state.
dnl #		This change was made to the 4.19.38 and 4.14.120 LTS kernels.
dnl #		HAVE_KERNEL_FPU_INTERNAL
dnl #
dnl # 4.2:	Use __kernel_fpu_{begin,end}()
dnl #		HAVE_UNDERSCORE_KERNEL_FPU & KERNEL_EXPORTS_X86_FPU
dnl #
dnl # Pre-4.2:	Use kernel_fpu_{begin,end}()
dnl #		HAVE_KERNEL_FPU & KERNEL_EXPORTS_X86_FPU
dnl #
dnl # N.B. The header check is performed before all other checks since it
dnl # depends on HAVE_KERNEL_FPU_API_HEADER being set in confdefs.h.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FPU_HEADER], [
	AC_MSG_CHECKING([whether fpu headers are available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
		#include <asm/fpu/api.h>
	],[
	],[
		AC_DEFINE(HAVE_KERNEL_FPU_API_HEADER, 1,
		    [kernel has asm/fpu/api.h])
		AC_MSG_RESULT(asm/fpu/api.h)
	],[
		AC_MSG_RESULT(i387.h & xcr.h)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_FPU], [
	ZFS_LINUX_TEST_SRC([kernel_fpu], [
		#include <linux/types.h>
		#ifdef HAVE_KERNEL_FPU_API_HEADER
		#include <asm/fpu/api.h>
		#else
		#include <asm/i387.h>
		#include <asm/xcr.h>
		#endif
	], [
		kernel_fpu_begin();
		kernel_fpu_end();
	], [], [ZFS_META_LICENSE])

	ZFS_LINUX_TEST_SRC([__kernel_fpu], [
		#include <linux/types.h>
		#ifdef HAVE_KERNEL_FPU_API_HEADER
		#include <asm/fpu/api.h>
		#else
		#include <asm/i387.h>
		#include <asm/xcr.h>
		#endif
	], [
		__kernel_fpu_begin();
		__kernel_fpu_end();
	], [], [ZFS_META_LICENSE])

	ZFS_LINUX_TEST_SRC([fpu_internal], [
		#if defined(__x86_64) || defined(__x86_64__) || \
		    defined(__i386) || defined(__i386__)
		#if !defined(__x86)
		#define __x86
		#endif
		#endif

		#if !defined(__x86)
		#error Unsupported architecture
		#endif

		#include <linux/types.h>
		#ifdef HAVE_KERNEL_FPU_API_HEADER
		#include <asm/fpu/api.h>
		#include <asm/fpu/internal.h>
		#else
		#include <asm/i387.h>
		#include <asm/xcr.h>
		#endif

		#if !defined(XSTATE_XSAVE)
		#error XSTATE_XSAVE not defined
		#endif

		#if !defined(XSTATE_XRESTORE)
		#error XSTATE_XRESTORE not defined
		#endif
	],[
		struct fpu *fpu = &current->thread.fpu;
		union fpregs_state *st = &fpu->state;
		struct fregs_state *fr __attribute__ ((unused)) = &st->fsave;
		struct fxregs_state *fxr __attribute__ ((unused)) = &st->fxsave;
		struct xregs_state *xr __attribute__ ((unused)) = &st->xsave;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FPU], [
	dnl #
	dnl # Legacy kernel
	dnl #
	AC_MSG_CHECKING([whether kernel fpu is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([kernel_fpu_license],
	    [kernel_fpu_begin], [arch/x86/kernel/fpu/core.c], [
		AC_MSG_RESULT(kernel_fpu_*)
		AC_DEFINE(HAVE_KERNEL_FPU, 1,
		    [kernel has kernel_fpu_* functions])
		AC_DEFINE(KERNEL_EXPORTS_X86_FPU, 1,
		    [kernel exports FPU functions])
	],[
		dnl #
		dnl # Linux 4.2 kernel
		dnl #
		ZFS_LINUX_TEST_RESULT_SYMBOL([__kernel_fpu_license],
		    [__kernel_fpu_begin],
		    [arch/x86/kernel/fpu/core.c arch/x86/kernel/i387.c], [
			AC_MSG_RESULT(__kernel_fpu_*)
			AC_DEFINE(HAVE_UNDERSCORE_KERNEL_FPU, 1,
			    [kernel has __kernel_fpu_* functions])
			AC_DEFINE(KERNEL_EXPORTS_X86_FPU, 1,
			    [kernel exports FPU functions])
		],[
			ZFS_LINUX_TEST_RESULT([fpu_internal], [
				AC_MSG_RESULT(internal)
				AC_DEFINE(HAVE_KERNEL_FPU_INTERNAL, 1,
				    [kernel fpu internal])
			],[
				AC_MSG_RESULT(unavailable)
			])
		])
	])
])
