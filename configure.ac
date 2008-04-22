AC_INIT

AC_CANONICAL_SYSTEM
AM_INIT_AUTOMAKE(spl, 0.2.0)
AC_CONFIG_HEADERS([config.h])

AC_PROG_INSTALL
AC_PROG_CC
AC_PROG_LIBTOOL

ver=`uname -r`
kernelsrc=
kernelbuild=
AC_ARG_WITH(kernel,
	[  --with-linux=PATH      Path to kernel source ],
	[kernelsrc="$withval"; kernelbuild="$withval"])
AC_ARG_WITH(kernel-build,
	[  --with-linux-obj=PATH  Path to kernel build objects ],
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

#
# Each version of the kernel provides a slightly different
# ABI, so figure out what we have to work with and adapt.
#
AC_MSG_CHECKING([if kernel defines kzalloc function])
if egrep -qw "kzalloc" $kernelsrc/include/linux/slab.h; then
	AC_DEFINE(HAVE_KZALLOC, 1, [kzalloc() is defined])
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi


AC_MSG_CHECKING([if kernel has mutex.h ])
if test -f $kernelsrc/include/linux/mutex.h; then
	AC_DEFINE(HAVE_MUTEX_H, 1, [kernel has mutex.h])
	AC_MSG_RESULT([yes])
else
	AC_MSG_RESULT([no])
fi

TOPDIR=`/bin/pwd`

# Add "V=1" to KERNELMAKE_PARAMS to enable verbose module build
KERNELMAKE_PARAMS=
KERNELCPPFLAGS="-I$TOPDIR -I$TOPDIR/include"

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
