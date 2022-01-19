AC_DEFUN([ZFS_AC_KERNEL_KTHREAD_COMPLETE_AND_EXIT], [
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

AC_DEFUN([ZFS_AC_KERNEL_KTHREAD_DEQUEUE_SIGNAL_4ARG], [
	dnl #
	dnl # 5.17 API: enum pid_type * as new 4th dequeue_signal() argument,
	dnl # 5768d8906bc23d512b1a736c1e198aa833a6daa4 ("signal: Requeue signals in the appropriate queue")
	dnl #
	dnl # int dequeue_signal(struct task_struct *task, sigset_t *mask, kernel_siginfo_t *info);
	dnl # int dequeue_signal(struct task_struct *task, sigset_t *mask, kernel_siginfo_t *info, enum pid_type *type);
	dnl #
	AC_MSG_CHECKING([whether dequeue_signal() takes 4 arguments])
	ZFS_LINUX_TEST_RESULT([kthread_dequeue_signal], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEQUEUE_SIGNAL_4ARG, 1, [dequeue_signal() takes 4 arguments])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTHREAD_COMPLETE_AND_EXIT], [
	ZFS_LINUX_TEST_SRC([kthread_complete_and_exit], [
		#include <linux/kthread.h>
	], [
		struct completion *completion = NULL;
		long code = 0;

		kthread_complete_and_exit(completion, code);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTHREAD_DEQUEUE_SIGNAL_4ARG], [
	ZFS_LINUX_TEST_SRC([kthread_dequeue_signal], [
		#include <linux/sched/signal.h>
	], [
		struct task_struct *task = NULL;
		sigset_t *mask = NULL;
		kernel_siginfo_t *info = NULL;
		enum pid_type *type = NULL;
		int error __attribute__ ((unused));

		error = dequeue_signal(task, mask, info, type);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KTHREAD], [
	ZFS_AC_KERNEL_KTHREAD_COMPLETE_AND_EXIT
	ZFS_AC_KERNEL_KTHREAD_DEQUEUE_SIGNAL_4ARG
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTHREAD], [
	ZFS_AC_KERNEL_SRC_KTHREAD_COMPLETE_AND_EXIT
	ZFS_AC_KERNEL_SRC_KTHREAD_DEQUEUE_SIGNAL_4ARG
])
