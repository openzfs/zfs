dnl #
dnl # 2.6.x API change
dnl #
dnl # 3.14 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_RQ_FOR_EACH_SEGMENT], [
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"

	AC_MSG_CHECKING([whether rq_for_each_segment() wants bio_vec *])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct bio_vec *bv;
		struct req_iterator iter;
		struct request *req = NULL;
		rq_for_each_segment(bv, req, iter) { }
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RQ_FOR_EACH_SEGMENT, 1,
		          [rq_for_each_segment() is available])
		AC_DEFINE(HAVE_RQ_FOR_EACH_SEGMENT_BVP, 1,
		          [rq_for_each_segment() wants bio_vec *])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether rq_for_each_segment() wants bio_vec])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		struct bio_vec bv;
		struct req_iterator iter;
		struct request *req = NULL;
		rq_for_each_segment(bv, req, iter) { }
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RQ_FOR_EACH_SEGMENT, 1,
		          [rq_for_each_segment() is available])
		AC_DEFINE(HAVE_RQ_FOR_EACH_SEGMENT_BV, 1,
		          [rq_for_each_segment() wants bio_vec])
	],[
		AC_MSG_RESULT(no)
	])

	EXTRA_KCFLAGS="$tmp_flags"
])
