dnl #
dnl # Starting from Linux 5.13, flush_dcache_page() becomes an inline
dnl # function and may indirectly referencing GPL-only symbols:
dnl # on powerpc: cpu_feature_keys
dnl # on riscv: PageHuge (added from 6.2)
dnl #

dnl #
dnl # Checking if flush_dcache_page is exported GPL-only
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FLUSH_DCACHE_PAGE], [
	ZFS_LINUX_TEST_SRC([flush_dcache_page], [
		#include <asm/cacheflush.h>
	], [
		flush_dcache_page(0);
	], [], [ZFS_META_LICENSE])
])
AC_DEFUN([ZFS_AC_KERNEL_FLUSH_DCACHE_PAGE], [
	AC_MSG_CHECKING([whether flush_dcache_page() is GPL-only])
	ZFS_LINUX_TEST_RESULT([flush_dcache_page_license], [
		AC_MSG_RESULT(no)
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FLUSH_DCACHE_PAGE_GPL_ONLY, 1,
		    [flush_dcache_page() is GPL-only])
	])
])
