dnl #
dnl # 6.12 removed f_version from struct file
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILE_F_VERSION], [
	ZFS_LINUX_TEST_SRC([file_f_version], [
		#include <linux/fs.h>

		static const struct f __attribute__((unused)) = {
			.f_version = 0;
		};
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FILE_F_VERSION], [
	AC_MSG_CHECKING([whether file->f_version exists])
	ZFS_LINUX_TEST_RESULT([file_f_version], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILE_F_VERSION, 1,
		    [file->f_version exists])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FILE], [
	ZFS_AC_KERNEL_FILE_F_VERSION
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_FILE], [
	ZFS_AC_KERNEL_SRC_FILE_F_VERSION
])
