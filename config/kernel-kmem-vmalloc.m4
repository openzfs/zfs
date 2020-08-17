dnl #
dnl # 5.8 API,
dnl # __vmalloc PAGE_KERNEL removal
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VMALLOC_PAGE_KERNEL], [
	ZFS_LINUX_TEST_SRC([__vmalloc], [
		#include <linux/mm.h>
		#include <linux/vmalloc.h>
	],[
		void *p __attribute__ ((unused));

		p = __vmalloc(0, GFP_KERNEL, PAGE_KERNEL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VMALLOC_PAGE_KERNEL], [
	AC_MSG_CHECKING([whether __vmalloc(ptr, flags, pageflags) is available])
	ZFS_LINUX_TEST_RESULT([__vmalloc], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VMALLOC_PAGE_KERNEL, 1, [__vmalloc page flags exists])
	],[
		AC_MSG_RESULT(no)
	])
])
