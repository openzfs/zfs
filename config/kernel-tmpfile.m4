dnl #
dnl # 3.11 API change
dnl # Add support for i_op->tmpfile
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_TMPFILE], [
	dnl #
	dnl # 6.3 API change
	dnl # The first arg is now struct mnt_idmap * 
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_mnt_idmap], [
		#include <linux/fs.h>
		static int tmpfile(struct mnt_idmap *idmap,
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
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile], [
		#include <linux/fs.h>
		static int tmpfile(struct user_namespace *userns,
		    struct inode *inode, struct file *file,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[])
	dnl #
	dnl # 5.11 API change
	dnl # add support for userns parameter to tmpfile
	dnl #
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_dentry_userns], [
		#include <linux/fs.h>
		static int tmpfile(struct user_namespace *userns,
		    struct inode *inode, struct dentry *dentry,
		    umode_t mode) { return 0; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.tmpfile = tmpfile,
		};
	],[])
	ZFS_LINUX_TEST_SRC([inode_operations_tmpfile_dentry], [
			#include <linux/fs.h>
			static int tmpfile(struct inode *inode, struct dentry *dentry,
			    umode_t mode) { return 0; }
			static struct inode_operations
			    iops __attribute__ ((unused)) = {
				.tmpfile = tmpfile,
			};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_TMPFILE], [
	AC_MSG_CHECKING([whether i_op->tmpfile() exists])
	ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_mnt_idmap], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TMPFILE, 1, [i_op->tmpfile() exists])
		AC_DEFINE(HAVE_TMPFILE_IDMAP, 1, [i_op->tmpfile() has mnt_idmap])
	], [
		ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_TMPFILE, 1, [i_op->tmpfile() exists])
			AC_DEFINE(HAVE_TMPFILE_USERNS, 1, [i_op->tmpfile() has userns])
		],[
			ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_dentry_userns], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_TMPFILE, 1, [i_op->tmpfile() exists])
				AC_DEFINE(HAVE_TMPFILE_USERNS, 1, [i_op->tmpfile() has userns])
				AC_DEFINE(HAVE_TMPFILE_DENTRY, 1, [i_op->tmpfile() uses old dentry signature])
			],[
				ZFS_LINUX_TEST_RESULT([inode_operations_tmpfile_dentry], [
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_TMPFILE, 1, [i_op->tmpfile() exists])
					AC_DEFINE(HAVE_TMPFILE_DENTRY, 1, [i_op->tmpfile() uses old dentry signature])
				],[
					ZFS_LINUX_REQUIRE_API([i_op->tmpfile()], [3.11])
				])
			])
		])
	])
])
