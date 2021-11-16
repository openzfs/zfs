dnl #
dnl # Linux 5.16 no longer allows directly calling wait_on_page_bit, and
dnl # instead requires you to call folio-specific functions. In this case,
dnl # wait_on_page_bit(pg, PG_writeback) becomes
dnl # folio_wait_bit(pg, PG_writeback)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PAGEMAP_FOLIO_WAIT_BIT], [
	ZFS_LINUX_TEST_SRC([pagemap_has_folio_wait_bit], [
		#include <linux/pagemap.h>
	],[
		static struct folio *f = NULL;

		folio_wait_bit(f, PG_writeback);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PAGEMAP_FOLIO_WAIT_BIT], [
	AC_MSG_CHECKING([folio_wait_bit() exists])
	ZFS_LINUX_TEST_RESULT([pagemap_has_folio_wait_bit], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_PAGEMAP_FOLIO_WAIT_BIT, 1,
			[folio_wait_bit() exists])
	],[
		AC_MSG_RESULT([no])
	])
])
