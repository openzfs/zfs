dnl # SPDX-License-Identifier: CDDL-1.0
dnl #
dnl # Checks if host toolchain supports SIMD instructions
dnl #

dnl #
dnl # Each invocation of ZFS_AC_TOOLCHAIN_SIMD_CHECK(name, asmsrc) creates
dnl # two sets of macros:
dnl # - ZFS_AC_TOOLCHAIN_SIMD_<name>
dnl # - ZFS_AC_KERNEL_SRC_SIMD_<name> &  ZFS_AC_KERNEL_SIMD_<name>
dnl #
dnl # These try to compile the given <asmsrc> in a __asm__ directive, using
dnl # either the host or the kernel toolchains. Successful checks set
dnl # HAVE_TOOLCHAIN_<name> or HAVE_KERNEL_<name>, respectively.
dnl #
AC_DEFUN([ZFS_AC_SIMD_CHECK], [
	AC_DEFUN([ZFS_AC_TOOLCHAIN_SIMD_]m4_quote($1), [
		AC_MSG_CHECKING([whether host toolchain supports $1])
		AC_LINK_IFELSE([AC_LANG_SOURCE([[
			int main () {
				__asm__ __volatile__($2);
				return (0);
			}
		]])], [
			AC_DEFINE([HAVE_TOOLCHAIN_$1], 1,
			    [Define if host toolchain supports $1])
			AC_MSG_RESULT([yes])
		], [
			AC_MSG_RESULT([no])
		])
	])

	AC_DEFUN([ZFS_AC_KERNEL_SRC_SIMD_]m4_quote($1), [
		ZFS_LINUX_TEST_SRC(
		    [simd_]m4_quote(m4_translit([$1], [A-Z], [a-z])), [], [
			__asm__ __volatile__($2);
		])
	])
	AC_DEFUN([ZFS_AC_KERNEL_SIMD_]m4_quote($1), [
		AC_MSG_CHECKING([whether kernel toolchain supports $1])
		ZFS_LINUX_TEST_RESULT(
		    [simd_]m4_quote(m4_translit([$1], [A-Z], [a-z])), [
			AC_MSG_RESULT(yes)
			AC_DEFINE([HAVE_KERNEL_$1], 1,
			    [Define if kernel toolchain supports $1])
		], [
			AC_MSG_RESULT([no])
		])
	])

	dnl Stash the names of the new functions so we can execute them later.
	m4_pushdef([_zfs_ac_toolchain_simd_checks],
	    [ZFS_AC_TOOLCHAIN_SIMD_]m4_quote($1))
	m4_pushdef([_zfs_ac_kernel_src_simd_checks],
	    [ZFS_AC_KERNEL_SRC_SIMD_]m4_quote($1))
	m4_pushdef([_zfs_ac_kernel_simd_checks],
	    [ZFS_AC_KERNEL_SIMD_]m4_quote($1))
])

dnl # Invoke the macros created by ZFS_AC_TOOLCHAIN_SIMD_CHECK.
AC_DEFUN([ZFS_AC_TOOLCHAIN_SIMD], [
	m4_stack_foreach([_zfs_ac_toolchain_simd_checks], [m4_indir])
])
AC_DEFUN([ZFS_AC_KERNEL_SRC_SIMD], [
	m4_stack_foreach([_zfs_ac_kernel_src_simd_checks], [m4_indir])
])
AC_DEFUN([ZFS_AC_KERNEL_SIMD], [
	m4_stack_foreach([_zfs_ac_kernel_simd_checks], [m4_indir])
])

dnl # Instruction sets to test
ZFS_AC_SIMD_CHECK([SSE2],       ["pxor %xmm0, %xmm1"])
ZFS_AC_SIMD_CHECK([SSSE3],      ["pshufb %xmm0, %xmm1"])
ZFS_AC_SIMD_CHECK([SSE4_1],     ["pmaxsb %xmm0, %xmm1"])
ZFS_AC_SIMD_CHECK([AVX],        ["vmovdqa %ymm0, %ymm1"])
ZFS_AC_SIMD_CHECK([AVX2],       ["vpshufb %ymm0, %ymm1, %ymm2"])
ZFS_AC_SIMD_CHECK([AVX512F],    ["vpandd %zmm0, %zmm1, %zmm2"])
ZFS_AC_SIMD_CHECK([AVX512BW],   ["vpshufb %zmm0, %zmm1, %zmm2"])
ZFS_AC_SIMD_CHECK([AVX512VL],   ["vpabsq %zmm0,%zmm1"])
ZFS_AC_SIMD_CHECK([AES],        ["aesenc %xmm0, %xmm1"])
ZFS_AC_SIMD_CHECK([PCLMULQDQ],  ["pclmulqdq %0, %%xmm0, %%xmm1" :: "i"(0)])
ZFS_AC_SIMD_CHECK([MOVBE],      ["movbe 0(%eax), %eax"])
ZFS_AC_SIMD_CHECK([VAES],       ["vaesenc %ymm0, %ymm1, %ymm0"])
ZFS_AC_SIMD_CHECK([VPCLMULQDQ], ["vpclmulqdq %0, %%ymm4, %%ymm3, %%ymm5" :: "i"(0)])
ZFS_AC_SIMD_CHECK([SHA512EXT],  ["vsha512msg2 %ymm5, %ymm6"])
ZFS_AC_SIMD_CHECK([XSAVE],      ["xsave 0"])
ZFS_AC_SIMD_CHECK([XSAVEOPT],   ["xsaveopt 0"])
ZFS_AC_SIMD_CHECK([XSAVES],     ["xsaves 0"])
