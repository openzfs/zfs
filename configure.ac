AC_INIT

AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE(spl, 0.2.2)
AC_CONFIG_HEADERS([config.h])

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LIBTOOL

kernelsrc=
kernelbuild=

AC_DEFUN([SPL_AC_KERNEL], [
	ver=`uname -r`

	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"; kernelbuild="$withval"])

	AC_ARG_WITH([linux-build],
		AS_HELP_STRING([--with-linux-obj=PATH],
		[Path to kernel build objects]),
		[kernelbuild="$withval"])

	AC_MSG_CHECKING([kernel source directory])
	if test -z "$kernelsrc"; then
		kernelbuild=
		sourcelink=/lib/modules/${ver}/source
		buildlink=/lib/modules/${ver}/build

		if test -e $sourcelink; then
			kernelsrc=`(cd $sourcelink; /bin/pwd)`
		fi
		if test -e $buildlink; then
			kernelbuild=`(cd $buildlink; /bin/pwd)`
		fi
		if test -z "$kernelsrc"; then
			kernelsrc=$kernelbuild
		fi
		if test -z "$kernelsrc" -o -z "$kernelbuild"; then
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
			*** Please specify the location of the kernel source
			*** with the '--with-kernel=PATH' option])
		fi
	fi

	AC_MSG_RESULT([$kernelsrc])
	AC_MSG_CHECKING([kernel build directory])
	AC_MSG_RESULT([$kernelbuild])

	AC_MSG_CHECKING([kernel source version])
	if test -r $kernelbuild/include/linux/version.h && 
		fgrep -q UTS_RELEASE $kernelbuild/include/linux/version.h; then

		kernsrcver=`(echo "#include <linux/version.h>"; 
		             echo "kernsrcver=UTS_RELEASE") | 
	        	     cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`

	elif test -r $kernelbuild/include/linux/utsrelease.h && 
		fgrep -q UTS_RELEASE $kernelbuild/include/linux/utsrelease.h; then

		kernsrcver=`(echo "#include <linux/utsrelease.h>"; 
		             echo "kernsrcver=UTS_RELEASE") | 
	        	     cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`
	fi

	if test -z "$kernsrcver"; then
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([
		*** Cannot determine the version of the linux kernel source.
		*** Please prepare the kernel before running this script])
	fi

	AC_MSG_RESULT([$kernsrcver])
	kmoduledir=${INSTALL_MOD_PATH}/lib/modules/$kernsrcver
	AC_SUBST(kernelsrc)
	AC_SUBST(kmoduledir)
])

AC_DEFUN([SPL_AC_DEBUG], [
	AC_MSG_CHECKING([whether debugging is enabled])
	AC_ARG_ENABLE( [debug],
		AS_HELP_STRING([--enable-debug],
		[Enable generic debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug=yes ;;
			no) spl_ac_debug=no ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug]) ;;
		esac ]
	) 
	if test "$spl_ac_debug" = yes; then
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG"
	else
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG"
		AC_DEFINE([NDEBUG], [1],
		[Define to 1 to disable debug tracing])
	fi
	AC_MSG_RESULT([${spl_ac_debug=no}])
])

AC_DEFUN([SPL_AC_DEBUG_KMEM], [
	AC_MSG_CHECKING([whether kmem debugging is enabled])
	AC_ARG_ENABLE( [debug-kmem],
		AS_HELP_STRING([--enable-debug-kmem],
		[Enable kmem debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug=yes ;;
			no) spl_ac_debug=no ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-kmem]) ;;
		esac ]
	) 
	if test "$spl_ac_debug" = yes; then
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM"
		AC_DEFINE([DEBUG_KMEM], [1],
		[Define to 1 to enable kmem debugging])
	fi
	AC_MSG_RESULT([${spl_ac_debug=no}])
])

AC_DEFUN([SPL_AC_DEBUG_MUTEX], [
	AC_MSG_CHECKING([whether mutex debugging is enabled])
	AC_ARG_ENABLE( [debug-mutex],
		AS_HELP_STRING([--enable-debug-mutex],
		[Enable mutex debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug=yes ;;
			no) spl_ac_debug=no ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-mutex]) ;;
		esac ]
	) 
	if test "$spl_ac_debug" = yes; then
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_MUTEX"
		AC_DEFINE([DEBUG_MUTEX], [1],
		[Define to 1 to enable mutex debugging])
	fi
	AC_MSG_RESULT([${spl_ac_debug=no}])
])

AC_DEFUN([SPL_AC_DEBUG_KSTAT], [
	AC_MSG_CHECKING([whether kstat debugging is enabled])
	AC_ARG_ENABLE( [debug-kstat],
		AS_HELP_STRING([--enable-debug-kstat],
		[Enable kstat debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug=yes ;;
			no) spl_ac_debug=no ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-kstat]) ;;
		esac ]
	) 
	if test "$spl_ac_debug" = yes; then
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KSTAT"
		AC_DEFINE([DEBUG_KSTAT], [1],
		[Define to 1 to enable kstat debugging])
	fi
	AC_MSG_RESULT([${spl_ac_debug=no}])
])

AC_DEFUN([SPL_AC_DEBUG_CALLB], [
	AC_MSG_CHECKING([whether callb debugging is enabled])
	AC_ARG_ENABLE( [debug-callb],
		AS_HELP_STRING([--enable-debug-callb],
		[Enable callb debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug=yes ;;
			no) spl_ac_debug=no ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-callb]) ;;
		esac ]
	) 
	if test "$spl_ac_debug" = yes; then
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_CALLB"
		AC_DEFINE([DEBUG_CALLB], [1],
		[Define to 1 to enable callb debugging])
	fi
	AC_MSG_RESULT([${spl_ac_debug=no}])
])

SPL_AC_KERNEL
SPL_AC_DEBUG
SPL_AC_DEBUG_KMEM
SPL_AC_DEBUG_MUTEX
SPL_AC_DEBUG_KSTAT
SPL_AC_DEBUG_CALLB

TOPDIR=`/bin/pwd`

# Add "V=1" to KERNELMAKE_PARAMS to enable verbose module build
KERNELMAKE_PARAMS=
KERNELCPPFLAGS="${KERNELCPPFLAGS} -I$TOPDIR -I$TOPDIR/include"

if test "$kernelbuild" != "$kernelsrc"; then
	KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$kernelbuild"
fi

AC_SUBST(KERNELMAKE_PARAMS)
AC_SUBST(KERNELCPPFLAGS)
AC_SUBST(KERNELCFLAGS)

AC_CONFIG_FILES([ Makefile
                  lib/Makefile
                  cmd/Makefile
                  modules/Makefile
                  modules/spl/Makefile
                  modules/splat/Makefile
                  include/Makefile
                  include/sys/Makefile
                  scripts/Makefile
                  scripts/spl.spec
               ])

AC_OUTPUT
