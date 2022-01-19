AC_DEFUN([ZFS_AC_KERNEL_KTHREAD], [
	dnl #
	dnl # 5.17 API,
	dnl # cead18552660702a4a46f58e65188fe5f36e9dfe ("exit: Rename complete_and_exit to kthread_complete_and_exit")
	dnl #
	dnl # Also moves the definition from include/linux/kernel.h to include/linux/kthread.h
	dnl #
	AC_MSG_CHECKING([whether kthread_complete_and_exit() is available])
	ZFS_LINUX_TEST_RESULT([kthread_complete_and_exit], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(SPL_KTHREAD_COMPLETE_AND_EXIT, kthread_complete_and_exit, [kthread_complete_and_exit() available])
	], [
		AC_MSG_RESULT(no)
		AC_DEFINE(SPL_KTHREAD_COMPLETE_AND_EXIT, complete_and_exit, [using complete_and_exit() instead])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTHREAD], [
	ZFS_LINUX_TEST_SRC([kthread_complete_and_exit], [
		#include <linux/kthread.h>
	], [
		struct completion *completion = NULL;
		long code = 0;

		kthread_complete_and_exit(completion, code);
	])
])
