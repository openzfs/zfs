dnl #
dnl # Check for generic io accounting interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_IO_ACCT], [
	ZFS_LINUX_TEST_SRC([generic_acct_3args], [
		#include <linux/bio.h>

		void (*generic_start_io_acct_f)(int, unsigned long,
		    struct hd_struct *) = &generic_start_io_acct;
		void (*generic_end_io_acct_f)(int, struct hd_struct *,
		    unsigned long) = &generic_end_io_acct;
	], [
		generic_start_io_acct(0, 0, NULL);
		generic_end_io_acct(0, NULL, 0);
	])

	ZFS_LINUX_TEST_SRC([generic_acct_4args], [
		#include <linux/bio.h>

		void (*generic_start_io_acct_f)(struct request_queue *, int,
		    unsigned long, struct hd_struct *) = &generic_start_io_acct;
		void (*generic_end_io_acct_f)(struct request_queue *, int,
		    struct hd_struct *, unsigned long) = &generic_end_io_acct;
	], [
		generic_start_io_acct(NULL, 0, 0, NULL);
		generic_end_io_acct(NULL, 0, NULL, 0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_GENERIC_IO_ACCT], [
	dnl #
	dnl # 3.19 API addition
	dnl #
	dnl # torvalds/linux@394ffa50 allows us to increment iostat
	dnl # counters without generic_make_request().
	dnl #
	AC_MSG_CHECKING([whether generic IO accounting wants 3 args])
	ZFS_LINUX_TEST_RESULT_SYMBOL([generic_acct_3args],
	    [generic_start_io_acct], [block/bio.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GENERIC_IO_ACCT_3ARG, 1,
		    [generic_start_io_acct()/generic_end_io_acct() available])
	], [
		AC_MSG_RESULT(no)

		dnl #
		dnl # Linux 4.14 API,
		dnl #
		dnl # generic_start_io_acct/generic_end_io_acct now require
		dnl # request_queue to be provided. No functional changes,
		dnl # but preparation for inflight accounting.
		dnl #
		AC_MSG_CHECKING([whether generic IO accounting wants 4 args])
		ZFS_LINUX_TEST_RESULT_SYMBOL([generic_acct_4args],
		    [generic_start_io_acct], [block/bio.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_GENERIC_IO_ACCT_4ARG, 1,
			    [generic_start_io_acct()/generic_end_io_acct() ]
			    [4 arg available])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
