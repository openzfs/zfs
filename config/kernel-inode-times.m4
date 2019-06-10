dnl #
dnl # 4.18 API change
dnl # i_atime, i_mtime, and i_ctime changed from timespec to timespec64.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_TIMES], [
	ZFS_LINUX_TEST_SRC([inode_times], [
		#include <linux/fs.h>
	],[
		struct inode ip;
		struct timespec ts;

		memset(&ip, 0, sizeof(ip));
		ts = ip.i_mtime;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_INODE_TIMES], [
	AC_MSG_CHECKING([whether inode->i_*time's are timespec64])
	ZFS_LINUX_TEST_RESULT([inode_times], [
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TIMESPEC64_TIMES, 1,
		    [inode->i_*time's are timespec64])
	])
])
