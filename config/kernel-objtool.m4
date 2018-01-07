dnl #
dnl # 4.6 API for compile-time stack validation
dnl #
AC_DEFUN([ZFS_AC_KERNEL_OBJTOOL], [
	AC_MSG_CHECKING([for compile-time stack validation (objtool)])
	ZFS_LINUX_TRY_COMPILE([
		#undef __ASSEMBLY__
		#include <asm/frame.h>
	],[
		#if !defined(FRAME_BEGIN)
		CTASSERT(1);
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_OBJTOOL, 1,
		    [kernel does stack verification])

		ZFS_AC_KERNEL_STACK_FRAME_NON_STANDARD
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.6 API added STACK_FRAME_NON_STANDARD macro
dnl #
AC_DEFUN([ZFS_AC_KERNEL_STACK_FRAME_NON_STANDARD], [
	AC_MSG_CHECKING([whether STACK_FRAME_NON_STANDARD is defined])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/frame.h>
	],[
		#if !defined(STACK_FRAME_NON_STANDARD)
		CTASSERT(1);
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_STACK_FRAME_NON_STANDARD, 1,
		   [STACK_FRAME_NON_STANDARD is defined])
	],[
		AC_MSG_RESULT(no)
	])
])
