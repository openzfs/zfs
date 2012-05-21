dnl #
dnl # Default ZFS kernel configuration 
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL], [
	ZFS_AC_KERNEL
	ZFS_AC_SPL
	ZFS_AC_KERNEL_CONFIG
	ZFS_AC_KERNEL_BDEV_BLOCK_DEVICE_OPERATIONS
	ZFS_AC_KERNEL_TYPE_FMODE_T
	ZFS_AC_KERNEL_KOBJ_NAME_LEN
	ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH
	ZFS_AC_KERNEL_OPEN_BDEV_EXCLUSIVE
	ZFS_AC_KERNEL_INVALIDATE_BDEV_ARGS
	ZFS_AC_KERNEL_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BIO_EMPTY_BARRIER
	ZFS_AC_KERNEL_BIO_FAILFAST
	ZFS_AC_KERNEL_BIO_FAILFAST_DTD
	ZFS_AC_KERNEL_REQ_FAILFAST_MASK
	ZFS_AC_KERNEL_BIO_END_IO_T_ARGS
	ZFS_AC_KERNEL_BIO_RW_SYNC
	ZFS_AC_KERNEL_BIO_RW_SYNCIO
	ZFS_AC_KERNEL_REQ_SYNC
	ZFS_AC_KERNEL_BLK_END_REQUEST
	ZFS_AC_KERNEL_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS
	ZFS_AC_KERNEL_BLK_QUEUE_PHYSICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BLK_QUEUE_IO_OPT
	ZFS_AC_KERNEL_BLK_QUEUE_NONROT
	ZFS_AC_KERNEL_BLK_QUEUE_DISCARD
	ZFS_AC_KERNEL_BLK_FETCH_REQUEST
	ZFS_AC_KERNEL_BLK_REQUEUE_REQUEST
	ZFS_AC_KERNEL_BLK_RQ_BYTES
	ZFS_AC_KERNEL_BLK_RQ_POS
	ZFS_AC_KERNEL_BLK_RQ_SECTORS
	ZFS_AC_KERNEL_GET_DISK_RO
	ZFS_AC_KERNEL_RQ_IS_SYNC
	ZFS_AC_KERNEL_RQ_FOR_EACH_SEGMENT
	ZFS_AC_KERNEL_CONST_XATTR_HANDLER
	ZFS_AC_KERNEL_XATTR_HANDLER_GET
	ZFS_AC_KERNEL_XATTR_HANDLER_SET
	ZFS_AC_KERNEL_SHOW_OPTIONS
	ZFS_AC_KERNEL_FSYNC
	ZFS_AC_KERNEL_EVICT_INODE
	ZFS_AC_KERNEL_NR_CACHED_OBJECTS
	ZFS_AC_KERNEL_FREE_CACHED_OBJECTS
	ZFS_AC_KERNEL_FALLOCATE
	ZFS_AC_KERNEL_CREATE_UMODE_T
	ZFS_AC_KERNEL_AUTOMOUNT
	ZFS_AC_KERNEL_INSERT_INODE_LOCKED
	ZFS_AC_KERNEL_D_OBTAIN_ALIAS
	ZFS_AC_KERNEL_CHECK_DISK_SIZE_CHANGE
	ZFS_AC_KERNEL_TRUNCATE_SETSIZE
	ZFS_AC_KERNEL_6ARGS_SECURITY_INODE_INIT_SECURITY
	ZFS_AC_KERNEL_CALLBACK_SECURITY_INODE_INIT_SECURITY
	ZFS_AC_KERNEL_MOUNT_NODEV
	ZFS_AC_KERNEL_SHRINK
	ZFS_AC_KERNEL_BDI
	ZFS_AC_KERNEL_BDI_SETUP_AND_REGISTER
	ZFS_AC_KERNEL_SET_NLINK

	AS_IF([test "$LINUX_OBJ" != "$LINUX"], [
		KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$LINUX_OBJ"
	])
	AC_SUBST(KERNELMAKE_PARAMS)


	dnl # -Wall -fno-strict-aliasing -Wstrict-prototypes and other
	dnl # compiler options are added by the kernel build system.
	KERNELCPPFLAGS="$KERNELCPPFLAGS $NO_UNUSED_BUT_SET_VARIABLE"
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DHAVE_SPL -D_KERNEL"
	KERNELCPPFLAGS="$KERNELCPPFLAGS -DTEXT_DOMAIN=\\\"zfs-linux-kernel\\\""

	AC_SUBST(KERNELCPPFLAGS)
])

