dnl #
dnl # Linux 5.14 adds a change to require set_page_dirty to be manually
dnl # wired up in struct address_space_operations. Determine if this needs
dnl # to be done. This patch set also introduced __set_page_dirty_nobuffers
dnl # declaration in linux/pagemap.h, so these tests look for the presence
dnl # of that function to tell the compiler to assign set_page_dirty in
dnl # module/os/linux/zfs/zpl_file.c
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_SET_PAGE_DIRTY_NOBUFFERS], [
	ZFS_LINUX_TEST_SRC([vfs_has_set_page_dirty_nobuffers], [
		#include <linux/pagemap.h>
		#include <linux/fs.h>

		static const struct address_space_operations
		    aops __attribute__ ((unused)) = {
			.set_page_dirty = __set_page_dirty_nobuffers,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_SET_PAGE_DIRTY_NOBUFFERS], [
	dnl #
	dnl # Linux 5.14 change requires set_page_dirty() to be assigned
	dnl # in address_space_operations()
	dnl #
	AC_MSG_CHECKING([whether __set_page_dirty_nobuffers exists])
	ZFS_LINUX_TEST_RESULT([vfs_has_set_page_dirty_nobuffers], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_VFS_SET_PAGE_DIRTY_NOBUFFERS, 1,
			[__set_page_dirty_nobuffers exists])
	],[
		AC_MSG_RESULT([no])
	])
])
