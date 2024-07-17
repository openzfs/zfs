dnl #
dnl # 5.11 API change
dnl # kmap_atomic() was deprecated in favor of kmap_local_page()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KMAP_LOCAL_PAGE], [
	ZFS_LINUX_TEST_SRC([kmap_local_page], [
		#include <linux/highmem.h>
	],[
		struct page page;
		kmap_local_page(&page);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KMAP_LOCAL_PAGE], [
	AC_MSG_CHECKING([whether kmap_local_page exists])
	ZFS_LINUX_TEST_RESULT([kmap_local_page], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMAP_LOCAL_PAGE, 1,
		    [kernel has kmap_local_page])
	],[
		AC_MSG_RESULT(no)
	])
])
