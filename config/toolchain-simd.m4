dnl #
dnl # Checks if host toolchain supports SIMD instructions
dnl #
AC_DEFUN([ZFS_AC_CONFIG_ALWAYS_TOOLCHAIN_SIMD], [
	case "$host_cpu" in
		x86_64 | x86 | i686)
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE2
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE3
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSSE3
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_1
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_2
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX
			ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX2
			;;
	esac
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE], [
	AC_MSG_CHECKING([whether host toolchain supports SSE])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("xorps %xmm0, %xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE], 1, [Define if host toolchain supports SSE])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE2
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE2], [
	AC_MSG_CHECKING([whether host toolchain supports SSE2])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pxor %xmm0, %xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE2], 1, [Define if host toolchain supports SSE2])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE3
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE3], [
	AC_MSG_CHECKING([whether host toolchain supports SSE3])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			char v[16];
			__asm__ __volatile__("lddqu %0,%%xmm0" :: "m"(v[0]));
		}
	]])], [
		AC_DEFINE([HAVE_SSE3], 1, [Define if host toolchain supports SSE3])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSSE3
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSSE3], [
	AC_MSG_CHECKING([whether host toolchain supports SSSE3])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pshufb %xmm0,%xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSSE3], 1, [Define if host toolchain supports SSSE3])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_1
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_1], [
	AC_MSG_CHECKING([whether host toolchain supports SSE4.1])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pmaxsb %xmm0,%xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE4_1], 1, [Define if host toolchain supports SSE4.1])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_2
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_SSE4_2], [
	AC_MSG_CHECKING([whether host toolchain supports SSE4.2])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			__asm__ __volatile__("pcmpgtq %xmm0, %xmm1");
		}
	]])], [
		AC_DEFINE([HAVE_SSE4_2], 1, [Define if host toolchain supports SSE4.2])
		AC_MSG_RESULT([yes])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX], [
	AC_MSG_CHECKING([whether host toolchain supports AVX])

	AC_LINK_IFELSE([AC_LANG_SOURCE([[
		void main()
		{
			char v[32];
			__asm__ __volatile__("vmovdqa %0,%%ymm0" :: "m"(v[0]));
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX], 1, [Define if host toolchain supports AVX])
	], [
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX2
dnl #
AC_DEFUN([ZFS_AC_CONFIG_TOOLCHAIN_CAN_BUILD_AVX2], [
	AC_MSG_CHECKING([whether host toolchain supports AVX2])

	AC_LINK_IFELSE([AC_LANG_SOURCE([
	[
		void main()
		{
			__asm__ __volatile__("vpshufb %ymm0,%ymm1,%ymm2");
		}
	]])], [
		AC_MSG_RESULT([yes])
		AC_DEFINE([HAVE_AVX2], 1, [Define if host toolchain supports AVX2])
	], [
		AC_MSG_RESULT([no])
	])
])
