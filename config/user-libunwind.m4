dnl
dnl Checks for libunwind, which usually does a better job than backtrace() when
dnl resolving symbols in the stack backtrace. Newer versions have support for
dnl getting info about the object file the function came from, so we look for
dnl that too and use it if found.
dnl
AC_DEFUN([ZFS_AC_CONFIG_USER_LIBUNWIND], [
	AC_ARG_WITH([libunwind],
	    AS_HELP_STRING([--with-libunwind],
		[use libunwind for backtraces in userspace assertions]),
	    [],
	    [with_libunwind=auto])

	AS_IF([test "x$with_libunwind" != "xno"], [
		ZFS_AC_FIND_SYSTEM_LIBRARY(LIBUNWIND, [libunwind], [libunwind.h], [], [unwind], [], [
			dnl unw_get_elf_filename() is sometimes a macro, other
			dnl times a proper symbol, so we can't just do a link
			dnl check; we need to include the header properly.
			AX_SAVE_FLAGS
			CFLAGS="$CFLAGS $LIBUNWIND_CFLAGS"
			LIBS="$LIBS $LIBUNWIND_LIBS"
			AC_MSG_CHECKING([for unw_get_elf_filename in libunwind])
			AC_LINK_IFELSE([
				AC_LANG_PROGRAM([
					#define UNW_LOCAL_ONLY
					#include <libunwind.h>
				], [
					unw_get_elf_filename(0, 0, 0, 0);
				])
			], [
				AC_MSG_RESULT([yes])
				AC_DEFINE(HAVE_LIBUNWIND_ELF, 1,
				    [libunwind has unw_get_elf_filename])
			], [
				AC_MSG_RESULT([no])
			])
			dnl LLVM includes it's own libunwind library, which
			dnl defines the highest numbered register in a different
			dnl way, and has an incompatible unw_resname function.
			AC_MSG_CHECKING([whether libunwind is llvm libunwind])
			AC_COMPILE_IFELSE([
				AC_LANG_PROGRAM([
					#include <libunwind.h>
					#if !defined(_LIBUNWIND_HIGHEST_DWARF_REGISTER)
					#error "_LIBUNWIND_HIGHEST_DWARF_REGISTER is not defined"
					#endif
				], [])], [
					AC_MSG_RESULT([yes])
					AC_DEFINE(IS_LIBUNWIND_LLVM, 1, [libunwind is llvm libunwind])
				], [
					AC_MSG_RESULT([no])
			])
			AX_RESTORE_FLAGS
		], [
			AS_IF([test "x$with_libunwind" = "xyes"], [
				AC_MSG_FAILURE([--with-libunwind was given, but libunwind is not available, try installing libunwind-devel])
			])
		])
	])
])
