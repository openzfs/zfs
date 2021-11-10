dnl #
dnl # 2.6.35 API change,
dnl # Unused 'struct dentry *' removed from vfs_fsync() prototype.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_FSYNC_2ARGS], [
	ZFS_LINUX_TEST_SRC([vfs_fsync_2args], [
		#include <linux/fs.h>
	],[
		vfs_fsync(NULL, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_FSYNC_2ARGS], [
	AC_MSG_CHECKING([whether vfs_fsync() wants 2 args])
	ZFS_LINUX_TEST_RESULT([vfs_fsync_2args], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([vfs_fsync()])
	])
])
