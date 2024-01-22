dnl # 4.14-rc3 API change
dnl # https://lwn.net/Articles/735887/
dnl #
dnl # Check if timer_list.func get passed a timer_list or an unsigned long
dnl # (older kernels).  Also sanity check the from_timer() and timer_setup()
dnl # macros are available as well, since they will be used in the same newer
dnl # kernels that support the new timer_list.func signature.
dnl #
dnl # Also check for the existence of flags in struct timer_list, they were
dnl # added in 4.1-rc8 via 0eeda71bc30d.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TIMER_SETUP], [
	ZFS_LINUX_TEST_SRC([timer_setup], [
		#include <linux/timer.h>

		struct my_task_timer {
			struct timer_list timer;
			int data;
		};

		static void task_expire(struct timer_list *tl)
		{
			struct my_task_timer *task_timer =
			    from_timer(task_timer, tl, timer);
			task_timer->data = 42;
		}
	],[
		struct my_task_timer task_timer;
		timer_setup(&task_timer.timer, task_expire, 0);
	])

	ZFS_LINUX_TEST_SRC([timer_list_function], [
		#include <linux/timer.h>
		static void task_expire(struct timer_list *tl) {}
	],[
		struct timer_list tl;
		tl.function = task_expire;
	])

	ZFS_LINUX_TEST_SRC([timer_list_flags], [
		#include <linux/timer.h>
	],[
		struct timer_list tl;
		tl.flags = 2;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_TIMER_SETUP], [
	AC_MSG_CHECKING([whether timer_setup() is available])
	ZFS_LINUX_TEST_RESULT([timer_setup], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_TIMER_SETUP, 1,
		    [timer_setup() is available])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether timer function expects timer_list])
	ZFS_LINUX_TEST_RESULT([timer_list_function], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_TIMER_FUNCTION_TIMER_LIST, 1,
		    [timer_list.function gets a timer_list])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether struct timer_list has flags])
	ZFS_LINUX_TEST_RESULT([timer_list_flags], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KERNEL_TIMER_LIST_FLAGS, 1,
		    [struct timer_list has a flags member])
	],[
		AC_MSG_RESULT(no)
	])
])
