dnl #
dnl # Linux 5.0: access_ok() drops 'type' parameter:
dnl #
dnl # - access_ok(type, addr, size)
dnl # + access_ok(addr, size)
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_ACCESS_OK_TYPE], [
	ZFS_LINUX_TEST_SRC([access_ok_type], [
		#include <linux/uaccess.h>
	],[
		const void __user __attribute__((unused)) *addr =
		    (void *) 0xdeadbeef;
		unsigned long __attribute__((unused)) size = 1;
		int error __attribute__((unused)) = access_ok(0, addr, size);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_ACCESS_OK_TYPE], [
	AC_MSG_CHECKING([whether access_ok() has 'type' parameter])
	ZFS_LINUX_TEST_RESULT([access_ok_type], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ACCESS_OK_TYPE, 1,
		    [kernel has access_ok with 'type' parameter])
	],[
		AC_MSG_RESULT(no)
	])
])
