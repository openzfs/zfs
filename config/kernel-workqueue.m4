dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 5.19 API change
dnl # flush_workqueue() will no longer compile for system/global workqueues,
dnl # __flush_workqueue() introduced with no restrictions.
dnl #
dnl # 6.17 API change
dnl # schedule_delayed_work() now queues to system_percpu_wq, system_wq
dnl # deprecated.
dnl #
dnl # We cannot simply test for system_percpu_wq, as it has been backported
dnl # to LTS kernels where it is _not_ used for the delay queue. However,
dnl # the same commit that introduced it on mainline (a2be943b46b4a) also
dnl # added system_percpu_wq to the flush_workqueue() macro as one of the
dnl # global workqueues to reject at compiletime, and removed system_wq.
dnl # That part however, was _not_ backported, so we can use it to determine
dnl # which waitqueue is used for delay - if __flush_workqueue() accepts it,
dnl # but flush_workqueue() does not, then it is the delay workqueue.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_FLUSH_DELAY_WORKQUEUE], [
	ZFS_LINUX_TEST_SRC([flush_workqueue_system], [
		#include <linux/workqueue.h>
	], [
		flush_workqueue(system_wq);
	])
	ZFS_LINUX_TEST_SRC([flush_workqueue_percpu], [
		#include <linux/workqueue.h>
	], [
		flush_workqueue(system_percpu_wq);
	])
	ZFS_LINUX_TEST_SRC([flush_workqueue_internal_system], [
		#include <linux/workqueue.h>
	], [
		__flush_workqueue(system_wq);
	])
	ZFS_LINUX_TEST_SRC([flush_workqueue_internal_percpu], [
		#include <linux/workqueue.h>
	], [
		__flush_workqueue(system_percpu_wq);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FLUSH_DELAY_WORKQUEUE], [
	AC_MSG_CHECKING([how to flush delay workqueue])
	ZFS_LINUX_TEST_RESULT([flush_workqueue_internal_percpu], [
		dnl system_percpu_wq exists. the flush_workqueue()
		dnl macro will reject the one used for delay
		ZFS_LINUX_TEST_RESULT([flush_workqueue_percpu], [
			AC_MSG_RESULT([internal+system])
			AC_DEFINE(HAVE_FLUSH_DELAY_WORKQUEUE_INTERNAL, 1,
			    [use __flush_workqueue() to flush delay workqueue])
		], [
			AC_MSG_RESULT([internal+percpu])
			AC_DEFINE(HAVE_FLUSH_DELAY_WORKQUEUE_INTERNAL, 1,
			    [use __flush_workqueue() to flush delay workqueue])
			AC_DEFINE(HAVE_FLUSH_DELAY_WORKQUEUE_PERCPU, 1,
			    [use system_delay_wq for delay workqueue])
		])
	], [
		dnl system_percpu_wq does not exist. use whichever did
		dnl not reject it
		ZFS_LINUX_TEST_RESULT([flush_workqueue_internal_system], [
			AC_MSG_RESULT([internal+system])
			AC_DEFINE(HAVE_FLUSH_DELAY_WORKQUEUE_INTERNAL, 1,
			    [use __flush_workqueue() to flush delay workqueue])
		], [
			AC_MSG_RESULT([system])
		])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_WORKQUEUE], [
	ZFS_AC_KERNEL_SRC_FLUSH_DELAY_WORKQUEUE
])
AC_DEFUN([ZFS_AC_KERNEL_WORKQUEUE], [
	ZFS_AC_KERNEL_FLUSH_DELAY_WORKQUEUE
])
