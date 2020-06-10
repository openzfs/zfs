dnl #
dnl # Supported symlink APIs
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_PUT_LINK], [
	ZFS_LINUX_TEST_SRC([put_link_cookie], [
		#include <linux/fs.h>
		void put_link(struct inode *ip, void *cookie)
		    { return; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.put_link = put_link,
		};
	],[])

	ZFS_LINUX_TEST_SRC([put_link_nameidata], [
		#include <linux/fs.h>
		void put_link(struct dentry *de, struct
		    nameidata *nd, void *ptr) { return; }
		static struct inode_operations
		    iops __attribute__ ((unused)) = {
			.put_link = put_link,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_PUT_LINK], [
	dnl #
	dnl # 4.5 API change
	dnl # get_link() uses delayed done, there is no put_link() interface.
	dnl # This check initially uses the inode_operations_get_link result
	dnl #
	ZFS_LINUX_TEST_RESULT([inode_operations_get_link], [
		AC_DEFINE(HAVE_PUT_LINK_DELAYED, 1, [iops->put_link() delayed])
	],[
		dnl #
		dnl # 4.2 API change
		dnl # This kernel retired the nameidata structure.
		dnl #
		AC_MSG_CHECKING([whether iops->put_link() passes cookie])
		ZFS_LINUX_TEST_RESULT([put_link_cookie], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_PUT_LINK_COOKIE, 1,
			    [iops->put_link() cookie])
		],[
			AC_MSG_RESULT(no)

			dnl #
			dnl # 2.6.32 API
			dnl #
			AC_MSG_CHECKING(
			    [whether iops->put_link() passes nameidata])
			ZFS_LINUX_TEST_RESULT([put_link_nameidata], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_PUT_LINK_NAMEIDATA, 1,
				    [iops->put_link() nameidata])
			],[
				ZFS_LINUX_TEST_ERROR([put_link])
			])
		])
	])
])
