dnl #
dnl # 2.6.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDEV_BLOCK_DEVICE_OPERATIONS], [
	AC_MSG_CHECKING([block device operation prototypes])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="${NO_UNUSED_BUT_SET_VARIABLE}"
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/blkdev.h>
	],[
		int (*blk_open) (struct block_device *, fmode_t) = NULL;
		int (*blk_release) (struct gendisk *, fmode_t) = NULL;
		int (*blk_ioctl) (struct block_device *, fmode_t,
		                  unsigned, unsigned long) = NULL;
		int (*blk_compat_ioctl) (struct block_device *, fmode_t,
                                         unsigned, unsigned long) = NULL;
		struct block_device_operations blk_ops = {
			.open		= blk_open,
			.release	= blk_release,
			.ioctl		= blk_ioctl,
			.compat_ioctl	= blk_compat_ioctl,
		};
		
		blk_ops.open(NULL, 0);
		blk_ops.release(NULL, 0);
		blk_ops.ioctl(NULL, 0, 0, 0);
		blk_ops.compat_ioctl(NULL, 0, 0, 0);
	],[
		AC_MSG_RESULT(struct block_device)
		AC_DEFINE(HAVE_BDEV_BLOCK_DEVICE_OPERATIONS, 1,
		          [struct block_device_operations use bdevs])
	],[
		AC_MSG_RESULT(struct inode)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])
