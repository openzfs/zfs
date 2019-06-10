dnl #
dnl # 2.6.34 API change
dnl # current->bio_tail and current->bio_list were struct bio pointers prior to
dnl # Linux 2.6.34. They were refactored into a struct bio_list pointer called
dnl # current->bio_list in Linux 2.6.34.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CURRENT_BIO_TAIL], [
	ZFS_LINUX_TEST_SRC([current_bio_tail], [
		#include <linux/sched.h>
	], [
		current->bio_tail = (struct bio **) NULL;
	])

	ZFS_LINUX_TEST_SRC([current_bio_list], [
		#include <linux/sched.h>
	], [
		current->bio_list = (struct bio_list *) NULL;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CURRENT_BIO_TAIL], [
	AC_MSG_CHECKING([whether current->bio_tail exists])
	ZFS_LINUX_TEST_RESULT([current_bio_tail], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CURRENT_BIO_TAIL, 1,
		    [current->bio_tail exists])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether current->bio_list exists])
		ZFS_LINUX_TEST_RESULT([current_bio_list], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_CURRENT_BIO_LIST, 1,
			    [current->bio_list exists])
		],[
			ZFS_LINUX_TEST_ERROR([bio_list])
		])
	])
])
