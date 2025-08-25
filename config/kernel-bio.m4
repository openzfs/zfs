dnl #
dnl # Linux 4.8 API,
dnl #
dnl # The bio_op() helper was introduced as a replacement for explicitly
dnl # checking the bio->bi_rw flags.  The following checks are used to
dnl # detect if a specific operation is supported.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_OPS], [
	ZFS_LINUX_TEST_SRC([bio_set_op_attrs], [
		#include <linux/bio.h>
	],[
		struct bio *bio __attribute__ ((unused)) = NULL;
		bio_set_op_attrs(bio, 0, 0);
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
	], [], [ZFS_META_LICENSE])
])

dnl #
dnl # Linux 5.16 API
dnl #
dnl # bio_set_dev is no longer a helper macro and is now an inline function,
dnl # meaning that the function it calls internally can no longer be overridden
dnl # by our code
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_SET_DEV_MACRO], [
	ZFS_LINUX_TEST_SRC([bio_set_dev_macro], [
		#include <linux/bio.h>
		#include <linux/fs.h>
	],[
		#ifndef bio_set_dev
		#error Not a macro
		#endif
	], [], [ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_DEV], [
	AC_MSG_CHECKING([whether bio_set_dev() is GPL-only])
	ZFS_LINUX_TEST_RESULT([bio_set_dev_license], [
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_DEV_GPL_ONLY, 1,
		    [bio_set_dev() GPL-only])
	])

	AC_MSG_CHECKING([whether bio_set_dev() is a macro])
	ZFS_LINUX_TEST_RESULT([bio_set_dev_macro], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_DEV_MACRO, 1,
		    [bio_set_dev() is a macro])
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
	], [], [ZFS_META_LICENSE])
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

dnl #
dnl # Linux 5.12 API,
dnl #
dnl # The Linux 5.12 kernel updated struct bio to create a new bi_bdev member
dnl # and bio->bi_disk was moved to bio->bi_bdev->bd_disk
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_BDEV_DISK], [
	ZFS_LINUX_TEST_SRC([bio_bdev_disk], [
		#include <linux/blk_types.h>
		#include <linux/blkdev.h>
	],[
		struct bio *b = NULL;
		struct gendisk *d = b->bi_bdev->bd_disk;
		blk_register_queue(d);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_BDEV_DISK], [
	AC_MSG_CHECKING([whether bio->bi_bdev->bd_disk exists])
	ZFS_LINUX_TEST_RESULT([bio_bdev_disk], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_BDEV_DISK, 1, [bio->bi_bdev->bd_disk exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 5.16 API
dnl #
dnl # The Linux 5.16 API for submit_bio changed the return type to be
dnl # void instead of int
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BDEV_SUBMIT_BIO_RETURNS_VOID], [
	ZFS_LINUX_TEST_SRC([bio_bdev_submit_bio_void], [
		#include <linux/blkdev.h>
	],[
		struct block_device_operations *bdev = NULL;
		__attribute__((unused)) void(*f)(struct bio *) = bdev->submit_bio;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BDEV_SUBMIT_BIO_RETURNS_VOID], [
	AC_MSG_CHECKING(
		[whether block_device_operations->submit_bio() returns void])
	ZFS_LINUX_TEST_RESULT([bio_bdev_submit_bio_void], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BDEV_SUBMIT_BIO_RETURNS_VOID, 1,
			[block_device_operations->submit_bio() returns void])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 5.18 API
dnl #
dnl # In 07888c665b405b1cd3577ddebfeb74f4717a84c4 ("block: pass a block_device and opf to bio_alloc")
dnl #   bio_alloc(gfp_t gfp_mask, unsigned short nr_iovecs)
dnl # became
dnl #   bio_alloc(struct block_device *bdev, unsigned short nr_vecs, unsigned int opf, gfp_t gfp_mask)
dnl # however
dnl # > NULL/0 can be passed, both for the
dnl # > passthrough case on a raw request_queue and to temporarily avoid
dnl # > refactoring some nasty code.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO_ALLOC_4ARG], [
	ZFS_LINUX_TEST_SRC([bio_alloc_4arg], [
		#include <linux/bio.h>
	],[
		gfp_t gfp_mask = 0;
		unsigned short nr_iovecs = 0;
		struct block_device *bdev = NULL;
		unsigned int opf = 0;

		struct bio *__attribute__((unused)) allocated = bio_alloc(bdev, nr_iovecs, opf, gfp_mask);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_ALLOC_4ARG], [
	AC_MSG_CHECKING([whether bio_alloc() wants 4 args])
	ZFS_LINUX_TEST_RESULT([bio_alloc_4arg],[
		AC_MSG_RESULT(yes)
		AC_DEFINE([HAVE_BIO_ALLOC_4ARG], 1, [bio_alloc() takes 4 arguments])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BIO], [
	ZFS_AC_KERNEL_SRC_BIO_OPS
	ZFS_AC_KERNEL_SRC_BIO_SET_DEV
	ZFS_AC_KERNEL_SRC_BIO_CURRENT_BIO_LIST
	ZFS_AC_KERNEL_SRC_BLKG_TRYGET
	ZFS_AC_KERNEL_SRC_BIO_BDEV_DISK
	ZFS_AC_KERNEL_SRC_BDEV_SUBMIT_BIO_RETURNS_VOID
	ZFS_AC_KERNEL_SRC_BIO_SET_DEV_MACRO
	ZFS_AC_KERNEL_SRC_BIO_ALLOC_4ARG
])

AC_DEFUN([ZFS_AC_KERNEL_BIO], [
	ZFS_AC_KERNEL_BIO_SET_OP_ATTRS
	ZFS_AC_KERNEL_BIO_SET_DEV
	ZFS_AC_KERNEL_BIO_CURRENT_BIO_LIST
	ZFS_AC_KERNEL_BLKG_TRYGET
	ZFS_AC_KERNEL_BIO_BDEV_DISK
	ZFS_AC_KERNEL_BDEV_SUBMIT_BIO_RETURNS_VOID
	ZFS_AC_KERNEL_BIO_ALLOC_4ARG
])
