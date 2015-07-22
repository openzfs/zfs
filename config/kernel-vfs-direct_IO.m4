dnl #
dnl # Linux 4.1.x API change
dnl #
AC_DEFUN([ZFS_AC_KERNEL_VFS_DIRECT_IO], [
	AC_MSG_CHECKING([whether fops->direct_IO() uses iov_iter without rw])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		ssize_t test_direct_IO(struct kiocb *kiocb,
		    struct iov_iter *iter, loff_t offset)
			{ return 0; }

		static const struct address_space_operations
		    fops __attribute__ ((unused)) = {
		    .direct_IO = test_direct_IO,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER, 1,
			[fops->direct_IO() uses iov_iter without rw])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # Linux 3.16.x API change
		dnl #
		AC_MSG_CHECKING([whether fops->direct_IO() uses iov_iter with rw])
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/fs.h>

			ssize_t test_direct_IO(int rw, struct kiocb *kiocb,
			    struct iov_iter *iter, loff_t offset)
				{ return 0; }

			static const struct address_space_operations
			    fops __attribute__ ((unused)) = {
			    .direct_IO = test_direct_IO,
			};
		],[
		],[
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_VFS_DIRECT_IO_ITER_RW, 1,
				[fops->direct_IO() uses iov_iter with rw])
		],[
			AC_MSG_RESULT(no)
			dnl #
			dnl # Ancient Linux API (predates git)
			dnl #
			AC_MSG_CHECKING([whether fops->direct_IO() uses iovec])
			ZFS_LINUX_TRY_COMPILE([
				#include <linux/fs.h>
				ssize_t test_direct_IO(int rw,
				    struct kiocb *kiocb,
				    const struct iovec *iov, loff_t offset,
				    unsigned long nr_segs)
					{ return 0; }

				static const struct address_space_operations
				    fops __attribute__ ((unused)) = {
				    .direct_IO = test_direct_IO,
				};
			],[
			],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_VFS_DIRECT_IO_IOVEC, 1,
					[fops->direct_IO() uses iovec])
			],[
				AC_MSG_ERROR(no)
			])
		])
	])
])
