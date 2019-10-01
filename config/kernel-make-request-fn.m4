dnl #
dnl # Check for make_request_fn interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_MAKE_REQUEST_FN], [
	ZFS_LINUX_TEST_SRC([make_request_fn_int], [
		#include <linux/blkdev.h>
		int make_request(struct request_queue *q,
		    struct bio *bio) { return (0); }
	],[
		blk_queue_make_request(NULL, &make_request);
	])

	ZFS_LINUX_TEST_SRC([make_request_fn_void], [
		#include <linux/blkdev.h>
		void make_request(struct request_queue *q,
		    struct bio *bio) { return; }
	],[
		blk_queue_make_request(NULL, &make_request);
	])

	ZFS_LINUX_TEST_SRC([make_request_fn_blk_qc_t], [
		#include <linux/blkdev.h>
		blk_qc_t make_request(struct request_queue *q,
		    struct bio *bio) { return (BLK_QC_T_NONE); }
	],[
		blk_queue_make_request(NULL, &make_request);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_MAKE_REQUEST_FN], [
	dnl #
	dnl # Legacy API
	dnl # make_request_fn returns int.
	dnl #
	AC_MSG_CHECKING([whether make_request_fn() returns int])
	ZFS_LINUX_TEST_RESULT([make_request_fn_int], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(MAKE_REQUEST_FN_RET, int,
		    [make_request_fn() return type])
		AC_DEFINE(HAVE_MAKE_REQUEST_FN_RET_INT, 1,
		    [Noting that make_request_fn() returns int])
	],[
		AC_MSG_RESULT(no)

		dnl #
		dnl # Linux 3.2 API Change
		dnl # make_request_fn returns void.
		dnl #
		AC_MSG_CHECKING([whether make_request_fn() returns void])
		ZFS_LINUX_TEST_RESULT([make_request_fn_void], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(MAKE_REQUEST_FN_RET, void,
			    [make_request_fn() return type])
			AC_DEFINE(HAVE_MAKE_REQUEST_FN_RET_VOID, 1,
			    [Noting that make_request_fn() returns void])
		],[
			AC_MSG_RESULT(no)

			dnl #
			dnl # Linux 4.4 API Change
			dnl # make_request_fn returns blk_qc_t.
			dnl #
			AC_MSG_CHECKING(
			    [whether make_request_fn() returns blk_qc_t])
			ZFS_LINUX_TEST_RESULT([make_request_fn_blk_qc_t], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(MAKE_REQUEST_FN_RET, blk_qc_t,
				    [make_request_fn() return type])
				AC_DEFINE(HAVE_MAKE_REQUEST_FN_RET_QC, 1,
				    [Noting that make_request_fn() ]
				    [returns blk_qc_t])
			],[
				ZFS_LINUX_TEST_ERROR([make_request_fn])
			])
		])
	])
])
