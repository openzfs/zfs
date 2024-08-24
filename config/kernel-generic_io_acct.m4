dnl #
dnl # Check for generic io accounting interface.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_GENERIC_IO_ACCT], [
	ZFS_LINUX_TEST_SRC([bdev_io_acct_63], [
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		struct bio *bio = NULL;
		unsigned long passed_time = 0;
		unsigned long start_time;

		start_time = bdev_start_io_acct(bdev, bio_op(bio),
		    passed_time);
		bdev_end_io_acct(bdev, bio_op(bio), bio_sectors(bio), start_time);
	])

	ZFS_LINUX_TEST_SRC([bdev_io_acct_old], [
		#include <linux/blkdev.h>
	], [
		struct block_device *bdev = NULL;
		struct bio *bio = NULL;
		unsigned long passed_time = 0;
		unsigned long start_time;

		start_time = bdev_start_io_acct(bdev, bio_sectors(bio),
		    bio_op(bio), passed_time);
		bdev_end_io_acct(bdev, bio_op(bio), start_time);
	])

	ZFS_LINUX_TEST_SRC([disk_io_acct], [
		#include <linux/blkdev.h>
	], [
		struct gendisk *disk = NULL;
		struct bio *bio = NULL;
		unsigned long start_time;

		start_time = disk_start_io_acct(disk, bio_sectors(bio), bio_op(bio));
		disk_end_io_acct(disk, bio_op(bio), start_time);
	])

	ZFS_LINUX_TEST_SRC([bio_io_acct], [
		#include <linux/blkdev.h>
	], [
		struct bio *bio = NULL;
		unsigned long start_time;

		start_time = bio_start_io_acct(bio);
		bio_end_io_acct(bio, start_time);
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
	dnl # Linux 6.3, and then backports thereof, changed
	dnl # the signatures on bdev_start_io_acct/bdev_end_io_acct
	dnl #
	AC_MSG_CHECKING([whether 6.3+ bdev_*_io_acct() are available])
	ZFS_LINUX_TEST_RESULT([bdev_io_acct_63], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_IO_ACCT_63, 1, [bdev_*_io_acct() available])
	], [
		AC_MSG_RESULT(no)

		dnl #
		dnl # 5.19 API,
		dnl #
		dnl # disk_start_io_acct() and disk_end_io_acct() have been replaced by
		dnl # bdev_start_io_acct() and bdev_end_io_acct().
		dnl #
		AC_MSG_CHECKING([whether pre-6.3 bdev_*_io_acct() are available])
		ZFS_LINUX_TEST_RESULT([bdev_io_acct_old], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_BDEV_IO_ACCT_OLD, 1, [bdev_*_io_acct() available])
		], [
			AC_MSG_RESULT(no)
			dnl #
			dnl # 5.12 API,
			dnl #
			dnl # bio_start_io_acct() and bio_end_io_acct() became GPL-exported
			dnl # so use disk_start_io_acct() and disk_end_io_acct() instead
			dnl #
			AC_MSG_CHECKING([whether generic disk_*_io_acct() are available])
			ZFS_LINUX_TEST_RESULT([disk_io_acct], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_DISK_IO_ACCT, 1, [disk_*_io_acct() available])
			], [
				AC_MSG_RESULT(no)

				dnl #
				dnl # 5.7 API,
				dnl #
				dnl # Added bio_start_io_acct() and bio_end_io_acct() helpers.
				dnl #
				AC_MSG_CHECKING([whether generic bio_*_io_acct() are available])
				ZFS_LINUX_TEST_RESULT([bio_io_acct], [
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_BIO_IO_ACCT, 1, [bio_*_io_acct() available])
				], [
					AC_MSG_RESULT(no)

					dnl #
					dnl # 4.14 API,
					dnl #
					dnl # generic_start_io_acct/generic_end_io_acct now require
					dnl # request_queue to be provided. No functional changes,
					dnl # but preparation for inflight accounting.
					dnl #
					AC_MSG_CHECKING([whether generic_*_io_acct wants 4 args])
					ZFS_LINUX_TEST_RESULT_SYMBOL([generic_acct_4args],
					    [generic_start_io_acct], [block/bio.c], [
						AC_MSG_RESULT(yes)
						AC_DEFINE(HAVE_GENERIC_IO_ACCT_4ARG, 1,
						    [generic_*_io_acct() 4 arg available])
					], [
						AC_MSG_RESULT(no)
					])
				])
			])
		])
	])
])
