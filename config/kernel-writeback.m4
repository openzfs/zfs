AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITEPAGE_T], [
	dnl #
	dnl # 6.3 API change
	dnl # The writepage_t function type now has its first argument as
	dnl # struct folio* instead of struct page*
	dnl #
	ZFS_LINUX_TEST_SRC([writepage_t_folio], [
		#include <linux/writeback.h>
		static int putpage(struct folio *folio,
		    struct writeback_control *wbc, void *data)
		{ return 0; }
		writepage_t func = putpage;
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_WRITEPAGE_T], [
	AC_MSG_CHECKING([whether int (*writepage_t)() takes struct folio*])
	ZFS_LINUX_TEST_RESULT([writepage_t_folio], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WRITEPAGE_T_FOLIO, 1,
		   [int (*writepage_t)() takes struct folio*])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITE_CACHE_PAGES], [
	dnl #
	dnl # 6.18 API change
	dnl # write_cache_pages() has been removed.
	dnl #
	ZFS_LINUX_TEST_SRC([write_cache_pages], [
		#include <linux/writeback.h>
	], [
		(void) write_cache_pages(NULL, NULL, NULL, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_WRITE_CACHE_PAGES], [
	AC_MSG_CHECKING([whether write_cache_pages() is available])
	ZFS_LINUX_TEST_RESULT([write_cache_pages], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WRITE_CACHE_PAGES, 1,
		    [write_cache_pages() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_WRITEBACK], [
	ZFS_AC_KERNEL_SRC_WRITEPAGE_T
	ZFS_AC_KERNEL_SRC_WRITE_CACHE_PAGES
])

AC_DEFUN([ZFS_AC_KERNEL_WRITEBACK], [
	ZFS_AC_KERNEL_WRITEPAGE_T
	ZFS_AC_KERNEL_WRITE_CACHE_PAGES
])
