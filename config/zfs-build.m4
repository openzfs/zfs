AC_DEFUN([ZFS_AC_LICENSE], [
	AC_MSG_CHECKING([zfs license])
	LICENSE=`grep MODULE_LICENSE module/zfs/zfs_ioctl.c | cut -f2 -d'"'`
	AC_MSG_RESULT([$LICENSE])
	if test "$LICENSE" = GPL; then
		AC_DEFINE([HAVE_GPL_ONLY_SYMBOLS], [1],
		          [Define to 1 if module is licensed under the GPL])
	fi

	AC_SUBST(LICENSE)
])

AC_DEFUN([ZFS_AC_DEBUG], [
	AC_MSG_CHECKING([whether debugging is enabled])
	AC_ARG_ENABLE( [debug],
		AS_HELP_STRING([--enable-debug],
		[Enable generic debug support (default off)]),
		[ case "$enableval" in
			yes) zfs_ac_debug=yes ;;
			no)  zfs_ac_debug=no  ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug]) ;;
		esac ]
)
if test "$zfs_ac_debug" = yes; then
	AC_MSG_RESULT([yes])
		AC_DEFINE([DEBUG], [1],
		[Define to 1 to enable debug tracing])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG "
		HOSTCFLAGS="${HOSTCFLAGS} -DDEBUG "
		USERDEBUG="-DDEBUG"
	else
		AC_MSG_RESULT([no])
		AC_DEFINE([NDEBUG], [1],
		[Define to 1 to disable debug tracing])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG "
		HOSTCFLAGS="${HOSTCFLAGS} -DNDEBUG "
		USERDEBUG="-DNDEBUG"
	fi

	AC_SUBST(USERDEBUG)
])

AC_DEFUN([ZFS_AC_CONFIG_SCRIPT], [
	cat >.script-config <<EOF
KERNELSRC=${LINUX}
KERNELBUILD=${LINUX_OBJ}
KERNELSRCVER=${LINUX_VERSION}
KERNELMOD=/lib/modules/\${KERNELSRCVER}/kernel

SPLSRC=${SPL}
SPLBUILD=${SPL_OBJ}
SPLSRCVER=${SPL_VERSION}

TOPDIR=${TOPDIR}
BUILDDIR=${BUILDDIR}
LIBDIR=${LIBDIR}
CMDDIR=${CMDDIR}
MODDIR=${MODDIR}
SCRIPTDIR=${SCRIPTDIR}
UDEVDIR=\${TOPDIR}/scripts/udev-rules
ZPOOLDIR=\${TOPDIR}/scripts/zpool-config
ZPIOSDIR=\${TOPDIR}/scripts/zpios-test
ZPIOSPROFILEDIR=\${TOPDIR}/scripts/zpios-profile

ZDB=\${CMDDIR}/zdb/zdb
ZFS=\${CMDDIR}/zfs/zfs
ZINJECT=\${CMDDIR}/zinject/zinject
ZPOOL=\${CMDDIR}/zpool/zpool
ZTEST=\${CMDDIR}/ztest/ztest
ZPIOS=\${CMDDIR}/zpios/zpios

COMMON_SH=\${SCRIPTDIR}/common.sh
ZFS_SH=\${SCRIPTDIR}/zfs.sh
ZPOOL_CREATE_SH=\${SCRIPTDIR}/zpool-create.sh
ZPIOS_SH=\${SCRIPTDIR}/zpios.sh
ZPIOS_SURVEY_SH=\${SCRIPTDIR}/zpios-survey.sh

LDMOD=/sbin/insmod

KERNEL_MODULES=(                                      \\
        \${KERNELMOD}/lib/zlib_deflate/zlib_deflate.ko \\
)

SPL_MODULES=(                                         \\
        \${SPLBUILD}/spl/spl.ko                        \\
)

ZFS_MODULES=(                                         \\
        \${MODDIR}/avl/zavl.ko                         \\
        \${MODDIR}/nvpair/znvpair.ko                   \\
        \${MODDIR}/unicode/zunicode.ko                 \\
        \${MODDIR}/zcommon/zcommon.ko                  \\
        \${MODDIR}/zfs/zfs.ko                          \\
)

ZPIOS_MODULES=(                                       \\
        \${MODDIR}/zpios/zpios.ko                      \\
)

MODULES=(                                             \\
        \${KERNEL_MODULES[[*]]}                          \\
        \${SPL_MODULES[[*]]}                             \\
        \${ZFS_MODULES[[*]]}                             \\
)
EOF
])

AC_DEFUN([ZFS_AC_CONFIG], [
	TOPDIR=`readlink -f ${srcdir}`
	BUILDDIR=$TOPDIR
	LIBDIR=$TOPDIR/lib
	CMDDIR=$TOPDIR/cmd
	MODDIR=$TOPDIR/module
	SCRIPTDIR=$TOPDIR/scripts
	TARGET_ASM_DIR=asm-generic

	AC_SUBST(TOPDIR)
	AC_SUBST(BUILDDIR)
	AC_SUBST(LIBDIR)
	AC_SUBST(CMDDIR)
	AC_SUBST(MODDIR)
	AC_SUBST(SCRIPTDIR)
	AC_SUBST(TARGET_ASM_DIR)

	ZFS_CONFIG=all
	AC_ARG_WITH([config],
		AS_HELP_STRING([--with-config=CONFIG],
		[Config file 'kernel|user|all|srpm']),
		[ZFS_CONFIG="$withval"])

	AC_MSG_CHECKING([zfs config])
	AC_MSG_RESULT([$ZFS_CONFIG]);
	AC_SUBST(ZFS_CONFIG)

	case "$ZFS_CONFIG" in
		kernel) ZFS_AC_CONFIG_KERNEL ;;
		user)	ZFS_AC_CONFIG_USER   ;;
		all)    ZFS_AC_CONFIG_KERNEL
			ZFS_AC_CONFIG_USER   ;;
		srpm)                        ;;
		*)
		AC_MSG_RESULT([Error!])
		AC_MSG_ERROR([Bad value "$ZFS_CONFIG" for --with-config,
		              user kernel|user|all|srpm]) ;;
	esac

	AM_CONDITIONAL([CONFIG_USER],
	               [test "$ZFS_CONFIG" = user] ||
	               [test "$ZFS_CONFIG" = all])
	AM_CONDITIONAL([CONFIG_KERNEL],
	               [test "$ZFS_CONFIG" = kernel] ||
	               [test "$ZFS_CONFIG" = all])

	ZFS_AC_CONFIG_SCRIPT
])
