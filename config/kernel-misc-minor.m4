dnl #
dnl # Determine an available miscellaneous minor number which can be used
dnl # for the /dev/zfs device.  This is needed because kernel module
dnl # auto-loading depends on registering a reserved non-conflicting minor
dnl # number.  Start with a large known available unreserved minor and work
dnl # our way down to lower value if a collision is detected.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_MISC_MINOR], [
	AC_MSG_CHECKING([whether /dev/zfs minor is available])

	for i in $(seq 249 -1 200); do
		if ! grep -q "^#define\s\+.*_MINOR\s\+.*$i" \
		    ${LINUX}/include/linux/miscdevice.h; then
			ZFS_DEVICE_MINOR="$i"
			AC_MSG_RESULT($ZFS_DEVICE_MINOR)
			AC_DEFINE_UNQUOTED([ZFS_DEVICE_MINOR],
			    [$ZFS_DEVICE_MINOR], [/dev/zfs minor])
			break
		fi
	done

	AS_IF([ test -z "$ZFS_DEVICE_MINOR"], [
		AC_MSG_ERROR([
	*** No available misc minor numbers available for use.])
	])
])
