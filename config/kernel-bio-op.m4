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

AC_DEFUN([ZFS_AC_KERNEL_REQ_OP_DISCARD], [
	AC_MSG_CHECKING([whether REQ_OP_DISCARD is defined])
	ZFS_LINUX_TEST_RESULT([req_op_discard], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_DISCARD, 1,
		    [REQ_OP_DISCARD is defined])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_REQ_OP_SECURE_ERASE], [
	AC_MSG_CHECKING([whether REQ_OP_SECURE_ERASE is defined])
	ZFS_LINUX_TEST_RESULT([req_op_secure_erase], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REQ_OP_SECURE_ERASE, 1,
		    [REQ_OP_SECURE_ERASE is defined])
	],[
		AC_MSG_RESULT(no)
	])
])


AC_DEFUN([ZFS_AC_KERNEL_REQ_OP_FLUSH], [
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

AC_DEFUN([ZFS_AC_KERNEL_HAVE_BIO_SET_OP_ATTRS], [
	AC_MSG_CHECKING([whether bio_set_op_attrs is available])
	ZFS_LINUX_TEST_RESULT([bio_set_op_attrs], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_OP_ATTRS, 1,
		    [bio_set_op_attrs is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_OPS], [
	ZFS_AC_KERNEL_REQ_OP_DISCARD
	ZFS_AC_KERNEL_REQ_OP_SECURE_ERASE
	ZFS_AC_KERNEL_REQ_OP_FLUSH
	ZFS_AC_KERNEL_BIO_BI_OPF
	ZFS_AC_KERNEL_HAVE_BIO_SET_OP_ATTRS
])
