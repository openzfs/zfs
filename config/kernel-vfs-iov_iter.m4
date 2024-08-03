dnl #
dnl # Check for available iov_iter functionality.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_IOV_ITER], [
	ZFS_LINUX_TEST_SRC([fault_in_iov_iter_readable], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		size_t size = 512;
		int error __attribute__ ((unused));

		error = fault_in_iov_iter_readable(&iter, size);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_get_pages2], [
		#include <linux/uio.h>
	], [
		struct iov_iter iter = { 0 };
		struct page **pages = NULL;
		size_t maxsize = 4096;
		unsigned maxpages = 1;
		size_t start;
		size_t ret __attribute__ ((unused));

		ret = iov_iter_get_pages2(&iter, pages, maxsize, maxpages,
		    &start);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_get_pages], [
		#include <linux/uio.h>
	], [
		struct iov_iter iter = { 0 };
		struct page **pages = NULL;
		size_t maxsize = 4096;
		unsigned maxpages = 1;
		size_t start;
		size_t ret __attribute__ ((unused));

		ret = iov_iter_get_pages(&iter, pages, maxsize, maxpages,
		    &start);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_type], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		__attribute__((unused)) enum iter_type i = iov_iter_type(&iter);
	])

	ZFS_LINUX_TEST_SRC([iter_iov], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		__attribute__((unused)) const struct iovec *iov = iter_iov(&iter);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_IOV_ITER], [
	enable_vfs_iov_iter="yes"

	AC_MSG_CHECKING([whether fault_in_iov_iter_readable() is available])
	ZFS_LINUX_TEST_RESULT([fault_in_iov_iter_readable], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FAULT_IN_IOV_ITER_READABLE, 1,
		    [fault_in_iov_iter_readable() is available])
	],[
		AC_MSG_RESULT(no)
	])

	dnl #
	dnl # Kernel 6.0 changed iov_iter_get_pages() to iov_iter_page_pages2().
	dnl #
	AC_MSG_CHECKING([whether iov_iter_get_pages2() is available])
	ZFS_LINUX_TEST_RESULT([iov_iter_get_pages2], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_GET_PAGES2, 1,
		    [iov_iter_get_pages2() is available])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether iov_iter_get_pages() is available])
			ZFS_LINUX_TEST_RESULT([iov_iter_get_pages], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_IOV_ITER_GET_PAGES, 1,
			    [iov_iter_get_pages() is available])
		], [
			AC_MSG_RESULT(no)
			enable_vfs_iov_iter="no"
		])
	])

	dnl #
	dnl # This checks for iov_iter_type() in linux/uio.h. It is not
	dnl # required, however, and the module will compiled without it
	dnl # using direct access of the member attribute
	dnl #
	AC_MSG_CHECKING([whether iov_iter_type() is available])
	ZFS_LINUX_TEST_RESULT([iov_iter_type], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_TYPE, 1,
		    [iov_iter_type() is available])
	],[
		AC_MSG_RESULT(no)
	])

	dnl #
	dnl # As of the 4.9 kernel support is provided for iovecs, kvecs,
	dnl # bvecs and pipes in the iov_iter structure.  As long as the
	dnl # other support interfaces are all available the iov_iter can
	dnl # be correctly used in the uio structure.
	dnl #
	AS_IF([test "x$enable_vfs_iov_iter" = "xyes"], [
		AC_DEFINE(HAVE_VFS_IOV_ITER, 1,
		    [All required iov_iter interfaces are available])
	])

	dnl #
	dnl # Kernel 6.5 introduces the iter_iov() function that returns the
	dnl # __iov member of an iov_iter*. The iov member was renamed to this
	dnl # __iov member, and is intended to be accessed via the helper
	dnl # function now.
	dnl #
	AC_MSG_CHECKING([whether iter_iov() is available])
	ZFS_LINUX_TEST_RESULT([iter_iov], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ITER_IOV, 1,
		    [iter_iov() is available])
	],[
		AC_MSG_RESULT(no)
	])
])
