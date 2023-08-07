AC_DEFUN([ZFS_AC_KERNEL_SRC_FILEMAP_SPLICE_READ], [
	dnl #
	dnl # Kernel 6.5 - generic_file_splice_read was removed in favor
	dnl # of filemap_splice_read for the .splice_read member of the
	dnl # file_operations struct.
	dnl #
	ZFS_LINUX_TEST_SRC([has_filemap_splice_read], [
		#include <linux/fs.h>

		struct file_operations fops __attribute__((unused)) = {
			.splice_read = filemap_splice_read,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_FILEMAP_SPLICE_READ], [
	AC_MSG_CHECKING([whether filemap_splice_read() exists])
	ZFS_LINUX_TEST_RESULT([has_filemap_splice_read], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILEMAP_SPLICE_READ, 1,
		    [filemap_splice_read exists])
	],[
		AC_MSG_RESULT(no)
	])
])
