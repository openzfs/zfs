dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Check whether assembler supports .cfi_negate_ra_state on AArch64.
dnl #

AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_TOOLCHAIN_CFI_PSEUDO_OP], [
	case "$host_cpu" in
	aarch64*)
		AC_CACHE_CHECK([whether assembler supports .cfi_negate_ra_state],
		    [zfs_cv_as_cfi_pseudo_op], [
			cat > conftest.S <<_ACEOF
	.text
conftest:
	.cfi_startproc
	.cfi_negate_ra_state
	ret
	.cfi_endproc
_ACEOF
			if AC_TRY_COMMAND([$CC -c $CFLAGS $CPPFLAGS conftest.S -o conftest.o]) >/dev/null 2>&1; then
				zfs_cv_as_cfi_pseudo_op=yes
			else
				zfs_cv_as_cfi_pseudo_op=no
			fi
			rm -f conftest.S conftest.o
		])

		AS_IF([test "x$zfs_cv_as_cfi_pseudo_op" = xyes], [
			AC_DEFINE([HAVE_AS_CFI_PSEUDO_OP], 1,
			    [Define if your assembler supports .cfi_negate_ra_state.])
		])
		;;
	esac
])
