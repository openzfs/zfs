dnl #
dnl # 4.13 API change
dnl # Renamed struct wait_queue -> struct wait_queue_entry.
dnl #
dnl # N.B. The type check is performed before all other checks
dnl # since ZFS_AC_KERNEL_SRC_WAIT_QUEUE_HEAD_ENTRY depends on
dnl # HAVE_WAIT_QUEUE_ENTRY_T being set in confdefs.h.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_WAIT_QUEUE_ENTRY_T], [
	AC_MSG_CHECKING([whether wait_queue_entry_t exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/wait.h>
	],[
		wait_queue_entry_t *entry __attribute__ ((unused));
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WAIT_QUEUE_ENTRY_T, 1,
		    [wait_queue_entry_t exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.13 API change
dnl # Renamed wait_queue_head::task_list -> wait_queue_head::head
dnl # Renamed wait_queue_entry::task_list -> wait_queue_entry::entry
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_WAIT_QUEUE_HEAD_ENTRY], [
	ZFS_LINUX_TEST_SRC([wait_queue_head_entry], [
		#include <linux/wait.h>

		#ifdef HAVE_WAIT_QUEUE_ENTRY_T
		typedef wait_queue_head_t	spl_wait_queue_head_t;
		typedef wait_queue_entry_t	spl_wait_queue_entry_t;
		#else
		typedef wait_queue_head_t	spl_wait_queue_head_t;
		typedef wait_queue_t		spl_wait_queue_entry_t;
		#endif
	],[
		spl_wait_queue_head_t wq_head;
		spl_wait_queue_entry_t wq_entry;
		struct list_head *head __attribute__ ((unused));
		struct list_head *entry __attribute__ ((unused));

		head = &wq_head.head;
		entry = &wq_entry.entry;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_WAIT_QUEUE_HEAD_ENTRY], [
	AC_MSG_CHECKING([whether wq_head->head and wq_entry->entry exist])
	ZFS_LINUX_TEST_RESULT([wait_queue_head_entry], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_WAIT_QUEUE_HEAD_ENTRY, 1,
		    [wq_head->head and wq_entry->entry exist])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_WAIT], [
	ZFS_AC_KERNEL_SRC_WAIT_QUEUE_HEAD_ENTRY
])

AC_DEFUN([ZFS_AC_KERNEL_WAIT], [
	ZFS_AC_KERNEL_WAIT_QUEUE_HEAD_ENTRY
])
