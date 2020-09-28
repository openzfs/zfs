dnl #
dnl # Certain kernel build options are not supported.  These must be
dnl # detected at configure time and cause a build failure.  Otherwise
dnl # modules may be successfully built that behave incorrectly.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_DEFINED], [
	AS_IF([test "x$cross_compiling" != xyes], [
		AC_RUN_IFELSE([
			AC_LANG_PROGRAM([
				#include "$LINUX/include/linux/license.h"
			], [
				return !license_is_gpl_compatible(
				    "$ZFS_META_LICENSE");
			])
		], [
			AC_DEFINE([ZFS_IS_GPL_COMPATIBLE], [1],
			    [Define to 1 if GPL-only symbols can be used])
		], [
		])
	])

	ZFS_AC_KERNEL_SRC_CONFIG_THREAD_SIZE
	ZFS_AC_KERNEL_SRC_CONFIG_DEBUG_LOCK_ALLOC
	ZFS_AC_KERNEL_SRC_CONFIG_TRIM_UNUSED_KSYMS
	ZFS_AC_KERNEL_SRC_CONFIG_ZLIB_INFLATE
	ZFS_AC_KERNEL_SRC_CONFIG_ZLIB_DEFLATE

	AC_MSG_CHECKING([for kernel config option compatibility])
	ZFS_LINUX_TEST_COMPILE_ALL([config])
	AC_MSG_RESULT([done])

	ZFS_AC_KERNEL_CONFIG_THREAD_SIZE
	ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC
	ZFS_AC_KERNEL_CONFIG_TRIM_UNUSED_KSYMS
	ZFS_AC_KERNEL_CONFIG_ZLIB_INFLATE
	ZFS_AC_KERNEL_CONFIG_ZLIB_DEFLATE
])

dnl #
dnl # Check configured THREAD_SIZE
dnl #
dnl # The stack size will vary by architecture, but as of Linux 3.15 on x86_64
dnl # the default thread stack size was increased to 16K from 8K.  Therefore,
dnl # on newer kernels and some architectures stack usage optimizations can be
dnl # conditionally applied to improve performance without negatively impacting
dnl # stability.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONFIG_THREAD_SIZE], [
	ZFS_LINUX_TEST_SRC([config_thread_size], [
		#include <linux/module.h>
	],[
		#if (THREAD_SIZE < 16384)
		#error "THREAD_SIZE is less than 16K"
		#endif
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CONFIG_THREAD_SIZE], [
	AC_MSG_CHECKING([whether kernel was built with 16K or larger stacks])
	ZFS_LINUX_TEST_RESULT([config_thread_size], [
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_LARGE_STACKS, 1, [kernel has large stacks])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # Check CONFIG_DEBUG_LOCK_ALLOC
dnl #
dnl # This is typically only set for debug kernels because it comes with
dnl # a performance penalty.  However, when it is set it maps the non-GPL
dnl # symbol mutex_lock() to the GPL-only mutex_lock_nested() symbol.
dnl # This will cause a failure at link time which we'd rather know about
dnl # at compile time.
dnl #
dnl # Since we plan to pursue making mutex_lock_nested() a non-GPL symbol
dnl # with the upstream community we add a check to detect this case.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONFIG_DEBUG_LOCK_ALLOC], [
	ZFS_LINUX_TEST_SRC([config_debug_lock_alloc], [
		#include <linux/mutex.h>
	],[
		struct mutex lock;

		mutex_init(&lock);
		mutex_lock(&lock);
		mutex_unlock(&lock);
	], [], [$ZFS_META_LICENSE])
])

AC_DEFUN([ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC], [
	AC_MSG_CHECKING([whether mutex_lock() is GPL-only])
	ZFS_LINUX_TEST_RESULT([config_debug_lock_alloc_license], [
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_MSG_ERROR([
	*** Kernel built with CONFIG_DEBUG_LOCK_ALLOC which is incompatible
	*** with the CDDL license and will prevent the module linking stage
	*** from succeeding.  You must rebuild your kernel without this
	*** option enabled.])
	])
])

dnl #
dnl # Check CONFIG_TRIM_UNUSED_KSYMS
dnl #
dnl # Verify the kernel has CONFIG_TRIM_UNUSED_KSYMS disabled.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONFIG_TRIM_UNUSED_KSYMS], [
	ZFS_LINUX_TEST_SRC([config_trim_unusued_ksyms], [
		#if defined(CONFIG_TRIM_UNUSED_KSYMS)
		#error CONFIG_TRIM_UNUSED_KSYMS not defined
		#endif
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_CONFIG_TRIM_UNUSED_KSYMS], [
	AC_MSG_CHECKING([whether CONFIG_TRIM_UNUSED_KSYM is disabled])
	ZFS_LINUX_TEST_RESULT([config_trim_unusued_ksyms], [
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AS_IF([test "x$enable_linux_builtin" != xyes], [
			AC_MSG_ERROR([
	*** This kernel has unused symbols trimming enabled, please disable.
	*** Rebuild the kernel with CONFIG_TRIM_UNUSED_KSYMS=n set.])
		])
	])
])

dnl #
dnl # Check CONFIG_ZLIB_INFLATE
dnl #
dnl # Verify the kernel has CONFIG_ZLIB_INFLATE support enabled.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONFIG_ZLIB_INFLATE], [
	ZFS_LINUX_TEST_SRC([config_zlib_inflate], [
		#if !defined(CONFIG_ZLIB_INFLATE) && \
		    !defined(CONFIG_ZLIB_INFLATE_MODULE)
		#error CONFIG_ZLIB_INFLATE not defined
		#endif
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_CONFIG_ZLIB_INFLATE], [
	AC_MSG_CHECKING([whether CONFIG_ZLIB_INFLATE is defined])
	ZFS_LINUX_TEST_RESULT([config_zlib_inflate], [
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel does not include the required zlib inflate support.
	*** Rebuild the kernel with CONFIG_ZLIB_INFLATE=y|m set.])
	])
])

dnl #
dnl # Check CONFIG_ZLIB_DEFLATE
dnl #
dnl # Verify the kernel has CONFIG_ZLIB_DEFLATE support enabled.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CONFIG_ZLIB_DEFLATE], [
	ZFS_LINUX_TEST_SRC([config_zlib_deflate], [
		#if !defined(CONFIG_ZLIB_DEFLATE) && \
		    !defined(CONFIG_ZLIB_DEFLATE_MODULE)
		#error CONFIG_ZLIB_DEFLATE not defined
		#endif
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_CONFIG_ZLIB_DEFLATE], [
	AC_MSG_CHECKING([whether CONFIG_ZLIB_DEFLATE is defined])
	ZFS_LINUX_TEST_RESULT([config_zlib_deflate], [
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AC_MSG_ERROR([
	*** This kernel does not include the required zlib deflate support.
	*** Rebuild the kernel with CONFIG_ZLIB_DEFLATE=y|m set.])
	])
])
