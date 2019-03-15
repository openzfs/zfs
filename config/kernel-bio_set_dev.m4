dnl #
dnl # Linux 4.14 API,
dnl #
dnl # The bio_set_dev() helper macro was introduced as part of the transition
dnl # to have struct gendisk in struct bio. 
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_DEV_MACRO], [
	AC_MSG_CHECKING([whether bio_set_dev() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/bio.h>
		#include <linux/fs.h>
	],[
		struct block_device *bdev = NULL;
		struct bio *bio = NULL;
		bio_set_dev(bio, bdev);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_DEV, 1, [bio_set_dev() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Linux 5.0 API,
dnl #
dnl # The bio_set_dev() helper macro was updated to internally depend on
dnl # bio_associate_blkg() symbol which is exported GPL-only.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_DEV_GPL_ONLY], [
	AC_MSG_CHECKING([whether bio_set_dev() is GPL-only])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
		#include <linux/bio.h>
		#include <linux/fs.h>
		MODULE_LICENSE("$ZFS_META_LICENSE");
	],[
		struct block_device *bdev = NULL;
		struct bio *bio = NULL;
		bio_set_dev(bio, bdev);
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BIO_SET_DEV_GPL_ONLY, 1,
		    [bio_set_dev() GPL-only])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BIO_SET_DEV], [
	ZFS_AC_KERNEL_BIO_SET_DEV_MACRO
	ZFS_AC_KERNEL_BIO_SET_DEV_GPL_ONLY
])
