dnl #
dnl # Linux 5.15 gets rid of -isystem and external <stdarg.h> inclusion
dnl # and ships its own <linux/stdarg.h>. Check if this header file does
dnl # exist and provide all necessary definitions for variable argument
dnl # functions. Adjust the inclusion of <stdarg.h> according to the
dnl # results.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_STANDALONE_LINUX_STDARG], [
	ZFS_LINUX_TEST_SRC([has_standalone_linux_stdarg], [
		#include <linux/stdarg.h>

		#if !defined(va_start) || !defined(va_end) || \
		    !defined(va_arg) || !defined(va_copy)
		#error "<linux/stdarg.h> is invalid"
		#endif
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_STANDALONE_LINUX_STDARG], [
	dnl #
	dnl # Linux 5.15 ships its own stdarg.h and doesn't allow to
	dnl # include compiler headers.
	dnl #
	AC_MSG_CHECKING([whether standalone <linux/stdarg.h> exists])
	ZFS_LINUX_TEST_RESULT([has_standalone_linux_stdarg], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_STANDALONE_LINUX_STDARG, 1,
			[standalone <linux/stdarg.h> exists])
	],[
		AC_MSG_RESULT([no])
	])
])
