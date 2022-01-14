dnl #
dnl # Enabled by default it provides a minimal level of memory tracking.
dnl # A total count of bytes allocated is kept for each alloc and free.
dnl # Then at module unload time a report to the console will be printed
dnl # if memory was leaked.
dnl #
AC_DEFUN([SPL_AC_DEBUG_KMEM], [
	AC_ARG_ENABLE([debug-kmem],
		[AS_HELP_STRING([--enable-debug-kmem],
		[Enable basic kmem accounting @<:@default=no@:>@])],
		[],
		[enable_debug_kmem=no])

	AS_IF([test "x$enable_debug_kmem" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM"
		DEBUG_KMEM="_with_debug_kmem"
		AC_DEFINE([DEBUG_KMEM], [1],
		[Define to 1 to enable basic kmem accounting])
	], [
		DEBUG_KMEM="_without_debug_kmem"
	])

	AC_SUBST(DEBUG_KMEM)
	AC_MSG_CHECKING([whether basic kmem accounting is enabled])
	AC_MSG_RESULT([$enable_debug_kmem])
])

dnl #
dnl # Disabled by default it provides detailed memory tracking.  This
dnl # feature also requires --enable-debug-kmem to be set.  When enabled
dnl # not only will total bytes be tracked but also the location of every
dnl # alloc and free.  When the SPL module is unloaded a list of all leaked
dnl # addresses and where they were allocated will be dumped to the console.
dnl # Enabling this feature has a significant impact on performance but it
dnl # makes finding memory leaks pretty straight forward.
dnl #
AC_DEFUN([SPL_AC_DEBUG_KMEM_TRACKING], [
	AC_ARG_ENABLE([debug-kmem-tracking],
		[AS_HELP_STRING([--enable-debug-kmem-tracking],
		[Enable detailed kmem tracking  @<:@default=no@:>@])],
		[],
		[enable_debug_kmem_tracking=no])

	AS_IF([test "x$enable_debug_kmem_tracking" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM_TRACKING"
		DEBUG_KMEM_TRACKING="_with_debug_kmem_tracking"
		AC_DEFINE([DEBUG_KMEM_TRACKING], [1],
		[Define to 1 to enable detailed kmem tracking])
	], [
		DEBUG_KMEM_TRACKING="_without_debug_kmem_tracking"
	])

	AC_SUBST(DEBUG_KMEM_TRACKING)
	AC_MSG_CHECKING([whether detailed kmem tracking is enabled])
	AC_MSG_RESULT([$enable_debug_kmem_tracking])
])

dnl #
dnl # 4.12 API,
dnl # Added kvmalloc allocation strategy
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_KVMALLOC], [
	ZFS_LINUX_TEST_SRC([kvmalloc], [
		#include <linux/mm.h>
		#include <linux/slab.h>
	],[
		void *p __attribute__ ((unused));

		p = kvmalloc(0, GFP_KERNEL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_KVMALLOC], [
	AC_MSG_CHECKING([whether kvmalloc(ptr, flags) is available])
	ZFS_LINUX_TEST_RESULT([kvmalloc], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KVMALLOC, 1, [kvmalloc exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 5.8 API,
dnl # __vmalloc PAGE_KERNEL removal
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VMALLOC_PAGE_KERNEL], [
	ZFS_LINUX_TEST_SRC([__vmalloc], [
		#include <linux/mm.h>
		#include <linux/vmalloc.h>
	],[
		void *p __attribute__ ((unused));

		p = __vmalloc(0, GFP_KERNEL, PAGE_KERNEL);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VMALLOC_PAGE_KERNEL], [
	AC_MSG_CHECKING([whether __vmalloc(ptr, flags, pageflags) is available])
	ZFS_LINUX_TEST_RESULT([__vmalloc], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VMALLOC_PAGE_KERNEL, 1, [__vmalloc page flags exists])
	],[
		AC_MSG_RESULT(no)
	])
])
-