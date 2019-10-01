dnl #
dnl # 2.6.36 API, exported elevator_change() symbol
dnl # 4.12 API, removed elevator_change() symbol
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_ELEVATOR_CHANGE], [
	ZFS_LINUX_TEST_SRC([elevator_change], [
		#include <linux/blkdev.h>
		#include <linux/elevator.h>
	],[
		struct request_queue *q = NULL;
		char *elevator = NULL;
		int error __attribute__ ((unused)) =
		    elevator_change(q, elevator);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_ELEVATOR_CHANGE], [
	AC_MSG_CHECKING([whether elevator_change() is available])
	ZFS_LINUX_TEST_RESULT([elevator_change], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ELEVATOR_CHANGE, 1,
		    [elevator_change() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
