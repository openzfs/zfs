dnl # 
dnl # Handle differences in kernel FPU code.
dnl #
dnl # Kernel
dnl # 5.0:	All kernel fpu functions are GPL only, so we can't use them.
dnl #		(nothing defined)
dnl #
dnl # 4.2:	Use __kernel_fpu_{begin,end}()
dnl #		HAVE_UNDERSCORE_KERNEL_FPU & KERNEL_EXPORTS_X86_FPU
dnl #
dnl # Pre-4.2:	Use kernel_fpu_{begin,end}()
dnl #		HAVE_KERNEL_FPU & KERNEL_EXPORTS_X86_FPU
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FPU], [
	AC_MSG_CHECKING([which kernel_fpu function to use])
	ZFS_LINUX_TRY_COMPILE([
		#include <asm/i387.h>
		#include <asm/xcr.h>
	],[
		kernel_fpu_begin();
		kernel_fpu_end();
	],[
		AC_MSG_RESULT(kernel_fpu_*)
		AC_DEFINE(HAVE_KERNEL_FPU, 1, [kernel has kernel_fpu_* functions])
		AC_DEFINE(KERNEL_EXPORTS_X86_FPU, 1, [kernel exports FPU functions])
	],[
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/kernel.h>
			#include <asm/fpu/api.h>
		],[
			__kernel_fpu_begin();
			__kernel_fpu_end();
		],[
			AC_MSG_RESULT(__kernel_fpu_*)
			AC_DEFINE(HAVE_UNDERSCORE_KERNEL_FPU, 1, [kernel has __kernel_fpu_* functions])
			AC_DEFINE(KERNEL_EXPORTS_X86_FPU, 1, [kernel exports FPU functions])
		],[
			AC_MSG_RESULT(not exported)
		])
	])
])
