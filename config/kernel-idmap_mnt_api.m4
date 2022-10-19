dnl #
dnl # 5.12 API
dnl #
dnl # Check if APIs for idmapped mount are available
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_IDMAP_MNT_API], [
        ZFS_LINUX_TEST_SRC([idmap_mnt_api], [
                #include <linux/fs.h>
        ],[
		int fs_flags = 0;
		fs_flags |= FS_ALLOW_IDMAP;
        ])
])

AC_DEFUN([ZFS_AC_KERNEL_IDMAP_MNT_API], [
        AC_MSG_CHECKING([whether APIs for idmapped mount are present])
        ZFS_LINUX_TEST_RESULT([idmap_mnt_api], [
                AC_MSG_RESULT([yes])
                AC_DEFINE(HAVE_IDMAP_MNT_API, 1,
                    [APIs for idmapped mount are present])
        ],[
                AC_MSG_RESULT([no])
        ])
])

