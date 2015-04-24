dnl #
dnl # Linux 3.2 API Change
dnl # make_request_fn returns void instead of int.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MAKE_REQUEST_FN], [
	AC_MSG_CHECKING([whether make_request_fn() returns int])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>

		int make_request(struct request_queue *q, struct bio *bio)
		{
			return (0);
		}
	],[
		blk_queue_make_request(NULL, &make_request);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(MAKE_REQUEST_FN_RET, int,
		    [make_request_fn() returns int])
		AC_DEFINE(HAVE_MAKE_REQUEST_FN_RET_INT, 1,
		    [Noting that make_request_fn() returns int])
	],[
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether make_request_fn() returns void])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/blkdev.h>

			void make_request(struct request_queue *q, struct bio *bio)
			{
				return;
			}
		],[
			blk_queue_make_request(NULL, &make_request);
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(MAKE_REQUEST_FN_RET, void,
			    [make_request_fn() returns void])
		],[
			AC_MSG_ERROR(no - Please file a bug report at
			    https://github.com/zfsonlinux/zfs/issues/new)
		])
	])
])
