dnl #
dnl # 4.2 API change
dnl # This kernel retired the nameidata structure which forced the
dnl # restructuring of the follow_link() prototype and how it is called.
dnl # We check for the new interface rather than detecting the old one.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_FOLLOW_LINK], [
	AC_MSG_CHECKING([whether iops->follow_link() passes nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		const char *follow_link(struct dentry *de, void **cookie)
		    { return "symlink"; }
		static struct inode_operations iops __attribute__ ((unused)) = {
			.follow_link = follow_link,
		};
	],[
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FOLLOW_LINK_NAMEIDATA, 1,
		          [iops->follow_link() nameidata])
	])
])
