dnl #
dnl # Set the target arch for libspl atomic implementation and the icp
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_ARCH], [
	AC_MSG_CHECKING(for target asm dir)
	TARGET_ARCH=`echo ${target_cpu} | sed -e s/i.86/i386/`

	case $TARGET_ARCH in
	i386|x86_64)
		TARGET_ASM_DIR=asm-${TARGET_ARCH}
		;;
	*)
		TARGET_ASM_DIR=asm-generic
		;;
	esac

	AC_SUBST([TARGET_ASM_DIR])
	AM_CONDITIONAL([TARGET_ASM_X86_64], test $TARGET_ASM_DIR = asm-x86_64)
	AM_CONDITIONAL([TARGET_ASM_I386], test $TARGET_ASM_DIR = asm-i386)
	AM_CONDITIONAL([TARGET_ASM_GENERIC], test $TARGET_ASM_DIR = asm-generic)
	AC_MSG_RESULT([$TARGET_ASM_DIR])
])
