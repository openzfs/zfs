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

AC_DEFUN([ZFS_AC_KERNEL_KTHREAD_DEQUEUE_SIGNAL], [
	dnl #
	dnl # prehistory:
	dnl #     int dequeue_signal(struct task_struct *task, sigset_t *mask,
	dnl #         siginfo_t *info)
	dnl #
	dnl # 4.20: kernel_siginfo_t introduced, replaces siginfo_t
	dnl #     int dequeue_signal(struct task_struct *task, sigset_t *mask,
	dnl           kernel_siginfo_t *info)
	dnl #
	dnl # 5.17: enum pid_type introduced as 4th arg
	dnl #     int dequeue_signal(struct task_struct *task, sigset_t *mask,
	dnl #         kernel_siginfo_t *info, enum pid_type *type)
	dnl #
	dnl # 6.12: first arg struct_task* removed
	dnl #     int dequeue_signal(sigset_t *mask, kernel_siginfo_t *info,
	dnl #         enum pid_type *type)
	dnl #
	AC_MSG_CHECKING([whether dequeue_signal() takes 4 arguments])
	ZFS_LINUX_TEST_RESULT([kthread_dequeue_signal_4arg], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DEQUEUE_SIGNAL_4ARG, 1,
		    [dequeue_signal() takes 4 arguments])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether 3-arg dequeue_signal() takes a type argument])
		ZFS_LINUX_TEST_RESULT([kthread_dequeue_signal_3arg_type], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_DEQUEUE_SIGNAL_3ARG_TYPE, 1,
			    [3-arg dequeue_signal() takes a type argument])
		], [
			AC_MSG_RESULT(no)
		])
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

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTHREAD_DEQUEUE_SIGNAL], [
	ZFS_LINUX_TEST_SRC([kthread_dequeue_signal_4arg], [
		#include <linux/sched/signal.h>
	], [
		struct task_struct *task = NULL;
		sigset_t *mask = NULL;
		kernel_siginfo_t *info = NULL;
		enum pid_type *type = NULL;
		int error __attribute__ ((unused));

		error = dequeue_signal(task, mask, info, type);
	])

	ZFS_LINUX_TEST_SRC([kthread_dequeue_signal_3arg_type], [
		#include <linux/sched/signal.h>
	], [
		sigset_t *mask = NULL;
		kernel_siginfo_t *info = NULL;
		enum pid_type *type = NULL;
		int error __attribute__ ((unused));

		error = dequeue_signal(mask, info, type);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KTHREAD], [
	ZFS_AC_KERNEL_KTHREAD_COMPLETE_AND_EXIT
	ZFS_AC_KERNEL_KTHREAD_DEQUEUE_SIGNAL
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KTHREAD], [
	ZFS_AC_KERNEL_SRC_KTHREAD_COMPLETE_AND_EXIT
	ZFS_AC_KERNEL_SRC_KTHREAD_DEQUEUE_SIGNAL
])
