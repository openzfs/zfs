dnl #
dnl # 2.6.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDEV_BLOCK_DEVICE_OPERATIONS], [
	AC_MSG_CHECKING([block device operation prototypes])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>

		int blk_open(struct block_device *bdev, fmode_t mode)
		    { return 0; }
		int blk_ioctl(struct block_device *bdev, fmode_t mode,
		    unsigned x, unsigned long y) { return 0; }
		int blk_compat_ioctl(struct block_device * bdev, fmode_t mode,
		    unsigned x, unsigned long y) { return 0; }

		static const struct block_device_operations
		    bops __attribute__ ((unused)) = {
			.open		= blk_open,
			.ioctl		= blk_ioctl,
			.compat_ioctl	= blk_compat_ioctl,
		};
	],[
	],[
		AC_MSG_RESULT(struct block_device)
		AC_DEFINE(HAVE_BDEV_BLOCK_DEVICE_OPERATIONS, 1,
		          [struct block_device_operations use bdevs])
	],[
		AC_MSG_RESULT(struct inode)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 3.10 API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BOPS_VOID_RELEASE], [
	AC_MSG_CHECKING([whether bops->release() returns void])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>

		static const struct block_device_operations
		    bops __attribute__ ((unused));
	],[
		int a = bops.release(NULL, 0);
		return a;
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_BOPS_VOID_RELEASE, 1,
		          [bops->release() returns void])

	])
	EXTRA_KCFLAGS="$tmp_flags"
])
