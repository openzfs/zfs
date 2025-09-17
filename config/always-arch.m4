dnl #
dnl # Set the target cpu architecture.  This allows the
dnl # following syntax to be used in a Makefile.am.
dnl #
dnl # if TARGET_CPU_POWERPC
dnl # ...
dnl # else
dnl # ...
dnl # endif
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_ARCH], [
	case $target_cpu in
	i?86)
		TARGET_CPU=i386
		;;
	amd64|x86_64)
		TARGET_CPU=x86_64
		;;
	powerpc*)
		TARGET_CPU=powerpc
		;;
	aarch64*)
		TARGET_CPU=aarch64
		;;
	armv*)
		TARGET_CPU=arm
		;;
	sparc64)
		TARGET_CPU=sparc64
		;;
	*)
		TARGET_CPU=$target_cpu
		;;
	esac

	AM_CONDITIONAL([TARGET_CPU_AARCH64], test $TARGET_CPU = aarch64)
	AM_CONDITIONAL([TARGET_CPU_I386],    test $TARGET_CPU = i386)
	AM_CONDITIONAL([TARGET_CPU_X86_64],  test $TARGET_CPU = x86_64)
	AM_CONDITIONAL([TARGET_CPU_POWERPC], test $TARGET_CPU = powerpc)
	AM_CONDITIONAL([TARGET_CPU_SPARC64], test $TARGET_CPU = sparc64)
	AM_CONDITIONAL([TARGET_CPU_ARM],     test $TARGET_CPU = arm)
])
dnl #
dnl # Check for conflicting environment variables
dnl #
dnl # If ARCH env variable is set up, then kernel Makefile in the /usr/src/kernel
dnl # can misbehave during the zfs ./configure test of the module compilation.
AC_DEFUN([ZFS_AC_CONFIG_CHECK_ARCH_VAR], [
	AC_MSG_CHECKING([for conflicting environment variables])
	if test -n "$ARCH"; then
		AC_MSG_RESULT([warning])
		AC_MSG_WARN(m4_normalize([ARCH environment variable is set to "$ARCH".
    This can cause build kernel modules support check failure.
    Please unset it.]))
	else
		AC_MSG_RESULT([done])
	fi
])

