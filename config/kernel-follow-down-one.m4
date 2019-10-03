dnl #
dnl # 2.6.38 API change
dnl # follow_down() renamed follow_down_one().  The original follow_down()
dnl # symbol still exists but will traverse down all the layers.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FOLLOW_DOWN_ONE], [
	ZFS_LINUX_TEST_SRC([follow_down_one], [
		#include <linux/namei.h>
	],[
		struct path *p = NULL;
		follow_down_one(p);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FOLLOW_DOWN_ONE], [
	AC_MSG_CHECKING([whether follow_down_one() is available])
	ZFS_LINUX_TEST_RESULT([follow_down_one], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FOLLOW_DOWN_ONE, 1,
		    [follow_down_one() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
