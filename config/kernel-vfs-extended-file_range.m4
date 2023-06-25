dnl #
dnl # EL7 have backported copy_file_range and clone_file_range and
dnl # added them to an "extended" file_operations struct.
dnl #
dnl # We're testing for both functions in one here, because they will only
dnl # ever appear together and we don't want to match a similar method in
dnl # some future vendor kernel.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_FILE_OPERATIONS_EXTEND], [
	ZFS_LINUX_TEST_SRC([vfs_file_operations_extend], [
		#include <linux/fs.h>

		static ssize_t test_copy_file_range(struct file *src_file,
		    loff_t src_off, struct file *dst_file, loff_t dst_off,
		    size_t len, unsigned int flags) {
			(void) src_file; (void) src_off;
			(void) dst_file; (void) dst_off;
			(void) len; (void) flags;
			return (0);
		}

		static int test_clone_file_range(struct file *src_file,
		    loff_t src_off, struct file *dst_file, loff_t dst_off,
		    u64 len) {
			(void) src_file; (void) src_off;
			(void) dst_file; (void) dst_off;
			(void) len;
			return (0);
		}

		static const struct file_operations_extend
		    fops __attribute__ ((unused)) = {
			.kabi_fops = {},
			.copy_file_range = test_copy_file_range,
			.clone_file_range = test_clone_file_range,
		};
	],[])
])
AC_DEFUN([ZFS_AC_KERNEL_VFS_FILE_OPERATIONS_EXTEND], [
	AC_MSG_CHECKING([whether file_operations_extend takes \
.copy_file_range() and .clone_file_range()])
	ZFS_LINUX_TEST_RESULT([vfs_file_operations_extend], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_FILE_OPERATIONS_EXTEND, 1,
		    [file_operations_extend takes .copy_file_range()
		    and .clone_file_range()])
	],[
		AC_MSG_RESULT([no])
	])
])
