dnl #
dnl # 2.6.38 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS], [
	ZFS_LINUX_TEST_SRC([block_device_operations_check_events], [
		#include <linux/blkdev.h>

		unsigned int blk_check_events(struct gendisk *disk,
		    unsigned int clearing) { return (0); }

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.check_events	= blk_check_events,
		};
	], [], [$NO_UNUSED_BUT_SET_VARIABLE])
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

		void blk_release(struct gendisk *g, fmode_t mode) { return; }

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.open		= NULL,
			.release	= blk_release,
			.ioctl		= NULL,
			.compat_ioctl	= NULL,
		};
	], [], [$NO_UNUSED_BUT_SET_VARIABLE])
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID], [
	AC_MSG_CHECKING([whether bops->release() is void])
	ZFS_LINUX_TEST_RESULT([block_device_operations_release_void], [
		AC_MSG_RESULT(yes)
	],[
		ZFS_LINUX_TEST_ERROR([bops->release()])
	])
])

dnl #
dnl # 5.13 API change
dnl # block_device_operations->revalidate_disk() was removed
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK], [
	ZFS_LINUX_TEST_SRC([block_device_operations_revalidate_disk], [
		#include <linux/blkdev.h>

		int blk_revalidate_disk(struct gendisk *disk) {
			return(0);
		}

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.revalidate_disk	= blk_revalidate_disk,
		};
	], [], [$NO_UNUSED_BUT_SET_VARIABLE])
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
	ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS], [
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_REVALIDATE_DISK
])
