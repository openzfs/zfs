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
		objtool_header=$LINUX/include/linux/objtool.h
		AC_DEFINE(HAVE_KERNEL_OBJTOOL_HEADER, 1,
		    [kernel has linux/objtool.h])
		AC_MSG_RESULT(linux/objtool.h)
	],[
		objtool_header=$LINUX/include/linux/frame.h
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

	dnl # 6.15 made CONFIG_OBJTOOL_WERROR=y the default. We need to handle
	dnl # this or our build will fail.
	ZFS_LINUX_TEST_SRC([config_objtool_werror], [
		#if !defined(CONFIG_OBJTOOL_WERROR)
		#error "CONFIG_OBJTOOL_WERROR is not defined."
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

			dnl # Needed for kernels missing the asm macro. We grep
			dnl # for it in the header file since there is currently
			dnl # no test to check the result of assembling a file.
			AC_MSG_CHECKING(
			    [whether STACK_FRAME_NON_STANDARD asm macro is defined])
			dnl # Escape square brackets.
			sp='@<:@@<:@:space:@:>@@:>@'
			dotmacro='@<:@.@:>@macro'
			regexp="^$sp*$dotmacro$sp+STACK_FRAME_NON_STANDARD$sp"
			AS_IF([$EGREP -s -q "$regexp" $objtool_header],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_STACK_FRAME_NON_STANDARD_ASM, 1,
				   [STACK_FRAME_NON_STANDARD asm macro is defined])
			],[
				AC_MSG_RESULT(no)
			])
		],[
			AC_MSG_RESULT(no)
		])

		AC_MSG_CHECKING([whether CONFIG_OBJTOOL_WERROR is defined])
		ZFS_LINUX_TEST_RESULT([config_objtool_werror],[
			AC_MSG_RESULT(yes)
			CONFIG_OBJTOOL_WERROR_DEFINED=yes
		],[
			AC_MSG_RESULT(no)
		])
	],[
		AC_MSG_RESULT(no)
	])
])
