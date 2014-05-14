AC_DEFUN([ZFS_AC_CONFIG_X86_64_ASM], [
	ZFS_AC_CONFIG_X86_64_AVX
	ZFS_AC_CONFIG_X86_64_AVX2
])

AC_DEFUN([ZFS_AC_CONFIG_X86_64_AVX], [
	AC_MSG_CHECKING(for x86_64 avx)
	AC_TRY_COMPILE(
	[
	],[
		asm volatile("vmovdqa %ymm0,%ymm1");
	],[
		AC_MSG_RESULT(yes)
		USER_AS_FLAGS="$USER_AS_FLAGS -DCONFIG_AS_AVX"
		AC_DEFINE(HAVE_X86_64_AVX, 1,
		[avx is available])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_CONFIG_X86_64_AVX2], [
	AC_MSG_CHECKING(for x86_64 avx2)
	AC_TRY_COMPILE(
	[
	],[
		asm volatile("vpabsd %ymm0,%ymm1");
	],[
		AC_MSG_RESULT(yes)
		USER_AS_FLAGS="$USER_AS_FLAGS -DCONFIG_AS_AVX2"
		AC_DEFINE(HAVE_X86_64_AVX2, 1,
		[avx is available])
	],[
		AC_MSG_RESULT(no)
	])
])
