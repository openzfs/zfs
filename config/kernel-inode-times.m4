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

	dnl #
	dnl # 6.6 API change
	dnl # i_ctime no longer directly accessible, must use
	dnl # inode_get_ctime(ip), inode_set_ctime*(ip) to
	dnl # read/write.
	dnl #
	ZFS_LINUX_TEST_SRC([inode_get_ctime], [
		#include <linux/fs.h>
	],[
		struct inode ip;

		memset(&ip, 0, sizeof(ip));
		inode_get_ctime(&ip);
	])

	ZFS_LINUX_TEST_SRC([inode_set_ctime_to_ts], [
		#include <linux/fs.h>
	],[
		struct inode ip;
		struct timespec64 ts = {0};

		memset(&ip, 0, sizeof(ip));
		inode_set_ctime_to_ts(&ip, ts);
	])

	dnl #
	dnl # 6.7 API change
	dnl # i_atime/i_mtime no longer directly accessible, must use
	dnl # inode_get_mtime(ip), inode_set_mtime*(ip) to
	dnl # read/write.
	dnl #
	ZFS_LINUX_TEST_SRC([inode_get_atime], [
		#include <linux/fs.h>
	],[
		struct inode ip;

		memset(&ip, 0, sizeof(ip));
		inode_get_atime(&ip);
	])
	ZFS_LINUX_TEST_SRC([inode_get_mtime], [
		#include <linux/fs.h>
	],[
		struct inode ip;

		memset(&ip, 0, sizeof(ip));
		inode_get_mtime(&ip);
	])

	ZFS_LINUX_TEST_SRC([inode_set_atime_to_ts], [
		#include <linux/fs.h>
	],[
		struct inode ip;
		struct timespec64 ts = {0};

		memset(&ip, 0, sizeof(ip));
		inode_set_atime_to_ts(&ip, ts);
	])
	ZFS_LINUX_TEST_SRC([inode_set_mtime_to_ts], [
		#include <linux/fs.h>
	],[
		struct inode ip;
		struct timespec64 ts = {0};

		memset(&ip, 0, sizeof(ip));
		inode_set_mtime_to_ts(&ip, ts);
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

	AC_MSG_CHECKING([whether inode_get_ctime() exists])
	ZFS_LINUX_TEST_RESULT([inode_get_ctime], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_GET_CTIME, 1,
		    [inode_get_ctime() exists in linux/fs.h])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether inode_set_ctime_to_ts() exists])
	ZFS_LINUX_TEST_RESULT([inode_set_ctime_to_ts], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_SET_CTIME_TO_TS, 1,
		    [inode_set_ctime_to_ts() exists in linux/fs.h])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether inode_get_atime() exists])
	ZFS_LINUX_TEST_RESULT([inode_get_atime], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_GET_ATIME, 1,
		    [inode_get_atime() exists in linux/fs.h])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether inode_set_atime_to_ts() exists])
	ZFS_LINUX_TEST_RESULT([inode_set_atime_to_ts], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_SET_ATIME_TO_TS, 1,
		    [inode_set_atime_to_ts() exists in linux/fs.h])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether inode_get_mtime() exists])
	ZFS_LINUX_TEST_RESULT([inode_get_mtime], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_GET_MTIME, 1,
		    [inode_get_mtime() exists in linux/fs.h])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether inode_set_mtime_to_ts() exists])
	ZFS_LINUX_TEST_RESULT([inode_set_mtime_to_ts], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_SET_MTIME_TO_TS, 1,
		    [inode_set_mtime_to_ts() exists in linux/fs.h])
	],[
		AC_MSG_RESULT(no)
	])
])
