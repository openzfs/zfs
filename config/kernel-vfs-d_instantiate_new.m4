dnl # d_instantiate_new()
dnl #
dnl # Since Linux 5.15
dnl # Replaces d_instantiate(); unlock_new_inode() sequence

AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_D_INSTANTIATE_NEW], [
	ZFS_LINUX_TEST_SRC([vfs_d_instantiate_new], [
		#include <linux/dcache.h>
		#include <linux/fs.h>

		static void __attribute__ ((unused))
		probe(void)
		{
			d_instantiate_new(NULL, NULL);
		}
	], [])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_D_INSTANTIATE_NEW], [
	AC_MSG_CHECKING([whether d_instantiate_new() is available])
	ZFS_LINUX_TEST_RESULT([vfs_d_instantiate_new], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_VFS_D_INSTANTIATE_NEW], 1,
			[d_instantiate_new() is available])
	], [
		AC_MSG_RESULT([no])
	])
])
