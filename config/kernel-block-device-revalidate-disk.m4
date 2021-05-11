dnl #
dnl # Linux 5.13 ABI, removal of the revalidate_disk .field
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_REVALIDATE_DISK], [
        ZFS_LINUX_TEST_SRC([block_device_revalidate_disk], [
                #include/linux/blkdev.h
        ],[
                static const struct revalidate_disk
                    ops __attribute__ ((unused)) = {
                        .revalidate_disk = NULL,
                };
        ])
])

AC_DEFUN([ZFS_AC_KERNEL_BLOCK_DEVICE_REVALIDATE_DISK], [
        AC_MSG_CHECKING([whether ops->revalidate_disk() exists])
        ZFS_LINUX_TEST_RESULT([block_device_revalidate_disk], [
                AC_MSG_RESULT(yes)
                AC_DEFINE(HAVE_BLOCK_DEVICE_REVALIDATE_DISK, 1,
                    [ops->revalidate_disk() exists])
        ],[
                AC_MSG_RESULT(no)
        ])
])
