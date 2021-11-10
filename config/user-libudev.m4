dnl #
dnl # Check for libudev - needed for vdev auto-online and auto-replace
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBUDEV], [
	ZFS_AC_FIND_SYSTEM_LIBRARY(LIBUDEV, [libudev], [libudev.h], [], [udev], [], [user_libudev=yes], [user_libudev=no])

	AS_IF([test "x$user_libudev" = xyes], [
	    AX_SAVE_FLAGS

	    CFLAGS="$CFLAGS $LIBUDEV_CFLAGS"
	    LIBS="$LIBUDEV_LIBS $LIBS"

	    AC_CHECK_FUNCS([udev_device_get_is_initialized])

	    AX_RESTORE_FLAGS
	])
])
