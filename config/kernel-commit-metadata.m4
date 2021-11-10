dnl #
dnl # 2.6.33 API change
dnl # Added eops->commit_metadata() callback to allow the underlying
dnl # filesystem to determine the most efficient way to commit the inode.
dnl # Prior to this the nfs server would issue an explicit fsync().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_COMMIT_METADATA], [
	ZFS_LINUX_TEST_SRC([export_operations_commit_metadata], [
		#include <linux/exportfs.h>
		int commit_metadata(struct inode *inode) { return 0; }
		static struct export_operations eops __attribute__ ((unused))={
			.commit_metadata = commit_metadata,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_COMMIT_METADATA], [
	AC_MSG_CHECKING([whether eops->commit_metadata() exists])
	ZFS_LINUX_TEST_RESULT([export_operations_commit_metadata], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([eops->commit_metadata()])
	])
])
