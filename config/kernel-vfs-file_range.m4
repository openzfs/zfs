dnl #
dnl # The *_file_range APIs have a long history:
dnl #
dnl # 2.6.29: BTRFS_IOC_CLONE and BTRFS_IOC_CLONE_RANGE ioctl introduced
dnl # 3.12: BTRFS_IOC_FILE_EXTENT_SAME ioctl introduced
dnl #
dnl # 4.5: copy_file_range() syscall introduced, added to VFS
dnl # 4.5: BTRFS_IOC_CLONE and BTRFS_IOC_CLONE_RANGE renamed to FICLONE ands
dnl #      FICLONERANGE, added to VFS as clone_file_range()
dnl # 4.5: BTRFS_IOC_FILE_EXTENT_SAME renamed to FIDEDUPERANGE, added to VFS
dnl #      as dedupe_file_range()
dnl #
dnl # 4.20: VFS clone_file_range() and dedupe_file_range() replaced by
dnl #       remap_file_range()
dnl #
dnl # 5.3: VFS copy_file_range() expected to do its own fallback,
dnl #      generic_copy_file_range() added to support it
dnl #
dnl # 6.8: generic_copy_file_range() removed, replaced by
dnl #      splice_copy_file_range()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_GENERIC_COPY_FILE_RANGE], [
	ZFS_LINUX_TEST_SRC([generic_copy_file_range], [
		#include <linux/fs.h>
	], [
		struct file *src_file __attribute__ ((unused)) = NULL;
		loff_t src_off __attribute__ ((unused)) = 0;
		struct file *dst_file __attribute__ ((unused)) = NULL;
		loff_t dst_off __attribute__ ((unused)) = 0;
		size_t len __attribute__ ((unused)) = 0;
		unsigned int flags __attribute__ ((unused)) = 0;
		generic_copy_file_range(src_file, src_off, dst_file, dst_off,
		    len, flags);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_GENERIC_COPY_FILE_RANGE], [
	AC_MSG_CHECKING([whether generic_copy_file_range() is available])
	ZFS_LINUX_TEST_RESULT_SYMBOL([generic_copy_file_range],
	[generic_copy_file_range], [fs/read_write.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_GENERIC_COPY_FILE_RANGE, 1,
		    [generic_copy_file_range() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_SPLICE_COPY_FILE_RANGE], [
	ZFS_LINUX_TEST_SRC([splice_copy_file_range], [
		#include <linux/splice.h>
	], [
		struct file *src_file __attribute__ ((unused)) = NULL;
		loff_t src_off __attribute__ ((unused)) = 0;
		struct file *dst_file __attribute__ ((unused)) = NULL;
		loff_t dst_off __attribute__ ((unused)) = 0;
		size_t len __attribute__ ((unused)) = 0;
		splice_copy_file_range(src_file, src_off, dst_file, dst_off,
		    len);
	])
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_SPLICE_COPY_FILE_RANGE], [
	AC_MSG_CHECKING([whether splice_copy_file_range() is available])
	ZFS_LINUX_TEST_RESULT([splice_copy_file_range], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_SPLICE_COPY_FILE_RANGE, 1,
		    [splice_copy_file_range() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_CLONE_FILE_RANGE], [
	ZFS_LINUX_TEST_SRC([vfs_clone_file_range], [
		#include <linux/fs.h>

		static int test_clone_file_range(struct file *src_file,
		    loff_t src_off, struct file *dst_file, loff_t dst_off,
		    u64 len) {
			(void) src_file; (void) src_off;
			(void) dst_file; (void) dst_off;
			(void) len;
			return (0);
		}

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.clone_file_range	= test_clone_file_range,
		};
	],[])
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_CLONE_FILE_RANGE], [
	AC_MSG_CHECKING([whether fops->clone_file_range() is available])
	ZFS_LINUX_TEST_RESULT([vfs_clone_file_range], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_CLONE_FILE_RANGE, 1,
		    [fops->clone_file_range() is available])
	],[
		AC_MSG_RESULT([no])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_DEDUPE_FILE_RANGE], [
	ZFS_LINUX_TEST_SRC([vfs_dedupe_file_range], [
		#include <linux/fs.h>

		static int test_dedupe_file_range(struct file *src_file,
		    loff_t src_off, struct file *dst_file, loff_t dst_off,
		    u64 len) {
			(void) src_file; (void) src_off;
			(void) dst_file; (void) dst_off;
			(void) len;
			return (0);
		}

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
                .dedupe_file_range	= test_dedupe_file_range,
		};
	],[])
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_DEDUPE_FILE_RANGE], [
	AC_MSG_CHECKING([whether fops->dedupe_file_range() is available])
	ZFS_LINUX_TEST_RESULT([vfs_dedupe_file_range], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_DEDUPE_FILE_RANGE, 1,
		    [fops->dedupe_file_range() is available])
	],[
		AC_MSG_RESULT([no])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_REMAP_FILE_RANGE], [
	ZFS_LINUX_TEST_SRC([vfs_remap_file_range], [
		#include <linux/fs.h>

		static loff_t test_remap_file_range(struct file *src_file,
		    loff_t src_off, struct file *dst_file, loff_t dst_off,
		    loff_t len, unsigned int flags) {
			(void) src_file; (void) src_off;
			(void) dst_file; (void) dst_off;
			(void) len; (void) flags;
			return (0);
		}

		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.remap_file_range	= test_remap_file_range,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_REMAP_FILE_RANGE], [
	AC_MSG_CHECKING([whether fops->remap_file_range() is available])
	ZFS_LINUX_TEST_RESULT([vfs_remap_file_range], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_REMAP_FILE_RANGE, 1,
		    [fops->remap_file_range() is available])
	],[
		AC_MSG_RESULT([no])
	])
])
