dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # 5.1 API change adds rq_for_each_bvec(), which iterates multi-page
dnl # bvecs rather than splitting every segment at a page boundary.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_RQ_FOR_EACH_BVEC], [
	ZFS_LINUX_TEST_SRC([rq_for_each_bvec], [
		#include <linux/blkdev.h>
		#include <linux/blk-mq.h>
	],[
		struct request *rq = NULL;
		struct bio_vec bv;
		struct req_iterator iter;

		rq_for_each_bvec(bv, rq, iter) { (void) bv; }
	])
])

AC_DEFUN([ZFS_AC_KERNEL_RQ_FOR_EACH_BVEC], [
	AC_MSG_CHECKING([whether rq_for_each_bvec() exists])
	ZFS_LINUX_TEST_RESULT([rq_for_each_bvec], [
		AC_MSG_RESULT(yes)

		AC_DEFINE([HAVE_RQ_FOR_EACH_BVEC], 1,
		    [rq_for_each_bvec() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
