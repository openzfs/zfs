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
                KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG "
		HOSTCFLAGS="${HOSTCFLAGS} -DDEBUG "
        else
        	AC_MSG_RESULT([no])
                AC_DEFINE([NDEBUG], [1],
                [Define to 1 to disable debug tracing])
                KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG "
		HOSTCFLAGS="${HOSTCFLAGS} -DNDEBUG "
        fi
])

AC_DEFUN([ZFS_AC_CONFIG_SCRIPT], [
	SCRIPT_CONFIG=.script-config
	rm -f ${SCRIPT_CONFIG}
	echo "KERNELSRC=${LINUX}"             >>${SCRIPT_CONFIG}
	echo "KERNELBUILD=${LINUX_OBJ}"       >>${SCRIPT_CONFIG}
	echo "KERNELSRCVER=${LINUX_VERSION}"  >>${SCRIPT_CONFIG}
	echo                                  >>${SCRIPT_CONFIG}
	echo "SPLSRC=${SPL}"                  >>${SCRIPT_CONFIG}
	echo "SPLBUILD=${SPL_OBJ}"            >>${SCRIPT_CONFIG}
	echo "SPLSRCVER=${SPL_VERSION}"       >>${SCRIPT_CONFIG}
	echo                                  >>${SCRIPT_CONFIG}
	echo "TOPDIR=${TOPDIR}"               >>${SCRIPT_CONFIG}
	echo "BUILDDIR=${BUILDDIR}"           >>${SCRIPT_CONFIG}
	echo "LIBDIR=${LIBDIR}"               >>${SCRIPT_CONFIG}
	echo "CMDDIR=${CMDDIR}"               >>${SCRIPT_CONFIG}
	echo "MODDIR=${MODDIR}"               >>${SCRIPT_CONFIG}
])

AC_DEFUN([ZFS_AC_CONFIG], [
	TOPDIR=`readlink -f ${srcdir}`
	BUILDDIR=$TOPDIR
	LIBDIR=$TOPDIR/lib
	CMDDIR=$TOPDIR/cmd
	MODDIR=$TOPDIR/module

	AC_SUBST(TOPDIR)
	AC_SUBST(BUILDDIR)
	AC_SUBST(LIBDIR)
	AC_SUBST(CMDDIR)
	AC_SUBST(MODDIR)

	ZFS_CONFIG=all
	AC_ARG_WITH([config],
		AS_HELP_STRING([--with-config=CONFIG],
		[Config file 'kernel|user|all']),
		[ZFS_CONFIG="$withval"])

	AC_MSG_CHECKING([zfs config])
	AC_MSG_RESULT([$ZFS_CONFIG]);
	AC_SUBST(ZFS_CONFIG)

	case "$ZFS_CONFIG" in
		kernel) ZFS_AC_CONFIG_KERNEL ;;
		user)	ZFS_AC_CONFIG_USER   ;;
		all)    ZFS_AC_CONFIG_KERNEL
			ZFS_AC_CONFIG_USER   ;;
		*)
		AC_MSG_RESULT([Error!])
		AC_MSG_ERROR([Bad value "$ZFS_CONFIG" for --with-config,
		              user kernel|user|all]) ;;
	esac

	AM_CONDITIONAL([CONFIG_USER],
	               [test "$ZFS_CONFIG" = user] ||
	               [test "$ZFS_CONFIG" = all])
	AM_CONDITIONAL([CONFIG_KERNEL],
	               [test "$ZFS_CONFIG" = kernel] ||
	               [test "$ZFS_CONFIG" = all])

	ZFS_AC_CONFIG_SCRIPT
])
