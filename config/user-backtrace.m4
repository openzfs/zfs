dnl
dnl backtrace(), for userspace assertions. glibc has this directly in libc.
dnl FreeBSD and (sometimes) musl have it in a separate -lexecinfo. It's assumed
dnl that this will also get the companion function backtrace_symbols().
dnl
AC_DEFUN([ZFS_AC_CONFIG_USER_BACKTRACE], [
	AX_SAVE_FLAGS
	LIBS=""
	AC_SEARCH_LIBS([backtrace], [execinfo], [
		AC_DEFINE(HAVE_BACKTRACE, 1, [backtrace() is available])
		AC_SUBST([BACKTRACE_LIBS], ["$LIBS"])
	])
	AX_RESTORE_FLAGS
])
