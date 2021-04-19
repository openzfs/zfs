dnl #
dnl # If -latomic exists, it's needed for __atomic intrinsics.
dnl #
dnl # Some systems (like FreeBSD 13) don't have a libatomic at all because
dnl # their toolchain doesn't ship it – they obviously don't need it.
dnl #
dnl # Others (like sufficiently ancient CentOS) have one,
dnl # but terminally broken or unlinkable (e.g. it's a dangling symlink,
dnl # or a linker script that points to a nonexistent file) –
dnl # most arches affected by this don't actually need -latomic (and if they do,
dnl # then they should have libatomic that actually exists and links,
dnl # so don't fall into this category).
dnl #
dnl # Technically, we could check if the platform *actually* needs -latomic,
dnl # or if it has native support for all the intrinsics we use,
dnl # but it /really/ doesn't matter, and C11 recommends to always link it.
dnl #
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBATOMIC], [
	AC_MSG_CHECKING([whether -latomic is present])

	saved_libs="$LIBS"
	LIBS="$LIBS -latomic"

	AC_LINK_IFELSE([AC_LANG_PROGRAM([], [])], [
		LIBATOMIC_LIBS="-latomic"
		AC_MSG_RESULT([yes])
	], [
		LIBATOMIC_LIBS=""
		AC_MSG_RESULT([no])
	])

	LIBS="$saved_libs"
	AC_SUBST([LIBATOMIC_LIBS])
])
