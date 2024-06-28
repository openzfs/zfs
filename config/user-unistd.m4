dnl #
dnl # Check for SEEK_DATA - only used for cp_files/seekflood test case.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_UNISTD_SEEK_DATA], [
	AC_MSG_CHECKING(whether host toolchain supports SEEK_DATA)

	AC_COMPILE_IFELSE([AC_LANG_SOURCE([[
		#ifndef _GNU_SOURCE
		#define _GNU_SOURCE
		#endif
		#include <unistd.h>
		#if defined(SEEK_DATA)
			int ok;
		#else
			error fail
		#endif
	]])], [
		user_unistd_seek_data=yes
		AC_MSG_RESULT([yes])
	], [
		user_unistd_seek_data=no
		AC_MSG_RESULT([no])
	])
])
