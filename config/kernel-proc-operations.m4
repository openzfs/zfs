dnl #
dnl # 5.6 API Change
dnl # The proc_ops structure was introduced to replace the use of
dnl # of the file_operations structure when registering proc handlers.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PROC_OPERATIONS], [
	ZFS_LINUX_TEST_SRC([proc_ops_struct], [
		#include <linux/proc_fs.h>

		int test_open(struct inode *ip, struct file *fp) { return 0; }
		ssize_t test_read(struct file *fp, char __user *ptr,
		    size_t size, loff_t *offp) { return 0; }
		ssize_t test_write(struct file *fp, const char __user *ptr,
		    size_t size, loff_t *offp) { return 0; }
		loff_t test_lseek(struct file *fp, loff_t off, int flag)
		    { return 0; }
		int test_release(struct inode *ip, struct file *fp)
		    { return 0; }

		const struct proc_ops test_ops __attribute__ ((unused)) = {
			.proc_open      = test_open,
			.proc_read      = test_read,
			.proc_write	= test_write,
			.proc_lseek     = test_lseek,
			.proc_release   = test_release,
		};
	], [
		struct proc_dir_entry *entry __attribute__ ((unused)) =
		    proc_create_data("test", 0444, NULL, &test_ops, NULL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_PROC_OPERATIONS], [
	AC_MSG_CHECKING([whether proc_ops structure exists])
	ZFS_LINUX_TEST_RESULT([proc_ops_struct], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PROC_OPS_STRUCT, 1, [proc_ops structure exists])
	], [
		AC_MSG_RESULT(no)
	])
])
