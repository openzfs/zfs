dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 5.6 API change
dnl # Before 5.6, fs_parse() took a struct fs_parameter_description
dnl # which wraps the parameter specs with name and enum pointers. From 5.6,
dnl # the description struct was removed and fs_parse() accepts the
dnl # fs_parameter_spec directly.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FS_PARSE], [
	ZFS_LINUX_TEST_SRC([fs_parse], [
		#include <linux/fs_context.h>
		#include <linux/fs_parser.h>
	],[
		static const struct fs_parameter_spec specs[] = {
			{}
		};
		int test __attribute__ ((unused));
		struct fs_context *fc __attribute__ ((unused)) = NULL;
		struct fs_parameter param __attribute__ ((unused));
		struct fs_parse_result result __attribute__ ((unused));
		test = fs_parse(fc, specs, &param, &result);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FS_PARSE], [
	AC_MSG_CHECKING([whether fs_parse() takes fs_parameter_spec directly])
	ZFS_LINUX_TEST_RESULT([fs_parse], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_PARSE_TAKES_SPEC, 1,
		    [fs_parse() takes fs_parameter_spec directly])
	],[
		AC_MSG_RESULT(no)
	])
])
