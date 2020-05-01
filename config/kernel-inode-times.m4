AC_DEFUN([ZFS_AC_KERNEL_SRC_INODE_TIMES], [

	dnl #
	dnl # 5.6 API change
	dnl # timespec64_trunc() replaced by timestamp_truncate() interface.
	dnl #
	ZFS_LINUX_TEST_SRC([timestamp_truncate], [
		#include <linux/fs.h>
	],[
		struct timespec64 ts;
		struct inode ip;

		memset(&ts, 0, sizeof(ts));
		ts = timestamp_truncate(ts, &ip);
	])

	dnl #
	dnl # 4.18 API change
	dnl # i_atime, i_mtime, and i_ctime changed from timespec to timespec64.
	dnl #
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
	AC_MSG_CHECKING([whether timestamp_truncate() exists])
	ZFS_LINUX_TEST_RESULT([timestamp_truncate], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TIMESTAMP_TRUNCATE, 1,
		    [timestamp_truncate() exists])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether inode->i_*time's are timespec64])
	ZFS_LINUX_TEST_RESULT([inode_times], [
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TIMESPEC64_TIMES, 1,
		    [inode->i_*time's are timespec64])
	])
])
