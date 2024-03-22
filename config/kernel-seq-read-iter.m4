dnl #
dnl # Linux 5.10 added the the seq_read_iter helper
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SEQ_READ_ITER], [
	ZFS_LINUX_TEST_SRC([seq_file_has_seq_read_iter], [
		#include <linux/seq_file.h>
		#include <linux/fs.h>
		static const struct file_operations
		    fops __attribute__ ((unused)) = {
			.read_iter =	seq_read_iter
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SEQ_READ_ITER], [
	dnl #
	dnl # Linux 5.10 added the the seq_read_iter helper
	dnl #
	AC_MSG_CHECKING([whether seq_file's seq_read_iter() exists])
	ZFS_LINUX_TEST_RESULT([seq_file_has_seq_read_iter], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_SEQ_READ_ITER, 1,
			[seq_file has seq_read_iter()])
	],[
		AC_MSG_RESULT([no])
	])
])
