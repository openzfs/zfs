dnl #
dnl # Default ZFS kernel configuration 
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL], [
	dnl # Kernel build make options
	dnl # KERNELMAKE_PARAMS="V=1"	# Enable verbose module build
	KERNELMAKE_PARAMS="V=1"

	dnl # Kernel build cpp flags
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DHAVE_SPL -D_KERNEL "
	KERNELCPPFLAGS="$KERNELCPPFLAGS -Wno-unknown-pragmas "
	KERNELCPPFLAGS="$KERNELCPPFLAGS -I$splsrc -I$splsrc/include -I$TOPDIR"

	dnl # Required for pread() functionality an other GNU goodness
	HOSTCFLAGS="$HOSTCFLAGS -ggdb -O2 -std=c99 "
	HOSTCFLAGS="$HOSTCFLAGS -D_GNU_SOURCE -D__EXTENSIONS__ "

	dnl # XXX: Quiet warnings not covered by the gcc-* patches
	dnl # XXX: Remove once all the warnings are resolved
	HOSTCFLAGS="$HOSTCFLAGS -Wno-switch -Wno-unused -Wno-missing-braces "
	HOSTCFLAGS="$HOSTCFLAGS -Wno-unknown-pragmas -Wno-parentheses "
	HOSTCFLAGS="$HOSTCFLAGS -Wno-uninitialized -fno-strict-aliasing "

	dnl # Expected defines not covered by zfs_config.h or spl_config.h
	HOSTCFLAGS="$HOSTCFLAGS -DHAVE_SPL -D_POSIX_PTHREAD_SEMANTICS "
	HOSTCFLAGS="$HOSTCFLAGS -D_FILE_OFFSET_BITS=64 "
	HOSTCFLAGS="$HOSTCFLAGS -D_LARGEFILE64_SOURCE -D_REENTRANT "
	HOSTCFLAGS="$HOSTCFLAGS -DTEXT_DOMAIN=\\\"zfs-linux-kernel\\\" "

	dnl # Expected default include path
	HOSTCFLAGS="$HOSTCFLAGS -I$TOPDIR "

	if test "$kernelbuild" != "$kernelsrc"; then
		KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$kernelbuild"
	fi

        AC_SUBST(KERNELMAKE_PARAMS)
        AC_SUBST(KERNELCPPFLAGS)
        AC_SUBST(HOSTCFLAGS)

	ZFS_AC_CONFIG_KERNEL_BIO_ARGS
])
