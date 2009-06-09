dnl #
dnl # Set the target arch for libspl atomic implementation
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_ARCH], [
	AC_MSG_CHECKING(for target arch)
	TARGET_ARCH=`echo ${target_cpu} | sed -e s/i.86/i386/`
	TARGET_ARCH_DIR=asm-$TARGET_ARCH
        AC_MSG_RESULT([$TARGET_ARCH])

	case $TARGET_ARCH in
	i386|x86_64|powerpc64)
		AC_SUBST([TARGET_ARCH])
		AC_SUBST([TARGET_ARCH_DIR])
		;;
	*)
                AC_MSG_ERROR([
		*** Unsupported architecture $TARGET_ARCH
		*** Available architectures: x86, x86_64, powerpc64])
		;;
	esac
])
