dnl #
dnl # 6.18: some architectures and config option causes the kasan_ inline
dnl #       functions to reference the GPL-only symbol 'kasan_flag_enabled',
dnl #       breaking the build. Detect this and work
dnl #       around it.
AC_DEFUN([ZFS_AC_KERNEL_SRC_KASAN_ENABLED], [
	ZFS_LINUX_TEST_SRC([kasan_enabled], [
		#include <linux/kasan.h>
	], [
		kasan_enabled();
	], [], [ZFS_META_LICENSE])
])
AC_DEFUN([ZFS_AC_KERNEL_KASAN_ENABLED], [
	AC_MSG_CHECKING([whether kasan_enabled() is GPL-only])
	ZFS_LINUX_TEST_RESULT([kasan_enabled_license], [
		AC_MSG_RESULT(no)
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KASAN_ENABLED_GPL_ONLY, 1,
		    [kasan_enabled() is GPL-only])
	])
])

