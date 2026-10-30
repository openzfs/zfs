dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # In 6.1, target changed from struct dentry to struct file. In 6.3,
dnl # idmap mechanism changed from user_namespace to mnt_idmap. We test
dnl # both for the struct file even though HAVE_IDMAP_MNTIDMAP implies
dnl # HAVE_TMPFILE_FILE, because HAVE_TMPFILE_FILE is used away from idmap
dnl # and so that would be confusing.
dnl #
dnl # 7.4, mnt_idmap is passed to inode ops as const, so we need a second check
dnl # with a const prototype.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TMPFILE], [
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_mntidmap_file], [
		#include <linux/fs.h>
		static int tmpfile(struct mnt_idmap *idmap,
		    struct inode *inode, struct file *file,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_mntidmap_file_const], [
		#include <linux/fs.h>
		static int tmpfile(const struct mnt_idmap *idmap,
		    struct inode *inode, struct file *file,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[])

	dnl # 6.1 API change
	dnl # use struct file instead of struct dentry
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_userns_file], [
		#include <linux/fs.h>
		static int tmpfile(struct user_namespace *userns,
		    struct inode *inode, struct file *file,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_TMPFILE], [
	AC_MSG_CHECKING([whether i_op->tmpfile() takes struct file])
	_result=no
	ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_mntidmap_file],
	    [_result=yes])
	ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_mntidmap_file_const],
	    [_result=yes])
	AS_IF([test "x$_result" = "xyes"], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TMPFILE_FILE, 1,
		    [i_op->tmpfile() uses takes struct file])
	],[
		ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_userns_file], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_TMPFILE_FILE, 1,
			    [i_op->tmpfile() uses takes struct file])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
