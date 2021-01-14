dnl #
dnl # Check for make_request_fn interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_MAKE_REQUEST_FN], [
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

	ZFS_LINUX_TEST_SRC([blk_alloc_queue_request_fn], [
		#include <linux/blkdev.h>
		blk_qc_t make_request(struct request_queue *q,
		    struct bio *bio) { return (BLK_QC_T_NONE); }
	],[
		struct request_queue *q __attribute__ ((unused));
		q = blk_alloc_queue(make_request, NUMA_NO_NODE);
	])

	ZFS_LINUX_TEST_SRC([blk_alloc_queue_request_fn_rh], [
		#include <linux/blkdev.h>
		blk_qc_t make_request(struct request_queue *q,
		    struct bio *bio) { return (BLK_QC_T_NONE); }
	],[
		struct request_queue *q __attribute__ ((unused));
		q = blk_alloc_queue_rh(make_request, NUMA_NO_NODE);
	])

	ZFS_LINUX_TEST_SRC([block_device_operations_submit_bio], [
		#include <linux/blkdev.h>
	],[
		struct block_device_operations o;
		o.submit_bio = NULL;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_MAKE_REQUEST_FN], [
	dnl # Checked as part of the blk_alloc_queue_request_fn test
	dnl #
	dnl # Linux 5.9 API Change
	dnl # make_request_fn was moved into block_device_operations->submit_bio
	dnl #
	AC_MSG_CHECKING([whether submit_bio is member of struct block_device_operations])
	ZFS_LINUX_TEST_RESULT([block_device_operations_submit_bio], [
		AC_MSG_RESULT(yes)

		AC_DEFINE(HAVE_SUBMIT_BIO_IN_BLOCK_DEVICE_OPERATIONS, 1,
		    [submit_bio is member of struct block_device_operations])
	],[
		AC_MSG_RESULT(no)

		dnl # Checked as part of the blk_alloc_queue_request_fn test
		dnl #
		dnl # Linux 5.7 API Change
		dnl # blk_alloc_queue() expects request function.
		dnl #
		AC_MSG_CHECKING([whether blk_alloc_queue() expects request function])
		ZFS_LINUX_TEST_RESULT([blk_alloc_queue_request_fn], [
			AC_MSG_RESULT(yes)

			dnl # This is currently always the case.
			AC_MSG_CHECKING([whether make_request_fn() returns blk_qc_t])
			AC_MSG_RESULT(yes)

			AC_DEFINE(HAVE_BLK_ALLOC_QUEUE_REQUEST_FN, 1,
			    [blk_alloc_queue() expects request function])
			AC_DEFINE(MAKE_REQUEST_FN_RET, blk_qc_t,
			    [make_request_fn() return type])
			AC_DEFINE(HAVE_MAKE_REQUEST_FN_RET_QC, 1,
			    [Noting that make_request_fn() returns blk_qc_t])
		],[
			dnl #
			dnl # CentOS Stream 4.18.0-257 API Change
			dnl # The Linux 5.7 blk_alloc_queue() change was back-
			dnl # ported and the symbol renamed blk_alloc_queue_rh().
			dnl # As of this kernel version they're not providing
			dnl # any compatibility code in the kernel for this.
			dnl #
			ZFS_LINUX_TEST_RESULT([blk_alloc_queue_request_fn_rh], [
				AC_MSG_RESULT(yes)

				dnl # This is currently always the case.
				AC_MSG_CHECKING([whether make_request_fn_rh() returns blk_qc_t])
				AC_MSG_RESULT(yes)

				AC_DEFINE(HAVE_BLK_ALLOC_QUEUE_REQUEST_FN_RH, 1,
				    [blk_alloc_queue_rh() expects request function])
				AC_DEFINE(MAKE_REQUEST_FN_RET, blk_qc_t,
				    [make_request_fn() return type])
				AC_DEFINE(HAVE_MAKE_REQUEST_FN_RET_QC, 1,
				    [Noting that make_request_fn() returns blk_qc_t])
			],[
				AC_MSG_RESULT(no)

				dnl #
				dnl # Linux 3.2 API Change
				dnl # make_request_fn returns void.
				dnl #
				AC_MSG_CHECKING(
				    [whether make_request_fn() returns void])
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
	])
])
