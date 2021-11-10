dnl #
dnl # 3.11 API change
dnl # Add support for i_op->tmpfile
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TMPFILE], [
	dnl #
	dnl # 5.11 API change
	dnl # add support for userns parameter to tmpfile
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_userns], [
		#include <linux/fs.h>
		int tmpfile(struct user_namespace *userns,
		    struct inode *inode, struct dentry *dentry,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile], [
			#include <linux/fs.h>
			int tmpfile(struct inode *inode, struct dentry *dentry,
			    umode_t mode) { return 0; }
			static struct inode_operations
			    iops __attribute__ ((unused)) = {
				.tmpfile = tmpfile,
			};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_TMPFILE], [
	AC_MSG_CHECKING([whether i_op->tmpfile() exists])
	ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_userns], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TMPFILE, 1, [i_op->tmpfile() exists])
		AC_DEFINE(HAVE_TMPFILE_USERNS, 1, [i_op->tmpfile() has userns])
	],[
		ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_TMPFILE, 1, [i_op->tmpfile() exists])
		],[
			AC_MSG_RESULT(no)
		])
	])
])
