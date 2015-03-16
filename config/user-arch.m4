dnl #
dnl # Set the target arch for libspl atomic implementation
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_ARCH], [
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
	AC_MSG_RESULT([$TARGET_ASM_DIR])
	ZFS_AC_CONFIG_USER_INCLUDE_ASM_DIR
])

dnl #
dnl # Setup include/asm dir
dnl # Will copy every *.h from include/asm-generic to include/asm.
dnl # Then, copy every *.h from include/$TARGET_ASM_DIR
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_INCLUDE_ASM_DIR], [
	AC_MSG_CHECKING(Setup include/asm dir)

	test -d include/asm && rm include/asm/*.h
	mkdir -p include/asm
	test -d include/asm-generic && cp include/asm-generic/*.h include/asm
	test asm-generic != $TARGET_ASM_DIR && test -d include/$TARGET_ASM_DIR && cp include/$TARGET_ASM_DIR/*.h include/asm

	AC_MSG_RESULT([done])
])
