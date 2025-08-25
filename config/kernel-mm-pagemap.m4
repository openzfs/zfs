AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_SIZE], [
	ZFS_LINUX_TEST_SRC([page_size], [
		#include <linux/mm.h>
	],[
		unsigned long s;
		s = page_size(NULL);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_SIZE], [
	AC_MSG_CHECKING([whether page_size() is available])
	ZFS_LINUX_TEST_RESULT([page_size], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MM_PAGE_SIZE, 1, [page_size() is available])
	],[
		AC_MSG_RESULT(no)
	])
])


AC_DEFUN([ZFS_AC_KERNEL_SRC_MM_PAGE_MAPPING], [
	ZFS_LINUX_TEST_SRC([page_mapping], [
		#include <linux/pagemap.h>
	],[
		struct address_space *m;
		m = page_mapping(NULL);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_MM_PAGE_MAPPING], [
	AC_MSG_CHECKING([whether page_mapping() is available])
	ZFS_LINUX_TEST_RESULT([page_mapping], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MM_PAGE_MAPPING, 1, [page_mapping() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
