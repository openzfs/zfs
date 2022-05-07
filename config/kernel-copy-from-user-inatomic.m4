dnl #
dnl # On certain architectures `__copy_from_user_inatomic`
dnl # is a GPL exported variable and cannot be used by OpenZFS.
dnl #

dnl #
dnl # Checking if `__copy_from_user_inatomic` is available.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC___COPY_FROM_USER_INATOMIC], [
	ZFS_LINUX_TEST_SRC([__copy_from_user_inatomic], [
		#include <linux/uaccess.h>
	], [
		int result __attribute__ ((unused)) = __copy_from_user_inatomic(NULL, NULL, 0);
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL___COPY_FROM_USER_INATOMIC], [
	AC_MSG_CHECKING([whether __copy_from_user_inatomic is available])
	ZFS_LINUX_TEST_RESULT([__copy_from_user_inatomic_license], [
		AC_MSG_RESULT(yes)
	], [
		AC_MSG_RESULT(no)
		AC_MSG_ERROR([
	*** The `__copy_from_user_inatomic()` Linux kernel function license
	*** (GPL only) is incompatible with the OpenZFS license
	*** ($ZFS_META_LICENSE). OpenZFS cannot be compiled due to
	*** legal and compliance reasons.
		])
	])
])
