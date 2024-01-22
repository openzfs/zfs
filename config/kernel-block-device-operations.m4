dnl #
dnl # 2.6.38 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS], [
	ZFS_LINUX_TEST_SRC([block_device_operations_check_events], [
		#include <linux/blkdev.h>

		static unsigned int blk_check_events(struct gendisk *disk,
		    unsigned int clearing) {
			(void) disk, (void) clearing;
			return (0);
		}

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.check_events	= blk_check_events,
		};
	], [], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS], [
	AC_MSG_CHECKING([whether bops->check_events() exists])
	ZFS_LINUX_TEST_RESULT([block_device_operations_check_events], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([bops->check_events()])
	])
])

dnl #
dnl # 3.10.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID], [
	ZFS_LINUX_TEST_SRC([block_device_operations_release_void], [
		#include <linux/blkdev.h>

		static void blk_release(struct gendisk *g, fmode_t mode) {
			(void) g, (void) mode;
			return;
		}

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.open		= NULL,
			.release	= blk_release,
			.ioctl		= NULL,
			.compat_ioctl	= NULL,
		};
	], [], [])
])

dnl #
dnl # 5.9.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_RELEASE_1ARG], [
	ZFS_LINUX_TEST_SRC([block_device_operations_release_void_1arg], [
		#include <linux/blkdev.h>

		static void blk_release(struct gendisk *g) {
			(void) g;
			return;
		}

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.open		= NULL,
			.release	= blk_release,
			.ioctl		= NULL,
			.compat_ioctl	= NULL,
		};
	], [], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID], [
	AC_MSG_CHECKING([whether bops->release() is void and takes 2 args])
	ZFS_LINUX_TEST_RESULT([block_device_operations_release_void], [
		AC_MSG_RESULT(yes)
	],[
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether bops->release() is void and takes 1 arg])
		ZFS_LINUX_TEST_RESULT([block_device_operations_release_void_1arg], [
			AC_MSG_RESULT(yes)
			AC_DEFINE([HAVE_BLOCK_DEVICE_OPERATIONS_RELEASE_1ARG], [1],
				[Define if release() in block_device_operations takes 1 arg])
		],[
			ZFS_LINUX_TEST_ERROR([bops->release()])
		])
	])
])

dnl #
dnl # 5.13 API change
dnl # block_device_operations->revalidate_disk() was removed
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK], [
	ZFS_LINUX_TEST_SRC([block_device_operations_revalidate_disk], [
		#include <linux/blkdev.h>

		static int blk_revalidate_disk(struct gendisk *disk) {
			(void) disk;
			return(0);
		}

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.revalidate_disk	= blk_revalidate_disk,
		};
	], [], [])
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK], [
	AC_MSG_CHECKING([whether bops->revalidate_disk() exists])
	ZFS_LINUX_TEST_RESULT([block_device_operations_revalidate_disk], [
		AC_DEFINE([HAVE_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK], [1],
			[Define if revalidate_disk() in block_device_operations])
		AC_MSG_RESULT(yes)
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS], [
	ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
	ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
	ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_RELEASE_1ARG
	ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS], [
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
])
