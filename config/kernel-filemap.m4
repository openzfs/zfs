dnl #
dnl # filemap_range_has_page was not available till 4.13
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FILEMAP], [
	ZFS_LINUX_TEST_SRC([filemap_range_has_page], [
		#include <linux/fs.h>
		#include <linux/pagemap.h>
	],[
		struct address_space *mapping = NULL;
		loff_t lstart = 0;
		loff_t lend = 0;
		bool ret __attribute__ ((unused));

		ret = filemap_range_has_page(mapping, lstart, lend);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FILEMAP], [
	AC_MSG_CHECKING([whether filemap_range_has_page() is available])
	ZFS_LINUX_TEST_RESULT([filemap_range_has_page], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILEMAP_RANGE_HAS_PAGE, 1,
		[filemap_range_has_page() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
