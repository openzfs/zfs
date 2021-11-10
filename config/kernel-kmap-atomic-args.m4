dnl #
dnl # 2.6.37 API change
dnl # kmap_atomic changed from assigning hard-coded named slot to using
dnl # push/pop based dynamical allocation.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KMAP_ATOMIC_ARGS], [
	ZFS_LINUX_TEST_SRC([kmap_atomic], [
		#include <linux/pagemap.h>
	],[
		struct page page;
		kmap_atomic(&page);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KMAP_ATOMIC_ARGS], [
	AC_MSG_CHECKING([whether kmap_atomic wants 1 args])
	ZFS_LINUX_TEST_RESULT([kmap_atomic], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([kmap_atomic()])
	])
])
