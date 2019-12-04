dnl #
dnl # 3.8 API change,
dnl # User namespaces, use kuid_t in place of uid_t where available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KUIDGID_T], [
	ZFS_LINUX_TEST_SRC([kuidgid_t], [
		#include <linux/uidgid.h>
	], [
		kuid_t userid __attribute__ ((unused)) = KUIDT_INIT(0);
		kgid_t groupid __attribute__ ((unused)) = KGIDT_INIT(0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KUIDGID_T], [
	AC_MSG_CHECKING([whether kuid_t/kgid_t is available])
	ZFS_LINUX_TEST_RESULT([kuidgid_t], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([kuid_t/kgid_t])
	])
])