dnl #
dnl # Detect name used for Module.symvers file in kernel
dnl #
AC_DEFUN([ZFS_AC_MODULE_SYMVERS], [
	modpost=$LINUX/scripts/Makefile.modpost
	AC_MSG_CHECKING([kernel file name for module symbols])
	AS_IF([test -f "$modpost"], [
		AS_IF([grep -q Modules.symvers $modpost], [
			LINUX_SYMBOLS=Modules.symvers
		], [
			LINUX_SYMBOLS=Module.symvers
		])

		AS_IF([test ! -f "$LINUX_OBJ/$LINUX_SYMBOLS"], [
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed.  If your building with a custom kernel make sure the
	*** kernel is configured, built, and the '--with-linux=PATH' configure
	*** option refers to the location of the kernel source.])
		])
	], [
		LINUX_SYMBOLS=NONE
	])
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
	AS_IF([test -z "$kernelsrc"], [
		AS_IF([test -e "/lib/modules/$(uname -r)/source"], [
			headersdir="/lib/modules/$(uname -r)/source"
			sourcelink=$(readlink -f "$headersdir")
		], [test -e "/lib/modules/$(uname -r)/build"], [
			headersdir="/lib/modules/$(uname -r)/build"
			sourcelink=$(readlink -f "$headersdir")
		], [
			sourcelink=$(ls -1d /usr/src/kernels/* \
			             /usr/src/linux-* \
			             2>/dev/null | grep -v obj | tail -1)
		])

		AS_IF([test -n "$sourcelink" && test -e ${sourcelink}], [
			kernelsrc=`readlink -f ${sourcelink}`
		], [
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed then try again.  If that fails you can specify the
	*** location of the kernel source with the '--with-linux=PATH' option.])
		])
	], [
		AS_IF([test "$kernelsrc" = "NONE"], [
			kernsrcver=NONE
		])
	])

	AC_MSG_RESULT([$kernelsrc])
	AC_MSG_CHECKING([kernel build directory])
	AS_IF([test -z "$kernelbuild"], [
		AS_IF([test -e "/lib/modules/$(uname -r)/build"], [
			kernelbuild=`readlink -f /lib/modules/$(uname -r)/build`
		], [test -d ${kernelsrc}-obj/${target_cpu}/${target_cpu}], [
			kernelbuild=${kernelsrc}-obj/${target_cpu}/${target_cpu}
		], [test -d ${kernelsrc}-obj/${target_cpu}/default], [
		        kernelbuild=${kernelsrc}-obj/${target_cpu}/default
		], [test -d `dirname ${kernelsrc}`/build-${target_cpu}], [
			kernelbuild=`dirname ${kernelsrc}`/build-${target_cpu}
		], [
			kernelbuild=${kernelsrc}
		])
	])
	AC_MSG_RESULT([$kernelbuild])

	AC_MSG_CHECKING([kernel source version])
	utsrelease1=$kernelbuild/include/linux/version.h
	utsrelease2=$kernelbuild/include/linux/utsrelease.h
	utsrelease3=$kernelbuild/include/generated/utsrelease.h
	AS_IF([test -r $utsrelease1 && fgrep -q UTS_RELEASE $utsrelease1], [
		utsrelease=linux/version.h
	], [test -r $utsrelease2 && fgrep -q UTS_RELEASE $utsrelease2], [
		utsrelease=linux/utsrelease.h
	], [test -r $utsrelease3 && fgrep -q UTS_RELEASE $utsrelease3], [
		utsrelease=generated/utsrelease.h
	])

	AS_IF([test "$utsrelease"], [
		kernsrcver=`(echo "#include <$utsrelease>";
		             echo "kernsrcver=UTS_RELEASE") |
		             cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`

		AS_IF([test -z "$kernsrcver"], [
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([*** Cannot determine kernel version.])
		])
	], [
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([*** Cannot find UTS_RELEASE definition.])
	])

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
dnl # Detect name used for the additional SPL Module.symvers file.  If one
dnl # does not exist this is likely because the SPL has been configured
dnl # but not built.  To allow recursive builds a good guess is made as to
dnl # what this file will be named based on what it is named in the kernel
dnl # build products.  This file will first be used at link time so if
dnl # the guess is wrong the build will fail then.  This unfortunately
dnl # means the ZFS package does not contain a reliable mechanism to
dnl # detect symbols exported by the SPL at configure time.
dnl #
AC_DEFUN([ZFS_AC_SPL_MODULE_SYMVERS], [
	AC_MSG_CHECKING([spl file name for module symbols])
	AS_IF([test -r $SPL_OBJ/Module.symvers], [
		SPL_SYMBOLS=Module.symvers
	], [test -r $SPL_OBJ/Modules.symvers], [
		SPL_SYMBOLS=Modules.symvers
	], [test -r $SPL_OBJ/module/Module.symvers], [
		SPL_SYMBOLS=Module.symvers
	], [test -r $SPL_OBJ/module/Modules.symvers], [
		SPL_SYMBOLS=Modules.symvers
	], [
		SPL_SYMBOLS=$LINUX_SYMBOLS
	])

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
	AS_IF([test -z "$splsrc"], [
		sourcelink=`ls -1d /usr/src/spl-*/${LINUX_VERSION} \
		            2>/dev/null | tail -1`

		AS_IF([test -z "$sourcelink" || test ! -e $sourcelink], [
			sourcelink=../spl
		])

		AS_IF([test -e $sourcelink], [
			splsrc=`readlink -f ${sourcelink}`
		], [
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
	*** Please make sure the spl devel package for your distribution
	*** is installed then try again.  If that fails you can specify the
	*** location of the spl source with the '--with-spl=PATH' option.])
		])
	], [
		AS_IF([test "$splsrc" = "NONE"], [
			splbuild=NONE
			splsrcver=NONE
		])
	])

	AC_MSG_RESULT([$splsrc])
	AC_MSG_CHECKING([spl build directory])
	AS_IF([test -z "$splbuild"], [
		splbuild=${splsrc}
	])
	AC_MSG_RESULT([$splbuild])

	AC_MSG_CHECKING([spl source version])
	AS_IF([test -r $splbuild/spl_config.h &&
		fgrep -q SPL_META_VERSION $splbuild/spl_config.h], [

		splsrcver=`(echo "#include <spl_config.h>";
		            echo "splsrcver=SPL_META_VERSION-SPL_META_RELEASE") |
		            cpp -I $splbuild |
		            grep "^splsrcver=" | tr -d \" | cut -d= -f2`
	])

	AS_IF([test -z "$splsrcver"], [
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([
	*** Cannot determine the version of the spl source.
	*** Please prepare the spl source before running this script])
	])

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
dnl # Certain kernel build options are not supported.  These must be
dnl # detected at configure time and cause a build failure.  Otherwise
dnl # modules may be successfully built that behave incorrectly.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG], [

	AS_IF([test "$ZFS_META_LICENSE" = GPL], [
		AC_DEFINE([HAVE_GPL_ONLY_SYMBOLS], [1],
			[Define to 1 if licensed under the GPL])
	])

	ZFS_AC_KERNEL_CONFIG_PREEMPT
	ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC
])

dnl #
dnl # Check CONFIG_PREEMPT
dnl #
dnl # Premptible kernels will be supported in the future.  But at the
dnl # moment there are a few places in the code which need to be updated
dnl # to accomidate them.  Until that work occurs we should detect this
dnl # at configure time and fail with a sensible message.  Otherwise,
dnl # people will be able to build successfully, however they will have
dnl # stability problems.  See https://github.com/zfsonlinux/zfs/issues/83
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_PREEMPT], [

	ZFS_LINUX_CONFIG([PREEMPT],
		AC_MSG_ERROR([
	*** Kernel built with CONFIG_PREEMPT which is not supported.
	*** You must rebuild your kernel without this option.]), [])
])

