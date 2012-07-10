dnl #
dnl # 3.5.0 API change #
dnl # torvalds/linux@b0b0382bb4904965a9e9fca77ad87514dfda0d1c changed the header
dnl # to use struct inode * instead of struct dentry *
dnl #
AC_DEFUN([ZFS_AC_KERNEL_EXPORT_ENCODE_FH_WITH_INODE_PARAMETER], [
	AC_MSG_CHECKING([export_operations->encodefh()])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/exportfs.h>
	],[
		int (*encode_fh)(struct inode *, __u32 *fh, int *, struct inode *) = NULL;
		struct export_operations export_ops = {
			.encode_fh	= encode_fh,
		};
		export_ops.encode_fh(0, 0, 0, 0);
	],[
		AC_MSG_RESULT(uses struct inode * as first parameter)
		AC_DEFINE(HAVE_EXPORT_ENCODE_FH_WITH_INODE_PARAMETER, 1,
		          [fhfn() uses struct inode *])
	],[
		AC_MSG_RESULT(does not use struct inode * as first parameter)
	])
])
