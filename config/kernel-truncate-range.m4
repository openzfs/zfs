dnl #
dnl # 3.5.0 API change #
dnl # torvalds/linux@17cf28afea2a1112f240a3a2da8af883be024811 removed
dnl # truncate_range(). The file hole punching functionality is provided by
dnl # fallocate()
dnl #
AC_DEFUN([ZFS_AC_KERNEL_INODE_TRUNCATE_RANGE], [
	AC_MSG_CHECKING([inode_operations->truncate_range() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		void (*tr)(struct inode *, loff_t, loff_t) = NULL;
		struct inode_operations inode_ops = {
			.truncate_range	= tr,
		};
		inode_ops.truncate_range(0, 0, 0);

		
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_TRUNCATE_RANGE, 1,
		          [inode_operations->truncate_range() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
