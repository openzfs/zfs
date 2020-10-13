dnl #
dnl # Linux 5.5 API,
dnl #
dnl # The Linux 5.5 kernel updated percpu_ref_tryget() which is inlined by
dnl # blkg_tryget() to use rcu_read_lock() instead of rcu_read_lock_sched().
dnl # As a side effect the function was converted to GPL-only.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKG_TRYGET], [
	ZFS_LINUX_TEST_SRC([blkg_tryget], [
		#include <linux/blk-cgroup.h>
		#include <linux/bio.h>
		#include <linux/fs.h>
	],[
		struct blkcg_gq blkg __attribute__ ((unused)) = {};
		bool rc __attribute__ ((unused));
		rc = blkg_tryget(&blkg);
	], [], [$ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKG_TRYGET], [
	AC_MSG_CHECKING([whether blkg_tryget() is available])
	ZFS_LINUX_TEST_RESULT([blkg_tryget], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKG_TRYGET, 1, [blkg_tryget() is available])

		AC_MSG_CHECKING([whether blkg_tryget() is GPL-only])
		ZFS_LINUX_TEST_RESULT([blkg_tryget_license], [
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLKG_TRYGET_GPL_ONLY, 1,
			    [blkg_tryget() GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])
])
