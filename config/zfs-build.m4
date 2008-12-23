AC_DEFUN([ZFS_AC_KERNEL], [
	ver=`uname -r`

	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"; kernelbuild="$withval"])

	AC_ARG_WITH(linux-obj,
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
			*** with the '--with-linux=PATH' option])
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
        LINUX=${kernelsrc}
        LINUX_OBJ=${kernelbuild}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(kmoduledir)
])

AC_DEFUN([ZFS_AC_SPL], [

	AC_ARG_WITH([spl],
		AS_HELP_STRING([--with-spl=PATH],
		[Path to spl source]),
		[splsrc="$withval"; splbuild="$withval"])

	AC_ARG_WITH([spl-obj],
		AS_HELP_STRING([--with-spl-obj=PATH],
		[Path to spl build objects]),
		[splbuild="$withval"])


	AC_MSG_CHECKING([spl source directory])
	if test -z "$splsrc"; then
		splbuild=
		sourcelink=/tmp/`whoami`/spl
		buildlink=/tmp/`whoami`/spl

		if test -e $sourcelink; then
			splsrc=`(cd $sourcelink; /bin/pwd)`
		fi
		if test -e $buildlink; then
			splbuild=`(cd $buildlink; /bin/pwd)`
		fi
		if test -z "$splsrc"; then
			splsrc=$splbuild
		fi
	fi

	if test -z "$splsrc" -o -z "$splbuild"; then
		sourcelink=/lib/modules/${ver}/source
		buildlink=/lib/modules/${ver}/build

		if test -e $sourcelink; then
			splsrc=`(cd $sourcelink; /bin/pwd)`
		fi
		if test -e $buildlink; then
			splbuild=`(cd $buildlink; /bin/pwd)`
		fi
		if test -z "$splsrc"; then
			splsrc=$splbuild
		fi
		if test -z "$splsrc" -o -z "$splbuild"; then
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
			*** Please specify the location of the spl source
			*** with the '--with-spl=PATH' option])
		fi
	fi

	AC_MSG_RESULT([$splsrc])
	AC_MSG_CHECKING([spl build directory])
	AC_MSG_RESULT([$splbuild])

	AC_MSG_CHECKING([spl source version])
	if test -r $splbuild/spl_config.h && 
		fgrep -q VERSION $splbuild/spl_config.h; then

		splsrcver=`(echo "#include <spl_config.h>"; 
		            echo "splsrcver=VERSION") | 
		            cpp -I $splbuild |
	        	    grep "^splsrcver=" | cut -d \" -f 2`
	fi

	if test -z "$splsrcver"; then
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([
		*** Cannot determine the version of the spl source. 
		*** Please prepare the spl source before running this script])
	fi

	AC_MSG_RESULT([$splsrcver])

	AC_MSG_CHECKING([spl Module.symvers])
	if test -r $splbuild/modules/Module.symvers; then
		splsymvers=$splbuild/modules/Module.symvers
	elif test -r $kernelbuild/Module.symvers; then
		splsymvers=$kernelbuild/Module.symvers
	fi

	if test -z "$splsymvers"; then
	        AC_MSG_RESULT([Not found])
	        AC_MSG_ERROR([
                *** Cannot find extra Module.symvers in the spl source.
                *** Please prepare the spl source before running this script])
	fi

	AC_MSG_RESULT([$splsymvers])
	AC_SUBST(splsrc)
	AC_SUBST(splsymvers)
])

AC_DEFUN([ZFS_AC_LICENSE], [
        AC_MSG_CHECKING([license])
        AC_MSG_RESULT([CDDL])
dnl #        AC_DEFINE([HAVE_GPL_ONLY_SYMBOLS], [1],
dnl #                [Define to 1 if module is licensed under the GPL])
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
	echo "KERNELSRC=${LINUX}"         >>${SCRIPT_CONFIG}
	echo "KERNELBUILD=${LINUX_OBJ}"   >>${SCRIPT_CONFIG}
	echo "KERNELSRCVER=$kernsrcver"   >>${SCRIPT_CONFIG}
	echo                              >>${SCRIPT_CONFIG}
	echo "SPLSRC=$splsrc"             >>${SCRIPT_CONFIG}
	echo "SPLBUILD=$splbuild"         >>${SCRIPT_CONFIG}
	echo "SPLSRCVER=$splsrcver"       >>${SCRIPT_CONFIG}
	echo "SPLSYMVERS=$splsymvers"     >>${SCRIPT_CONFIG}
	echo                              >>${SCRIPT_CONFIG}
	echo "TOPDIR=${TOPDIR}"           >>${SCRIPT_CONFIG}
	echo "LIBDIR=${LIBDIR}"           >>${SCRIPT_CONFIG}
	echo "CMDDIR=${CMDDIR}"           >>${SCRIPT_CONFIG}
])

