dnl #
dnl # Default ZFS kernel mode configuration 
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG], [
	dnl # Kernel build make options
	KERNELMAKE_PARAMS=
	dnl #KERNELMAKE_PARAMS="V=1"	# Enable verbose module build

	dnl # Kernel build cpp flags
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DHAVE_SPL -D_KERNEL "
	KERNELCPPFLAGS="$KERNELCPPFLAGS -I$splsrc -I$splsrc/include -I$TOPDIR"

	dnl # Required for pread() functionality an other GNU goodness
	HOSTCFLAGS="$HOSTCFLAGS -ggdb -O2 -std=c99 "
	HOSTCFLAGS="$HOSTCFLAGS -D_GNU_SOURCE -D__EXTENSIONS__ "

	dnl # XXX: Quiet warnings not covered by the gcc-* patches
	dnl # XXX: Remove once all the warnings are resolved
	HOSTCFLAGS="$HOSTCFLAGS -Wno-switch -Wno-unused -Wno-missing-braces "
	HOSTCFLAGS="$HOSTCFLAGS -Wno-parentheses "
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

	dnl # XXX: I really, really hate this...  but to ensure the kernel
	dnl # build system compiles C files shared between a library and a 
	dnl # kernel module, we need to ensure each file has a unique make
	dnl # target.  To do that I'm creating symlinks for each shared
	dnl # file at configure time.  It may be possible something better
	dnl # can be done in the Makefile but it will take some serious
	dnl # investigation and I don't have the time now.

	echo "Creating symlinks for additional make targets"
	ln -f -s $LIBDIR/libport/u8_textprep.c      $LIBDIR/libport/ku8_textprep.c
	ln -f -s $LIBDIR/libavl/avl.c               $LIBDIR/libavl/kavl.c
	ln -f -s $LIBDIR/libavl/avl.c               $LIBDIR/libavl/uavl.c
	ln -f -s $LIBDIR/libnvpair/nvpair.c         $LIBDIR/libnvpair/knvpair.c
	ln -f -s $LIBDIR/libnvpair/nvpair.c         $LIBDIR/libnvpair/unvpair.c
	ln -f -s $LIBDIR/libzcommon/zfs_deleg.c     $LIBDIR/libzcommon/kzfs_deleg.c
	ln -f -s $LIBDIR/libzcommon/zfs_prop.c      $LIBDIR/libzcommon/kzfs_prop.c
	ln -f -s $LIBDIR/libzcommon/zprop_common.c  $LIBDIR/libzcommon/kzprop_common.c
	ln -f -s $LIBDIR/libzcommon/compress.c      $LIBDIR/libzcommon/kcompress.c
	ln -f -s $LIBDIR/libzcommon/list.c          $LIBDIR/libzcommon/klist.c
	ln -f -s $LIBDIR/libzcommon/zfs_namecheck.c $LIBDIR/libzcommon/kzfs_namecheck.c
	ln -f -s $LIBDIR/libzcommon/zfs_comutil.c   $LIBDIR/libzcommon/kzfs_comutil.c
	ln -f -s $LIBDIR/libzcommon/zpool_prop.c    $LIBDIR/libzcommon/kzpool_prop.c
	]
)
