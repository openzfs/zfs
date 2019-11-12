dnl #
dnl # grsecurity API change,
dnl # kmem_cache_create() with SLAB_USERCOPY flag replaced by
dnl # kmem_cache_create_usercopy().
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KMEM_CACHE_CREATE_USERCOPY], [
	ZFS_LINUX_TEST_SRC([kmem_cache_create_usercopy], [
		#include <linux/slab.h>
		static void ctor(void *foo) { /* fake ctor */ }
	],[
		struct kmem_cache *skc_linux_cache;
		const char *name = "test";
		size_t size = 4096;
		size_t align = 8;
		unsigned long flags = 0;
		size_t useroffset = 0;
		size_t usersize = size - useroffset;

		skc_linux_cache = kmem_cache_create_usercopy(
		    name, size, align, flags, useroffset, usersize, ctor);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KMEM_CACHE_CREATE_USERCOPY], [
	AC_MSG_CHECKING([whether kmem_cache_create_usercopy() exists])
	ZFS_LINUX_TEST_RESULT([kmem_cache_create_usercopy], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMEM_CACHE_CREATE_USERCOPY, 1,
		    [kmem_cache_create_usercopy() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_KMEM_CACHE], [
	ZFS_AC_KERNEL_SRC_KMEM_CACHE_CREATE_USERCOPY
])

AC_DEFUN([ZFS_AC_KERNEL_KMEM_CACHE], [
	ZFS_AC_KERNEL_KMEM_CACHE_CREATE_USERCOPY
])
