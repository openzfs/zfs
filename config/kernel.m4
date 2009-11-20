dnl #
dnl # Default ZFS kernel configuration 
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL], [
	ZFS_AC_KERNEL
	ZFS_AC_SPL
	ZFS_AC_KERNEL_BDEV_BLOCK_DEVICE_OPERATIONS
	ZFS_AC_KERNEL_OPEN_BDEV_EXCLUSIVE
	ZFS_AC_KERNEL_INVALIDATE_BDEV_ARGS
	ZFS_AC_KERNEL_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BIO_EMPTY_BARRIER
	ZFS_AC_KERNEL_BIO_END_IO_T_ARGS
	ZFS_AC_KERNEL_BIO_RW_SYNCIO
	ZFS_AC_KERNEL_BLK_END_REQUEST
	ZFS_AC_KERNEL_BLK_FETCH_REQUEST
	ZFS_AC_KERNEL_BLK_REQUEUE_REQUEST
	ZFS_AC_KERNEL_BLK_RQ_BYTES
	ZFS_AC_KERNEL_BLK_RQ_POS
	ZFS_AC_KERNEL_BLK_RQ_SECTORS
	ZFS_AC_KERNEL_GET_DISK_RO
	ZFS_AC_KERNEL_RQ_IS_SYNC
	ZFS_AC_KERNEL_RQ_FOR_EACH_SEGMENT

	dnl # Kernel build make options
	dnl # KERNELMAKE_PARAMS="V=1"	# Enable verbose module build
	KERNELMAKE_PARAMS=

	dnl # -Wall -fno-strict-aliasing -Wstrict-prototypes and other
	dnl # compiler options are added by the kernel build system.
	KERNELCPPFLAGS="$KERNELCPPFLAGS -Werror -DHAVE_SPL -D_KERNEL"
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DTEXT_DOMAIN=\\\"zfs-linux-kernel\\\""
	KERNELCPPFLAGS="$KERNELCPPFLAGS -I$TOPDIR -I$SPL -I$SPL/include"

	if test "$LINUX_OBJ" != "$LINUX"; then
		KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$LINUX_OBJ"
	fi

	AC_SUBST(KERNELMAKE_PARAMS)
	AC_SUBST(KERNELCPPFLAGS)
])

dnl #
dnl # Detect name used more Module.symvers file
dnl #
AC_DEFUN([ZFS_AC_MODULE_SYMVERS], [
	modpost=$LINUX/scripts/Makefile.modpost
	AC_MSG_CHECKING([kernel file name for module symbols])
	if test -f "$modpost"; then
		if grep -q Modules.symvers $modpost; then
			LINUX_SYMBOLS=Modules.symvers
		else
			LINUX_SYMBOLS=Module.symvers
		fi
	else
		LINUX_SYMBOLS=NONE
	fi
	AC_MSG_RESULT($LINUX_SYMBOLS)
	AC_SUBST(LINUX_SYMBOLS)
])

dnl #
dnl # Detect the kernel to be built against
dnl #
AC_DEFUN([ZFS_AC_KERNEL], [
	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"])

	AC_ARG_WITH(linux-obj,
		AS_HELP_STRING([--with-linux-obj=PATH],
		[Path to kernel build objects]),
		[kernelbuild="$withval"])

	AC_MSG_CHECKING([kernel source directory])
	if test -z "$kernelsrc"; then
		sourcelink=`ls -1d /usr/src/kernels/* /usr/src/linux-* \
		            2>/dev/null | grep -v obj | tail -1`

		if test -e $sourcelink; then
			kernelsrc=`readlink -f ${sourcelink}`
		else
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
			*** Please specify the location of the kernel source
			*** with the '--with-linux=PATH' option])
		fi
	else
		if test "$kernelsrc" = "NONE"; then
			kernsrcver=NONE
		fi
	fi

	AC_MSG_RESULT([$kernelsrc])
	AC_MSG_CHECKING([kernel build directory])
	if test -z "$kernelbuild"; then
		if test -d ${kernelsrc}-obj/`arch`/`arch`; then
			kernelbuild=${kernelsrc}-obj/`arch`/`arch`
		elif test -d ${kernelsrc}-obj/`arch`/default; then
		        kernelbuild=${kernelsrc}-obj/`arch`/default
		elif test -d `dirname ${kernelsrc}`/build-`arch`; then
			kernelbuild=`dirname ${kernelsrc}`/build-`arch`
		else
			kernelbuild=${kernelsrc}
		fi
	fi
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

	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}
	LINUX_VERSION=${kernsrcver}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(LINUX_VERSION)

	ZFS_AC_MODULE_SYMVERS
])

