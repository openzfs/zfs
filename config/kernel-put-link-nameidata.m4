dnl #
dnl # 4.2 API change
dnl # This kernel retired the nameidata structure which forced the
dnl # restructuring of the put_link() prototype and how it is called.
dnl # We check for the new interface rather than detecting the old one.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_PUT_LINK], [
	AC_MSG_CHECKING([whether iops->put_link() passes nameidata])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		void put_link(struct inode *ip, void *cookie) { return; }
		static struct inode_operations iops __attribute__ ((unused)) = {
			.put_link = put_link,
		};
	],[
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PUT_LINK_NAMEIDATA, 1,
		          [iops->put_link() nameidata])
	])
])
