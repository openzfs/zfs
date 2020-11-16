dnl #
dnl # Detect objtool functionality.
dnl #

dnl #
dnl # Kernel 5.10: linux/frame.h was renamed linux/objtool.h
dnl #
AC_DEFUN([ZFS_AC_KERNEL_OBJTOOL_HEADER], [
	AC_MSG_CHECKING([whether objtool header is available])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/objtool.h>
	],[
	],[
		AC_DEFINE(HAVE_KERNEL_OBJTOOL_HEADER, 1,
		    [kernel has linux/objtool.h])
		AC_MSG_RESULT(linux/objtool.h)
	],[
		AC_MSG_RESULT(linux/frame.h)
	])
])

dnl #
dnl # Check for objtool support.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_OBJTOOL], [

	dnl # 4.6 API for compile-time stack validation
	ZFS_LINUX_TEST_SRC([objtool], [
		#undef __ASSEMBLY__
		#include <asm/ptrace.h>
		#include <asm/frame.h>
	],[
		#if !defined(FRAME_BEGIN)
		#error "FRAME_BEGIN is not defined"
		#endif
	])

	dnl # 4.6 API added STACK_FRAME_NON_STANDARD macro
	ZFS_LINUX_TEST_SRC([stack_frame_non_standard], [
		#ifdef HAVE_KERNEL_OBJTOOL_HEADER
		#include <linux/objtool.h>
		#else
		#include <linux/frame.h>
		#endif
	],[
		#if !defined(STACK_FRAME_NON_STANDARD)
		#error "STACK_FRAME_NON_STANDARD is not defined."
		#endif
	])
])

AC_DEFUN([ZFS_AC_KERNEL_OBJTOOL], [
	AC_MSG_CHECKING(
	    [whether compile-time stack validation (objtool) is available])
	ZFS_LINUX_TEST_RESULT([objtool], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_OBJTOOL, 1,
		    [kernel does stack verification])

		AC_MSG_CHECKING([whether STACK_FRAME_NON_STANDARD is defined])
		ZFS_LINUX_TEST_RESULT([stack_frame_non_standard], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_STACK_FRAME_NON_STANDARD, 1,
			   [STACK_FRAME_NON_STANDARD is defined])
		],[
			AC_MSG_RESULT(no)
		])
	],[
		AC_MSG_RESULT(no)
	])
])
