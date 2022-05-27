dnl #
dnl # Check for available iov_iter functionality.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_VFS_IOV_ITER], [
	ZFS_LINUX_TEST_SRC([iov_iter_types], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		int type __attribute__ ((unused)) =
		    ITER_IOVEC | ITER_KVEC | ITER_BVEC | ITER_PIPE;
	])

	ZFS_LINUX_TEST_SRC([iov_iter_advance], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		size_t advance = 512;

		iov_iter_advance(&iter, advance);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_revert], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		size_t revert = 512;

		iov_iter_revert(&iter, revert);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_fault_in_readable], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		size_t size = 512;
		int error __attribute__ ((unused));

		error = iov_iter_fault_in_readable(&iter, size);
	])

	ZFS_LINUX_TEST_SRC([fault_in_iov_iter_readable], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		size_t size = 512;
		int error __attribute__ ((unused));

		error = fault_in_iov_iter_readable(&iter, size);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_count], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		size_t bytes __attribute__ ((unused));

		bytes = iov_iter_count(&iter);
	])

	ZFS_LINUX_TEST_SRC([copy_to_iter], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		char buf[512] = { 0 };
		size_t size = 512;
		size_t bytes __attribute__ ((unused));

		bytes = copy_to_iter((const void *)&buf, size, &iter);
	])

	ZFS_LINUX_TEST_SRC([copy_from_iter], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		char buf[512] = { 0 };
		size_t size = 512;
		size_t bytes __attribute__ ((unused));

		bytes = copy_from_iter((void *)&buf, size, &iter);
	])

	ZFS_LINUX_TEST_SRC([iov_iter_type], [
		#include <linux/fs.h>
		#include <linux/uio.h>
	],[
		struct iov_iter iter = { 0 };
		__attribute__((unused)) enum iter_type i = iov_iter_type(&iter);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_VFS_IOV_ITER], [
	enable_vfs_iov_iter="yes"

	AC_MSG_CHECKING([whether iov_iter types are available])
	ZFS_LINUX_TEST_RESULT([iov_iter_types], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_TYPES, 1,
		    [iov_iter types are available])
	],[
		AC_MSG_RESULT(no)
		enable_vfs_iov_iter="no"
	])

	AC_MSG_CHECKING([whether iov_iter_advance() is available])
	ZFS_LINUX_TEST_RESULT([iov_iter_advance], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_ADVANCE, 1,
		    [iov_iter_advance() is available])
	],[
		AC_MSG_RESULT(no)
		enable_vfs_iov_iter="no"
	])

	AC_MSG_CHECKING([whether iov_iter_revert() is available])
	ZFS_LINUX_TEST_RESULT([iov_iter_revert], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_REVERT, 1,
		    [iov_iter_revert() is available])
	],[
		AC_MSG_RESULT(no)
		enable_vfs_iov_iter="no"
	])

	AC_MSG_CHECKING([whether iov_iter_fault_in_readable() is available])
	ZFS_LINUX_TEST_RESULT([iov_iter_fault_in_readable], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_FAULT_IN_READABLE, 1,
		    [iov_iter_fault_in_readable() is available])
	],[
		AC_MSG_RESULT(no)

		AC_MSG_CHECKING([whether fault_in_iov_iter_readable() is available])
		ZFS_LINUX_TEST_RESULT([fault_in_iov_iter_readable], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_FAULT_IN_IOV_ITER_READABLE, 1,
			    [fault_in_iov_iter_readable() is available])
		],[
			AC_MSG_RESULT(no)
			enable_vfs_iov_iter="no"
		])
	])

	AC_MSG_CHECKING([whether iov_iter_count() is available])
	ZFS_LINUX_TEST_RESULT([iov_iter_count], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_IOV_ITER_COUNT, 1,
		    [iov_iter_count() is available])
	],[
		AC_MSG_RESULT(no)
		enable_vfs_iov_iter="no"
	])

	AC_MSG_CHECKING([whether copy_to_iter() is available])
	ZFS_LINUX_TEST_RESULT([copy_to_iter], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_COPY_TO_ITER, 1,
		    [copy_to_iter() is available])
	],[
		AC_MSG_RESULT(no)
		enable_vfs_iov_iter="no"
	])

	AC_MSG_CHECKING([whether copy_from_iter() is available])
	ZFS_LINUX_TEST_RESULT([copy_from_iter], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_COPY_FROM_ITER, 1,
		    [copy_from_iter() is available])
	],[
		AC_MSG_RESULT(no)
		enable_vfs_iov_iter="no"
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
])
