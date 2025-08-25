dnl #
dnl # 6.2: timer_delete_sync introduced, del_timer_sync deprecated and made
dnl #      into a simple wrapper
dnl # 6.15: del_timer_sync removed
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TIMER_DELETE_SYNC], [
	ZFS_LINUX_TEST_SRC([timer_delete_sync], [
		#include <linux/timer.h>
	],[
		struct timer_list *timer __attribute__((unused)) = NULL;
		timer_delete_sync(timer);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_TIMER_DELETE_SYNC], [
	AC_MSG_CHECKING([whether timer_delete_sync() is available])
	ZFS_LINUX_TEST_RESULT([timer_delete_sync], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TIMER_DELETE_SYNC, 1,
		    [timer_delete_sync is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_TIMER], [
	ZFS_AC_KERNEL_SRC_TIMER_DELETE_SYNC
])

AC_DEFUN([ZFS_AC_KERNEL_TIMER], [
	ZFS_AC_KERNEL_TIMER_DELETE_SYNC
])
