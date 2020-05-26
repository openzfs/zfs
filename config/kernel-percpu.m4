dnl #
dnl # 3.18 API change,
dnl # The function percpu_counter_init now must be passed a GFP mask.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PERCPU_COUNTER_INIT], [
	ZFS_LINUX_TEST_SRC([percpu_counter_init_with_gfp], [
		#include <linux/gfp.h>
		#include <linux/percpu_counter.h>
	],[
		struct percpu_counter counter;
		int error;

		error = percpu_counter_init(&counter, 0, GFP_KERNEL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PERCPU_COUNTER_INIT], [
	AC_MSG_CHECKING([whether percpu_counter_init() wants gfp_t])
	ZFS_LINUX_TEST_RESULT([percpu_counter_init_with_gfp], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PERCPU_COUNTER_INIT_WITH_GFP, 1,
		    [percpu_counter_init() wants gfp_t])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_PERCPU], [
	ZFS_AC_KERNEL_SRC_PERCPU_COUNTER_INIT
])

AC_DEFUN([ZFS_AC_KERNEL_PERCPU], [
	ZFS_AC_KERNEL_PERCPU_COUNTER_INIT
])
