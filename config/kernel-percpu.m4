dnl #
dnl # 5.10 API change,
dnl # The "count" was moved into ref->data, from ref
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PERCPU_REF_COUNT_IN_DATA], [
	ZFS_LINUX_TEST_SRC([percpu_ref_count_in_data], [
		#include <linux/percpu-refcount.h>
	],[
		struct percpu_ref_data d;

		atomic_long_set(&d.count, 1L);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PERCPU_REF_COUNT_IN_DATA], [
	AC_MSG_CHECKING([whether is inside percpu_ref.data])
	ZFS_LINUX_TEST_RESULT([percpu_ref_count_in_data], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(ZFS_PERCPU_REF_COUNT_IN_DATA, 1,
		    [count is located in percpu_ref.data])
	],[
		AC_MSG_RESULT(no)
	])
])
AC_DEFUN([ZFS_AC_KERNEL_SRC_PERCPU], [
	ZFS_AC_KERNEL_SRC_PERCPU_REF_COUNT_IN_DATA
])

AC_DEFUN([ZFS_AC_KERNEL_PERCPU], [
	ZFS_AC_KERNEL_PERCPU_REF_COUNT_IN_DATA
])
