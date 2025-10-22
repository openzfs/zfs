dnl #
dnl # Linux 6.16 removed readahead_page
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PAGEMAP_READAHEAD_PAGE], [
	ZFS_LINUX_TEST_SRC([pagemap_has_readahead_page], [
		#include <linux/pagemap.h>
	], [
		struct page *p __attribute__ ((unused)) = NULL;
		struct readahead_control *ractl __attribute__ ((unused)) = NULL;
		p = readahead_page(ractl);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PAGEMAP_READAHEAD_PAGE], [
	AC_MSG_CHECKING([whether readahead_page() exists])
	ZFS_LINUX_TEST_RESULT([pagemap_has_readahead_page], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_PAGEMAP_READAHEAD_PAGE, 1,
			[readahead_page() exists])
	],[
		AC_MSG_RESULT([no])
	])
])