dnl #
dnl # Check CONFIG_DEBUG_LOCK_ALLOC
dnl #
dnl # This is typically only set for debug kernels because it comes with
dnl # a performance penalty.  However, when it is set it maps the non-GPL
dnl # symbol mutex_lock() to the GPL-only mutex_lock_nested() symbol.
dnl # This will cause a failure at link time which we'd rather know about
dnl # at compile time.
dnl #
dnl # Since we plan to pursue making mutex_lock_nested() a non-GPL symbol
dnl # with the upstream community we add a check to detect this case.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC], [

	ZFS_LINUX_CONFIG([DEBUG_LOCK_ALLOC], [
		AC_MSG_CHECKING([whether mutex_lock() is GPL-only])
		tmp_flags="$EXTRA_KCFLAGS"
		ZFS_LINUX_TRY_COMPILE([
			#include <linux/module.h>
			#include <linux/mutex.h>

			MODULE_LICENSE("$ZFS_META_LICENSE");
		],[
			struct mutex lock;

			mutex_init(&lock);
			mutex_lock(&lock);
			mutex_unlock(&lock);
		],[
			AC_MSG_RESULT(no)
		],[
			AC_MSG_RESULT(yes)
			AC_MSG_ERROR([
	*** Kernel built with CONFIG_DEBUG_LOCK_ALLOC which is incompatible
	*** with the CDDL license.  You must rebuild your kernel without
	*** this option enabled.])
		])
		EXTRA_KCFLAGS="$tmp_flags"
	], [])
])

dnl #
dnl # ZFS_LINUX_CONFTEST
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST], [
cat confdefs.h - <<_ACEOF >conftest.c
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
		#include <linux/module.h>
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
		$LINUX_OBJ/$LINUX_SYMBOLS 2>/dev/null
	rc=$?
	AS_IF([test $rc -ne 0], [
		export=0
		for file in $2; do
			grep -q -E "EXPORT_SYMBOL.*($1)" "$LINUX/$file" 2>/dev/null
			rc=$?
			AS_IF([test $rc -eq 0], [
				export=1
				break;
			])
		done
		AS_IF([test $export -eq 0], [
			AC_MSG_RESULT([no])
			$4
		], [
			AC_MSG_RESULT([yes])
			$3
		])
	], [
		AC_MSG_RESULT([yes])
		$3
	])
])
