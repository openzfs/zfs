dnl #
dnl # 2.6.36 API change,
dnl # REQ_FAILFAST_{DEV|TRANSPORT|DRIVER}
dnl # REQ_DISCARD
dnl # REQ_FLUSH
dnl #
dnl # 4.8 - 4.9 API,
dnl # REQ_FLUSH was renamed to REQ_PREFLUSH
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_REQ], [
	ZFS_LINUX_TEST_SRC([req_failfast_mask], [
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = REQ_FAILFAST_MASK;
	])

	ZFS_LINUX_TEST_SRC([req_discard], [
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = REQ_DISCARD;
	])

	ZFS_LINUX_TEST_SRC([req_flush], [
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = REQ_FLUSH;
	])

	ZFS_LINUX_TEST_SRC([req_preflush], [
		#include <linux/bio.h>
	],[
		int flags __attribute__ ((unused));
		flags = REQ_PREFLUSH;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_FAILFAST_MASK], [
	AC_MSG_CHECKING([whether REQ_FAILFAST_MASK is defined])
	ZFS_LINUX_TEST_RESULT([req_failfast_mask], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([REQ_FAILFAST_MASK])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_DISCARD], [
	AC_MSG_CHECKING([whether REQ_DISCARD is defined])
	ZFS_LINUX_TEST_RESULT([req_discard], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_DISCARD, 1, [REQ_DISCARD is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_FLUSH], [
	AC_MSG_CHECKING([whether REQ_FLUSH is defined])
	ZFS_LINUX_TEST_RESULT([req_flush], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_FLUSH, 1, [REQ_FLUSH is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_PREFLUSH], [
	AC_MSG_CHECKING([whether REQ_PREFLUSH is defined])
	ZFS_LINUX_TEST_RESULT([req_preflush], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_PREFLUSH, 1, [REQ_PREFLUSH is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 4.8 API,
dnl #
dnl # The bio_op() helper was introduced as a replacement for explicitly
dnl # checking the bio->bi_rw flags.  The following checks are used to
dnl # detect if a specific operation is supported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_OPS], [
	ZFS_LINUX_TEST_SRC([req_op_discard], [
		#include <linux/blk_types.h>
	],[
		int op __attribute__ ((unused)) = REQ_OP_DISCARD;
	])

	ZFS_LINUX_TEST_SRC([req_op_secure_erase], [
		#include <linux/blk_types.h>
	],[
		int op __attribute__ ((unused)) = REQ_OP_SECURE_ERASE;
	])

	ZFS_LINUX_TEST_SRC([req_op_flush], [
		#include <linux/blk_types.h>
	],[
		int op __attribute__ ((unused)) = REQ_OP_FLUSH;
	])

	ZFS_LINUX_TEST_SRC([bio_bi_opf], [
		#include <linux/bio.h>
	],[
		struct bio bio __attribute__ ((unused));
		bio.bi_opf = 0;
	])

	ZFS_LINUX_TEST_SRC([bio_set_op_attrs], [
		#include <linux/bio.h>
	],[
		struct bio *bio __attribute__ ((unused)) = NULL;
		bio_set_op_attrs(bio, 0, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_OP_DISCARD], [
	AC_MSG_CHECKING([whether REQ_OP_DISCARD is defined])
	ZFS_LINUX_TEST_RESULT([req_op_discard], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_DISCARD, 1, [REQ_OP_DISCARD is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_OP_SECURE_ERASE], [
	AC_MSG_CHECKING([whether REQ_OP_SECURE_ERASE is defined])
	ZFS_LINUX_TEST_RESULT([req_op_secure_erase], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_SECURE_ERASE, 1,
		    [REQ_OP_SECURE_ERASE is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_REQ_OP_FLUSH], [
	AC_MSG_CHECKING([whether REQ_OP_FLUSH is defined])
	ZFS_LINUX_TEST_RESULT([req_op_flush], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_FLUSH, 1, [REQ_OP_FLUSH is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_BI_OPF], [
	AC_MSG_CHECKING([whether bio->bi_opf is defined])
	ZFS_LINUX_TEST_RESULT([bio_bi_opf], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BI_OPF, 1, [bio->bi_opf is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_OP_ATTRS], [
	AC_MSG_CHECKING([whether bio_set_op_attrs is available])
	ZFS_LINUX_TEST_RESULT([bio_set_op_attrs], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_OP_ATTRS, 1,
		    [bio_set_op_attrs is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 4.14 API,
dnl #
dnl # The bio_set_dev() helper macro was introduced as part of the transition
dnl # to have struct gendisk in struct bio.
dnl #
dnl # Linux 5.0 API,
dnl #
dnl # The bio_set_dev() helper macro was updated to internally depend on
dnl # bio_associate_blkg() symbol which is exported GPL-only.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_SET_DEV], [
	ZFS_LINUX_TEST_SRC([bio_set_dev], [
		#include <linux/bio.h>
		#include <linux/fs.h>
	],[
		struct block_device *bdev = NULL;
		struct bio *bio = NULL;
		bio_set_dev(bio, bdev);
	], [], [$ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_DEV], [
	AC_MSG_CHECKING([whether bio_set_dev() is available])
	ZFS_LINUX_TEST_RESULT([bio_set_dev], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_DEV, 1, [bio_set_dev() is available])

		AC_MSG_CHECKING([whether bio_set_dev() is GPL-only])
		ZFS_LINUX_TEST_RESULT([bio_set_dev_license], [
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BIO_SET_DEV_GPL_ONLY, 1,
			    [bio_set_dev() GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.3 API change
dnl # Error argument dropped from bio_endio in favor of newly introduced
dnl # bio->bi_error. This also replaces bio->bi_flags value BIO_UPTODATE.
dnl # Introduced by torvalds/linux@4246a0b63bd8f56a1469b12eafeb875b1041a451
dnl # ("block: add a bi_error field to struct bio").
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_END_IO_T_ARGS], [
	ZFS_LINUX_TEST_SRC([bio_end_io_t_args], [
		#include <linux/bio.h>
		void wanted_end_io(struct bio *bio) { return; }
		bio_end_io_t *end_io __attribute__ ((unused)) = wanted_end_io;
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_END_IO_T_ARGS], [
	AC_MSG_CHECKING([whether bio_end_io_t wants 1 arg])
	ZFS_LINUX_TEST_RESULT([bio_end_io_t_args], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_BIO_END_IO_T, 1,
		    [bio_end_io_t wants 1 arg])
	], [
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.13 API change
dnl # The bio->bi_error field was replaced with bio->bi_status which is an
dnl # enum which describes all possible error types.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_BI_STATUS], [
	ZFS_LINUX_TEST_SRC([bio_bi_status], [
		#include <linux/bio.h>
	], [
		struct bio bio __attribute__ ((unused));
		blk_status_t status __attribute__ ((unused)) = BLK_STS_OK;
		bio.bi_status = status;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_BI_STATUS], [
	AC_MSG_CHECKING([whether bio->bi_status exists])
	ZFS_LINUX_TEST_RESULT([bio_bi_status], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BI_STATUS, 1, [bio->bi_status exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.14 API change,
dnl # Immutable biovecs. A number of fields of struct bio are moved to
dnl # struct bvec_iter.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_BVEC_ITER], [
	ZFS_LINUX_TEST_SRC([bio_bvec_iter], [
		#include <linux/bio.h>
	],[
		struct bio bio;
		bio.bi_iter.bi_sector = 0;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_BVEC_ITER], [
	AC_MSG_CHECKING([whether bio has bi_iter])
	ZFS_LINUX_TEST_RESULT([bio_bvec_iter], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BVEC_ITER, 1, [bio has bi_iter])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.8 API change
dnl # The rw argument has been removed from submit_bio/submit_bio_wait.
dnl # Callers are now expected to set bio->bi_rw instead of passing it in.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_SUBMIT_BIO], [
	ZFS_LINUX_TEST_SRC([submit_bio], [
		#include <linux/bio.h>
	],[
		blk_qc_t blk_qc;
		struct bio *bio = NULL;
		blk_qc = submit_bio(bio);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_SUBMIT_BIO], [
	AC_MSG_CHECKING([whether submit_bio() wants 1 arg])
	ZFS_LINUX_TEST_RESULT([submit_bio], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_1ARG_SUBMIT_BIO, 1, [submit_bio() wants 1 arg])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.34 API change
dnl # current->bio_list
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_CURRENT_BIO_LIST], [
	ZFS_LINUX_TEST_SRC([current_bio_list], [
		#include <linux/sched.h>
	], [
		current->bio_list = (struct bio_list *) NULL;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_CURRENT_BIO_LIST], [
	AC_MSG_CHECKING([whether current->bio_list exists])
	ZFS_LINUX_TEST_RESULT([current_bio_list], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([bio_list])
	])
])

dnl #
dnl # Linux 5.5 API,
dnl #
dnl # The Linux 5.5 kernel updated percpu_ref_tryget() which is inlined by
dnl # blkg_tryget() to use rcu_read_lock() instead of rcu_read_lock_sched().
dnl # As a side effect the function was converted to GPL-only.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLKG_TRYGET], [
	ZFS_LINUX_TEST_SRC([blkg_tryget], [
		#include <linux/blk-cgroup.h>
		#include <linux/bio.h>
		#include <linux/fs.h>
	],[
		struct blkcg_gq blkg __attribute__ ((unused)) = {};
		bool rc __attribute__ ((unused));
		rc = blkg_tryget(&blkg);
	], [], [$ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_BLKG_TRYGET], [
	AC_MSG_CHECKING([whether blkg_tryget() is available])
	ZFS_LINUX_TEST_RESULT([blkg_tryget], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BLKG_TRYGET, 1, [blkg_tryget() is available])

		AC_MSG_CHECKING([whether blkg_tryget() is GPL-only])
		ZFS_LINUX_TEST_RESULT([blkg_tryget_license], [
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BLKG_TRYGET_GPL_ONLY, 1,
			    [blkg_tryget() GPL-only])
		])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO], [
	ZFS_AC_KERNEL_SRC_REQ
	ZFS_AC_KERNEL_SRC_BIO_OPS
	ZFS_AC_KERNEL_SRC_BIO_SET_DEV
	ZFS_AC_KERNEL_SRC_BIO_END_IO_T_ARGS
	ZFS_AC_KERNEL_SRC_BIO_BI_STATUS
	ZFS_AC_KERNEL_SRC_BIO_BVEC_ITER
	ZFS_AC_KERNEL_SRC_BIO_SUBMIT_BIO
	ZFS_AC_KERNEL_SRC_BIO_CURRENT_BIO_LIST
	ZFS_AC_KERNEL_SRC_BLKG_TRYGET
])

AC_DEFUN([ZFS_AC_KERNEL_BIO], [
	ZFS_AC_KERNEL_BIO_REQ_FAILFAST_MASK
	ZFS_AC_KERNEL_BIO_REQ_DISCARD
	ZFS_AC_KERNEL_BIO_REQ_FLUSH
	ZFS_AC_KERNEL_BIO_REQ_PREFLUSH

	ZFS_AC_KERNEL_BIO_REQ_OP_DISCARD
	ZFS_AC_KERNEL_BIO_REQ_OP_SECURE_ERASE
	ZFS_AC_KERNEL_BIO_REQ_OP_FLUSH
	ZFS_AC_KERNEL_BIO_BI_OPF
	ZFS_AC_KERNEL_BIO_SET_OP_ATTRS

	ZFS_AC_KERNEL_BIO_SET_DEV
	ZFS_AC_KERNEL_BIO_END_IO_T_ARGS
	ZFS_AC_KERNEL_BIO_BI_STATUS
	ZFS_AC_KERNEL_BIO_BVEC_ITER
	ZFS_AC_KERNEL_BIO_SUBMIT_BIO
	ZFS_AC_KERNEL_BIO_CURRENT_BIO_LIST
	ZFS_AC_KERNEL_BLKG_TRYGET
])
