dnl #
dnl # 2.6.35 API change,
dnl # The 'struct xattr_handler' was constified in the generic
dnl # super_block structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONST_XATTR_HANDLER],
	[AC_MSG_CHECKING([whether super_block uses const struct xattr_hander])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/xattr.h>

		const struct xattr_handler xattr_test_handler = {
			.prefix	= "test",
			.get	= NULL,
			.set	= NULL,
		};

		const struct xattr_handler *xattr_handlers[] = {
			&xattr_test_handler,
		};
	],[
		struct super_block sb;

		sb.s_xattr = xattr_handlers;
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_CONST_XATTR_HANDLER, 1,
		          [super_block uses const struct xattr_hander])
	],[
		AC_MSG_RESULT([no])
	])
])
