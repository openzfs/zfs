dnl #
dnl # cpu_has_feature() may referencing GPL-only cpu_feature_keys on powerpc
dnl #

dnl #
dnl # Checking if cpu_has_feature is exported GPL-only
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CPU_HAS_FEATURE], [
	ZFS_LINUX_TEST_SRC([cpu_has_feature], [
		#include <linux/version.h>
		#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 7, 0)
		#include <asm/cpu_has_feature.h>
		#else
		#include <asm/cputable.h>
		#endif
	], [
		return cpu_has_feature(CPU_FTR_ALTIVEC) ? 0 : 1;
	], [], [ZFS_META_LICENSE])
])
AC_DEFUN([ZFS_AC_KERNEL_CPU_HAS_FEATURE], [
	AC_MSG_CHECKING([whether cpu_has_feature() is GPL-only])
	ZFS_LINUX_TEST_RESULT([cpu_has_feature_license], [
		AC_MSG_RESULT(no)
	], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CPU_HAS_FEATURE_GPL_ONLY, 1,
		    [cpu_has_feature() is GPL-only])
	])
])