AC_DEFUN([ZFS_AC_CONFIG], [

	TOPDIR=`/bin/pwd`
	BUILDDIR=$TOPDIR
	LIBDIR=$TOPDIR/lib
	CMDDIR=$TOPDIR/cmd
	MODDIR=$TOPDIR/module
	UNAME=`uname -r | cut -d- -f1`

	AC_SUBST(UNAME)
	AC_SUBST(TOPDIR)
	AC_SUBST(BUILDDIR)
	AC_SUBST(LIBDIR)
	AC_SUBST(CMDDIR)
	AC_SUBST(MODDIR)
	AC_SUBST(UNAME)

	AC_ARG_WITH([zfs-config],
		AS_HELP_STRING([--with-config=CONFIG],
		[Config file 'kernel|user|all']),
		[zfsconfig="$withval"])

	AC_MSG_CHECKING([zfs config])
	AC_MSG_RESULT([$zfsconfig]);

	case "$zfsconfig" in
		kernel) ZFS_AC_CONFIG_KERNEL ;;
		user)	ZFS_AC_CONFIG_USER   ;;
		all)    ZFS_AC_CONFIG_KERNEL
			ZFS_AC_CONFIG_USER   ;;
		*)
		AC_MSG_RESULT([Error!])
		AC_MSG_ERROR([Bad value "$zfsconfig" for --with-config,
		              user kernel|user|all]) ;;
	esac

	ZFS_AC_CONFIG_SCRIPT
])

dnl #
dnl # ZFS_LINUX_CONFTEST
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST], [
cat >conftest.c <<_ACEOF
$1
_ACEOF
])

dnl #
dnl # ZFS_LANG_PROGRAM(C)([PROLOGUE], [BODY])
dnl #
m4_define([ZFS_LANG_PROGRAM], [
$1
int
main (void)
{
dnl Do *not* indent the following line: there may be CPP directives.
dnl Don't move the `;' right after for the same reason.
$2
  ;
  return 0;
}
])

dnl #
dnl # ZFS_LINUX_COMPILE_IFELSE / like AC_COMPILE_IFELSE
dnl #
AC_DEFUN([ZFS_LINUX_COMPILE_IFELSE], [
m4_ifvaln([$1], [ZFS_LINUX_CONFTEST([$1])])dnl
rm -f build/conftest.o build/conftest.mod.c build/conftest.ko build/Makefile
echo "obj-m := conftest.o" >build/Makefile
dnl AS_IF([AC_TRY_COMMAND(cp conftest.c build && make [$2] CC="$CC" -f $PWD/build/Makefile LINUXINCLUDE="-Iinclude -include include/linux/autoconf.h" -o tmp_include_depends -o scripts -o include/config/MARKER -C $LINUX_OBJ EXTRA_CFLAGS="-Werror-implicit-function-declaration $EXTRA_KCFLAGS" $ARCH_UM SUBDIRS=$PWD/build) >/dev/null && AC_TRY_COMMAND([$3])],
AS_IF([AC_TRY_COMMAND(cp conftest.c build && make [$2] CC="$CC" LINUXINCLUDE="-Iinclude -include include/linux/autoconf.h" -o tmp_include_depends -o scripts -o include/config/MARKER -C $LINUX_OBJ EXTRA_CFLAGS="-Werror-implicit-function-declaration $EXTRA_KCFLAGS" $ARCH_UM M=$PWD/build) >/dev/null && AC_TRY_COMMAND([$3])],
        [$4],
        [_AC_MSG_LOG_CONFTEST
m4_ifvaln([$5],[$5])dnl])dnl
rm -f build/conftest.o build/conftest.mod.c build/conftest.mod.o build/conftest.ko m4_ifval([$1], [build/conftest.c conftest.c])[]dnl
])

dnl #
dnl # ZFS_LINUX_TRY_COMPILE like AC_TRY_COMPILE
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE],
	[ZFS_LINUX_COMPILE_IFELSE(
        [AC_LANG_SOURCE([ZFS_LANG_PROGRAM([[$1]], [[$2]])])],
        [modules],
        [test -s build/conftest.o],
        [$3], [$4])
])

dnl #
dnl # ZFS_LINUX_CONFIG
dnl #
AC_DEFUN([ZFS_LINUX_CONFIG],
	[AC_MSG_CHECKING([whether Linux was built with CONFIG_$1])
	ZFS_LINUX_TRY_COMPILE([
		#ifndef AUTOCONF_INCLUDED
		#include <linux/config.h>
		#endif
	],[
		#ifndef CONFIG_$1
		#error CONFIG_$1 not #defined
		#endif
	],[
		AC_MSG_RESULT([yes])
		$2
	],[
		AC_MSG_RESULT([no])
		$3
	])
])

dnl #
dnl # ZFS_CHECK_SYMBOL_EXPORT
dnl # check symbol exported or not
dnl #
AC_DEFUN([ZFS_CHECK_SYMBOL_EXPORT],
	[AC_MSG_CHECKING([whether symbol $1 is exported])
	grep -q -E '[[[:space:]]]$1[[[:space:]]]' $LINUX/Module.symvers 2>/dev/null
	rc=$?
	if test $rc -ne 0; then
		export=0
		for file in $2; do
			grep -q -E "EXPORT_SYMBOL.*($1)" "$LINUX/$file" 2>/dev/null
			rc=$?
		        if test $rc -eq 0; then
		                export=1
		                break;
		        fi
		done
		if test $export -eq 0; then
			AC_MSG_RESULT([no])
			$4
		else
			AC_MSG_RESULT([yes])
			$3
		fi
	else
		AC_MSG_RESULT([yes])
		$3
	fi
])
