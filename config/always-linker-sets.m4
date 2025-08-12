dnl #
dnl # Checks if host toolchain supports ELF linker sets
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_LINKER_SETS], [
	AC_MSG_CHECKING([whether host toolchain can support ELF linker sets])
	AC_LINK_IFELSE([
		AC_LANG_SOURCE([[
			extern const void *__start_test_set;
			extern const void *__stop_test_set;
			static void *test_item
				__attribute__((__section__("test_set")))
				__attribute__((__used__)) = (void *)(0);
			int main() {
				for (const void **p = &__start_test_set;
				    p != &__stop_test_set; p++) { }
				return (0);
			}
		]])
	], [
		AC_DEFINE([HAVE_ELF_LINKER_SETS], 1,
			[host toolchain can support ELF linker sets])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])