dnl #
dnl # Detect name used for the additional SPL Module.symvers file
dnl #
AC_DEFUN([ZFS_AC_SPL_MODULE_SYMVERS], [
	AC_MSG_CHECKING([spl file name for module symbols])
	if test -r $SPL_OBJ/Module.symvers; then
		SPL_SYMBOLS=Module.symvers
	elif test -r $SPL_OBJ/Modules.symvers; then
		SPL_SYMBOLS=Modules.symvers
	else
		SPL_SYMBOLS=NONE
	fi

	AC_MSG_RESULT([$SPL_SYMBOLS])
	AC_SUBST(SPL_SYMBOLS)
])

dnl #
dnl # Detect the SPL module to be built against
dnl #
AC_DEFUN([ZFS_AC_SPL], [
	AC_ARG_WITH([spl],
		AS_HELP_STRING([--with-spl=PATH],
		[Path to spl source]),
		[splsrc="$withval"])

	AC_ARG_WITH([spl-obj],
		AS_HELP_STRING([--with-spl-obj=PATH],
		[Path to spl build objects]),
		[splbuild="$withval"])


	AC_MSG_CHECKING([spl source directory])
	if test -z "$splsrc"; then
		sourcelink=`ls -1d /usr/src/spl-*/${LINUX_VERSION} \
		            2>/dev/null | tail -1`

		if test -e $sourcelink; then
			splsrc=`readlink -f ${sourcelink}`
		else
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
			*** Please specify the location of the spl source
			*** with the '--with-spl=PATH' option])
		fi
	else
		if test "$splsrc" = "NONE"; then
			splbuild=NONE
			splsrcver=NONE
		fi
	fi

	AC_MSG_RESULT([$splsrc])
	AC_MSG_CHECKING([spl build directory])
	if test -z "$splbuild"; then
		if test -d ${splsrc}/module; then
			splbuild=${splsrc}/module
		else
			splbuild=${splsrc}
		fi
	fi
	AC_MSG_RESULT([$splbuild])

	AC_MSG_CHECKING([spl source version])
	if test -r $splsrc/spl_config.h &&
		fgrep -q SPL_META_VERSION $splsrc/spl_config.h; then

		splsrcver=`(echo "#include <spl_config.h>";
		            echo "splsrcver=SPL_META_VERSION") |
		            cpp -I $splsrc |
		            grep "^splsrcver=" | cut -d \" -f 2`
	fi

	if test -z "$splsrcver"; then
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([
		*** Cannot determine the version of the spl source.
		*** Please prepare the spl source before running this script])
	fi

	AC_MSG_RESULT([$splsrcver])

	SPL=${splsrc}
	SPL_OBJ=${splbuild}
	SPL_VERSION=${splsrcver}

	AC_SUBST(SPL)
	AC_SUBST(SPL_OBJ)
	AC_SUBST(SPL_VERSION)

	ZFS_AC_SPL_MODULE_SYMVERS
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
	m4_ifvaln([$1], [ZFS_LINUX_CONFTEST([$1])])
	rm -Rf build && mkdir -p build
	echo "obj-m := conftest.o" >build/Makefile
	AS_IF(
		[AC_TRY_COMMAND(cp conftest.c build && make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror-implicit-function-declaration $EXTRA_KCFLAGS" $ARCH_UM M=$PWD/build) >/dev/null && AC_TRY_COMMAND([$3])],
		[$4],
		[_AC_MSG_LOG_CONFTEST m4_ifvaln([$5],[$5])]
	)
	rm -Rf build
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
	grep -q -E '[[[:space:]]]$1[[[:space:]]]' \
		$LINUX_OBJ/Module*.symvers $SPL_OBJ/Module*.symvers 2>/dev/null
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
