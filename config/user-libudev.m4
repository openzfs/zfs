dnl #
dnl # Check for libudev - needed for vdev auto-online and auto-replace
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBUDEV], [
	FIND_SYSTEM_LIBRARY(LIBUDEV, [libudev], [libudev.h], [], [udev], [], [user_libudev=yes], [user_libudev=no])

	AS_IF([test "x$user_libudev" = xyes], [
	    AX_SAVE_FLAGS

	    CFLAGS="$CFLAGS $LIBUDEV_CFLAGS"
	    LDFLAGS="$LDFLAGS $LIBUDEV_LIBS"

	    AC_CHECK_LIB([udev], [udev_device_get_is_initialized], [
	        AC_DEFINE([HAVE_LIBUDEV_UDEV_DEVICE_GET_IS_INITIALIZED], 1, [
	        Define if udev_device_get_is_initialized is available])], [])

	    AX_RESTORE_FLAGS
	])
])
