dnl #
dnl # macOS - attempt to find kernel headers. This is expected to
dnl # only run on mac platforms (using xcrun command) to iterate 
dnl # through versions of xcode, and xnu kernel source locations
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_MACOS_HEADERS], [
	AM_COND_IF([BUILD_MACOS], [
		AC_MSG_CHECKING([macOS kernel source directory])
		AS_IF([test -z "$kernelsrc"], [
			system_major_version=`sw_vers -productVersion | $AWK -F '.' '{ print $[]1 "." $[]2 }'`
			AS_IF([test -d "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX${system_major_version}.sdk/System/Library/Frameworks/Kernel.framework/Headers"], [
				kernelsrc="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX${system_major_version}.sdk/System/Library/Frameworks/Kernel.framework"])
		])
		AS_IF([test -z "$kernelsrc"], [
			AS_IF([test -d "/System/Library/Frameworks/Kernel.framework/Headers"], [
				kernelsrc="/System/Library/Frameworks/Kernel.framework"])
		])
		AS_IF([test -z "$kernelsrc"], [
			tmpdir=`xcrun --show-sdk-path`
			AS_IF([test -d "$tmpdir/System/Library/Frameworks/Kernel.framework/Headers"], [
				kernelsrc="$tmpdir/System/Library/Frameworks/Kernel.framework"])
		])
		AS_IF([test -z "$kernelsrc"], [
			AS_IF([test -d "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.10.sdk/System/Library/Frameworks/Kernel.framework"], [
				kernelsrc="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.10.sdk/System/Library/Frameworks/Kernel.framework"])
		])
		AS_IF([test -z "$kernelsrc"], [
			AS_IF([test -d "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk/System/Library/Frameworks/Kernel.framework"], [
				kernelsrc="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.11.sdk/System/Library/Frameworks/Kernel.framework"])
		])
		AS_IF([test -z "$kernelsrc"], [
			AS_IF([test -d "/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.9.sdk/System/Library/Frameworks/Kernel.framework"], [
				kernelsrc="/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX10.9.sdk/System/Library/Frameworks/Kernel.framework"])
		])

		AC_MSG_RESULT([$kernelsrc])

		AC_MSG_CHECKING([macOS kernel build directory])
		AS_IF([test -z "$kernelbuild"], [
				kernelbuild=${kernelsrc}
			])
		])
		AC_MSG_RESULT([$kernelbuild])

       AC_ARG_WITH([kernel-modprefix],
                AS_HELP_STRING([--with-kernel-modprefix=PATH],
                [Path to kernel module prefix]),
                [KERNEL_MODPREFIX="$withval"])
        AC_MSG_CHECKING([macOS kernel module prefix])
        AS_IF([test -z "$KERNEL_MODPREFIX"], [
                KERNEL_MODPREFIX="/Library/Extensions"
        ])
        AC_MSG_RESULT([$KERNEL_MODPREFIX])
        AC_DEFINE_UNQUOTED([KERNEL_MODPREFIX],
                ["$KERNEL_MODPREFIX"],
                [Path where the kernel module is installed.]
        )

		AC_MSG_CHECKING([macOS kernel source version])
		utsrelease1=$kernelbuild/Headers/libkern/version.h
		AS_IF([test -r $utsrelease1 && fgrep -q OSRELEASE $utsrelease1], [
			kernverfile=libkern/version.h ])

		AS_IF([test "$kernverfile"], [
			kernsrcver=`(echo "#include <$kernverfile>";
				echo "kernsrcver=OSRELEASE") |
				cpp -I$kernelbuild/Headers |
				grep "^kernsrcver=" | cut -d \" -f 2`

			AS_IF([test -z "$kernsrcver"], [
				AC_MSG_RESULT([Not found])
				AC_MSG_ERROR([*** Cannot determine kernel version.])
			])
		AC_MSG_RESULT([$kernsrcver])
		])

		AC_MSG_CHECKING([mach_kernel])
		AS_IF([test -z "$machkernel"], [
			AS_IF([test -e "/System/Library/Kernels/kernel"], [
				machkernel="/System/Library/Kernels/kernel" ] )
			AS_IF([test -e "/mach_kernel"], [
				machkernel="/mach_kernel" ] )
			AS_IF([test ! -f "$machkernel"], [
				AC_MSG_ERROR([
        *** mach_kernel file not found. For 10.9 and prior, this should be
        *** '/mach_kernel' and for 10.10 and following, this should be
        *** '/System/Library/Kernels/kernel'])
			])
		])
		AC_MSG_RESULT($machkernel)


dnl More Generic names:
	MACH_KERNEL=${machkernel}
	KERNEL_HEADERS=${kernelsrc}
	KERNEL_VERSION=${kernsrcver}
	AC_SUBST(KERNEL_HEADERS)
	AC_SUBST(KERNEL_MODPREFIX)
	AC_SUBST(KERNEL_VERSION)
	AC_SUBST(MACH_KERNEL)
	])
])

