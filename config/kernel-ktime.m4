dnl #
dnl # 4.18: ktime_get_coarse_real_ts64() replaces current_kernel_time64().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KTIME_GET_COARSE_REAL_TS64], [
	ZFS_LINUX_TEST_SRC([ktime_get_coarse_real_ts64], [
		#include <linux/mm.h>
	], [
		struct timespec64 ts;
		ktime_get_coarse_real_ts64(&ts);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KTIME_GET_COARSE_REAL_TS64], [
	AC_MSG_CHECKING([whether ktime_get_coarse_real_ts64() exists])
	ZFS_LINUX_TEST_RESULT([ktime_get_coarse_real_ts64], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTIME_GET_COARSE_REAL_TS64, 1,
		    [ktime_get_coarse_real_ts64() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.18: ktime_get_raw_ts64() replaces getrawmonotonic64().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KTIME_GET_RAW_TS64], [
	ZFS_LINUX_TEST_SRC([ktime_get_raw_ts64], [
		#include <linux/mm.h>
	], [
		struct timespec64 ts;
		ktime_get_raw_ts64(&ts);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KTIME_GET_RAW_TS64], [
	AC_MSG_CHECKING([whether ktime_get_raw_ts64() exists])
	ZFS_LINUX_TEST_RESULT([ktime_get_raw_ts64], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KTIME_GET_RAW_TS64, 1,
		    [ktime_get_raw_ts64() exists])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTIME], [
	ZFS_AC_KERNEL_SRC_KTIME_GET_COARSE_REAL_TS64
	ZFS_AC_KERNEL_SRC_KTIME_GET_RAW_TS64
])

AC_DEFUN([ZFS_AC_KERNEL_KTIME], [
	ZFS_AC_KERNEL_KTIME_GET_COARSE_REAL_TS64
	ZFS_AC_KERNEL_KTIME_GET_RAW_TS64
])
