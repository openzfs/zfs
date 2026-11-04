dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Check if __atomic intrinsics are available in the compiler, either
dnl # directly or via -latomic.
dnl #

AC_DEFUN([_ZFS_AC_ATOMIC_SOURCE], [
	dnl #
	dnl # Test program for __atomic. We deliberately use uint64_t, which
	dnl # on most 32-bit architectures can't be inlined and will require a
	dnl # call out to libatomic.
	dnl #
	AC_LANG_SOURCE([[
		#include <stdint.h>
		uint64_t v;
		int main () {
			while (__atomic_load_n(&v, __ATOMIC_SEQ_CST)) {}
			__atomic_add_fetch(&v, 1, __ATOMIC_SEQ_CST);
			return (0);
		}
	]])
])

AC_DEFUN([ZFS_AC_CONFIG_USER_ATOMIC], [
	AC_MSG_CHECKING([if $CC has __atomic intrinsics])

	AC_LINK_IFELSE([_ZFS_AC_ATOMIC_SOURCE], [
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
		AC_MSG_CHECKING([if $CC has __atomic intrinsics in -latomic])

		LIBS="$LIBS -latomic"
		AC_LINK_IFELSE([_ZFS_AC_ATOMIC_SOURCE], [
			AC_MSG_RESULT([yes])
		], [
			AC_MSG_RESULT([no])
			AC_MSG_ERROR([No support for __atomic intrinsics. Can't build.])
		])
	])
])
