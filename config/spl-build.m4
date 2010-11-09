###############################################################################
# Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
# Copyright (C) 2007 The Regents of the University of California.
# Written by Brian Behlendorf <behlendorf1@llnl.gov>.
###############################################################################
# SPL_AC_CONFIG_KERNEL: Default SPL kernel configuration.
###############################################################################

AC_DEFUN([SPL_AC_CONFIG_KERNEL], [
	SPL_AC_KERNEL

	if test "${LINUX_OBJ}" != "${LINUX}"; then
		KERNELMAKE_PARAMS="$KERNELMAKE_PARAMS O=$LINUX_OBJ"
	fi
	AC_SUBST(KERNELMAKE_PARAMS)

	KERNELCPPFLAGS="$KERNELCPPFLAGS -Wstrict-prototypes"
	AC_SUBST(KERNELCPPFLAGS)

	SPL_AC_DEBUG
	SPL_AC_DEBUG_KMEM
	SPL_AC_DEBUG_KMEM_TRACKING
	SPL_AC_ATOMIC_SPINLOCK
	SPL_AC_TYPE_ATOMIC64_CMPXCHG
	SPL_AC_TYPE_ATOMIC64_XCHG
	SPL_AC_TYPE_UINTPTR_T
	SPL_AC_3ARGS_INIT_WORK
	SPL_AC_2ARGS_REGISTER_SYSCTL
	SPL_AC_SET_SHRINKER
	SPL_AC_3ARGS_SHRINKER_CALLBACK
	SPL_AC_PATH_IN_NAMEIDATA
	SPL_AC_TASK_CURR
	SPL_AC_CTL_UNNUMBERED
	SPL_AC_CTL_NAME
	SPL_AC_FLS64
	SPL_AC_DEVICE_CREATE
	SPL_AC_5ARGS_DEVICE_CREATE
	SPL_AC_CLASS_DEVICE_CREATE
	SPL_AC_SET_NORMALIZED_TIMESPEC_EXPORT
	SPL_AC_SET_NORMALIZED_TIMESPEC_INLINE
	SPL_AC_TIMESPEC_SUB
	SPL_AC_INIT_UTSNAME
	SPL_AC_FDTABLE_HEADER
	SPL_AC_FILES_FDTABLE
	SPL_AC_UACCESS_HEADER
	SPL_AC_KMALLOC_NODE
	SPL_AC_MONOTONIC_CLOCK
	SPL_AC_INODE_I_MUTEX
	SPL_AC_MUTEX_OWNER
	SPL_AC_MUTEX_LOCK_NESTED
	SPL_AC_3ARGS_ON_EACH_CPU
	SPL_AC_KALLSYMS_LOOKUP_NAME
	SPL_AC_GET_VMALLOC_INFO
	SPL_AC_PGDAT_HELPERS
	SPL_AC_FIRST_ONLINE_PGDAT
	SPL_AC_NEXT_ONLINE_PGDAT
	SPL_AC_NEXT_ZONE
	SPL_AC_PGDAT_LIST
	SPL_AC_GLOBAL_PAGE_STATE
	SPL_AC_ZONE_STAT_ITEM_FREE
	SPL_AC_ZONE_STAT_ITEM_INACTIVE
	SPL_AC_ZONE_STAT_ITEM_ACTIVE
	SPL_AC_GET_ZONE_COUNTS
	SPL_AC_USER_PATH_DIR
	SPL_AC_SET_FS_PWD
	SPL_AC_2ARGS_SET_FS_PWD
	SPL_AC_2ARGS_VFS_UNLINK
	SPL_AC_4ARGS_VFS_RENAME
	SPL_AC_FS_STRUCT_SPINLOCK
	SPL_AC_CRED_STRUCT
	SPL_AC_GROUPS_SEARCH
	SPL_AC_PUT_TASK_STRUCT
	SPL_AC_5ARGS_PROC_HANDLER
	SPL_AC_KVASPRINTF
	SPL_AC_3ARGS_FILE_FSYNC
	SPL_AC_EXPORTED_RWSEM_IS_LOCKED
])

AC_DEFUN([SPL_AC_MODULE_SYMVERS], [
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

AC_DEFUN([SPL_AC_KERNEL], [
	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"])

	AC_ARG_WITH([linux-obj],
		AS_HELP_STRING([--with-linux-obj=PATH],
		[Path to kernel build objects]),
		[kernelbuild="$withval"])

	AC_MSG_CHECKING([kernel source directory])
	if test -z "$kernelsrc"; then
		headersdir="/lib/modules/$(uname -r)/build"
		if test -e "$headersdir"; then
			sourcelink=$(readlink -f "$headersdir")
		else
			sourcelink=$(ls -1d /usr/src/kernels/* \
				     /usr/src/linux-* \
			             2>/dev/null | grep -v obj | tail -1)
		fi

		if test -n "$sourcelink" && test -e ${sourcelink}; then
			kernelsrc=`readlink -f ${sourcelink}`
		else
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed then try again.  If that fails you can specify the
	*** location of the kernel source with the '--with-linux=PATH' option.])
		fi
	else
		if test "$kernelsrc" = "NONE"; then
			kernsrcver=NONE
		fi
	fi

	AC_MSG_RESULT([$kernelsrc])
	AC_MSG_CHECKING([kernel build directory])
	if test -z "$kernelbuild"; then
		if test -d ${kernelsrc}-obj/${target_cpu}/${target_cpu}; then
			kernelbuild=${kernelsrc}-obj/${target_cpu}/${target_cpu}
		elif test -d ${kernelsrc}-obj/${target_cpu}/default; then
			kernelbuild=${kernelsrc}-obj/${target_cpu}/default
		elif test -d `dirname ${kernelsrc}`/build-${target_cpu}; then
			kernelbuild=`dirname ${kernelsrc}`/build-${target_cpu}
		else
			kernelbuild=${kernelsrc}
		fi
	fi
	AC_MSG_RESULT([$kernelbuild])

	AC_MSG_CHECKING([kernel source version])
	utsrelease1=$kernelbuild/include/linux/version.h
	utsrelease2=$kernelbuild/include/linux/utsrelease.h
	utsrelease3=$kernelbuild/include/generated/utsrelease.h
	if test -r $utsrelease1 && fgrep -q UTS_RELEASE $utsrelease1; then
		utsrelease=linux/version.h
	elif test -r $utsrelease2 && fgrep -q UTS_RELEASE $utsrelease2; then
		utsrelease=linux/utsrelease.h
	elif test -r $utsrelease3 && fgrep -q UTS_RELEASE $utsrelease3; then
		utsrelease=generated/utsrelease.h
	fi

	if test "$utsrelease"; then
		kernsrcver=`(echo "#include <$utsrelease>";
		             echo "kernsrcver=UTS_RELEASE") | 
		             cpp -I $kernelbuild/include |
		             grep "^kernsrcver=" | cut -d \" -f 2`

		if test -z "$kernsrcver"; then
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([*** Cannot determine kernel version.])
		fi
	else
		AC_MSG_RESULT([Not found])
		AC_MSG_ERROR([*** Cannot find UTS_RELEASE definition.])
	fi

	AC_MSG_RESULT([$kernsrcver])

	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}
	LINUX_VERSION=${kernsrcver}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(LINUX_VERSION)

	SPL_AC_MODULE_SYMVERS
])

dnl #
dnl # Explicitly check for gawk, we require it for the the usermode
dnl # helper.  For some reason the standard awk command does not
dnl # behave correctly when invoked from the usermode helper.
dnl #
AC_DEFUN([SPL_AC_GAWK], [
	AS_IF([test "x$AWK" != xgawk], [
                AC_MSG_ERROR([
	*** Required util gawk missing.  Please install the required
	*** gawk package for your distribution and try again.])
	])
])

dnl #
dnl # Default SPL user configuration
dnl #
AC_DEFUN([SPL_AC_CONFIG_USER], [
	SPL_AC_GAWK
])

dnl #
dnl # Check for rpm+rpmbuild to build RPM packages.  If these tools
dnl # are missing it is non-fatal but you will not be able to build
dnl # RPM packages and will be warned if you try too.
dnl #
AC_DEFUN([SPL_AC_RPM], [
	RPM=rpm
	RPMBUILD=rpmbuild

	AC_MSG_CHECKING([whether $RPM is available])
	AS_IF([tmp=$($RPM --version 2>/dev/null)], [
		RPM_VERSION=$(echo $tmp | $AWK '/RPM/ { print $[3] }')
		HAVE_RPM=yes
		AC_MSG_RESULT([$HAVE_RPM ($RPM_VERSION)])
	],[
		HAVE_RPM=no
		AC_MSG_RESULT([$HAVE_RPM])
	])

	AC_MSG_CHECKING([whether $RPMBUILD is available])
	AS_IF([tmp=$($RPMBUILD --version 2>/dev/null)], [
		RPMBUILD_VERSION=$(echo $tmp | $AWK '/RPM/ { print $[3] }')
		HAVE_RPMBUILD=yes
		AC_MSG_RESULT([$HAVE_RPMBUILD ($RPMBUILD_VERSION)])
	],[
		HAVE_RPMBUILD=no
		AC_MSG_RESULT([$HAVE_RPMBUILD])
	])

	AC_SUBST(HAVE_RPM)
	AC_SUBST(RPM)
	AC_SUBST(RPM_VERSION)

	AC_SUBST(HAVE_RPMBUILD)
	AC_SUBST(RPMBUILD)
	AC_SUBST(RPMBUILD_VERSION)
])

dnl #
dnl # Check for dpkg+dpkg-buildpackage to build DEB packages.  If these
dnl # tools are missing it is non-fatal but you will not be able to build
dnl # DEB packages and will be warned if you try too.
dnl #
AC_DEFUN([SPL_AC_DPKG], [
	DPKG=dpkg
	DPKGBUILD=dpkg-buildpackage

	AC_MSG_CHECKING([whether $DPKG is available])
	AS_IF([tmp=$($DPKG --version 2>/dev/null)], [
		DPKG_VERSION=$(echo $tmp | $AWK '/Debian/ { print $[7] }')
		HAVE_DPKG=yes
		AC_MSG_RESULT([$HAVE_DPKG ($DPKG_VERSION)])
	],[
		HAVE_DPKG=no
		AC_MSG_RESULT([$HAVE_DPKG])
	])

	AC_MSG_CHECKING([whether $DPKGBUILD is available])
	AS_IF([tmp=$($DPKGBUILD --version 2>/dev/null)], [
		DPKGBUILD_VERSION=$(echo $tmp | \
		    $AWK '/Debian/ { print $[4] }' | cut -f-4 -d'.')
		HAVE_DPKGBUILD=yes
		AC_MSG_RESULT([$HAVE_DPKGBUILD ($DPKGBUILD_VERSION)])
	],[
		HAVE_DPKGBUILD=no
		AC_MSG_RESULT([$HAVE_DPKGBUILD])
	])

	AC_SUBST(HAVE_DPKG)
	AC_SUBST(DPKG)
	AC_SUBST(DPKG_VERSION)

	AC_SUBST(HAVE_DPKGBUILD)
	AC_SUBST(DPKGBUILD)
	AC_SUBST(DPKGBUILD_VERSION)
])

dnl #
dnl # Until native packaging for various different packing systems
dnl # can be added the least we can do is attempt to use alien to
dnl # convert the RPM packages to the needed package type.  This is
dnl # a hack but so far it has worked reasonable well.
dnl #
AC_DEFUN([SPL_AC_ALIEN], [
	ALIEN=alien

	AC_MSG_CHECKING([whether $ALIEN is available])
	AS_IF([tmp=$($ALIEN --version 2>/dev/null)], [
		ALIEN_VERSION=$(echo $tmp | $AWK '{ print $[3] }')
		HAVE_ALIEN=yes
		AC_MSG_RESULT([$HAVE_ALIEN ($ALIEN_VERSION)])
	],[
		HAVE_ALIEN=no
		AC_MSG_RESULT([$HAVE_ALIEN])
	])

	AC_SUBST(HAVE_ALIEN)
	AC_SUBST(ALIEN)
	AC_SUBST(ALIEN_VERSION)
])

dnl #
dnl # Using the VENDOR tag from config.guess set the default
dnl # package type for 'make pkg': (rpm | deb | tgz)
dnl #
AC_DEFUN([SPL_AC_DEFAULT_PACKAGE], [
	AC_MSG_CHECKING([linux distribution])
	if test -f /etc/redhat-release ; then
		VENDOR=redhat ;
	elif test -f /etc/fedora-release ; then
		VENDOR=fedora ;
	elif test -f /etc/lsb-release ; then
		VENDOR=ubuntu ;
	elif test -f /etc/debian_version ; then
		VENDOR=debian ;
	elif test -f /etc/SuSE-release ; then
		VENDOR=sles ;
	elif test -f /etc/slackware-version ; then
		VENDOR=slackware ;
	elif test -f /etc/gentoo-release ; then
		VENDOR=gentoo ;
	else
		VENDOR= ;
	fi
	AC_MSG_RESULT([$VENDOR])
	AC_SUBST(VENDOR)

	AC_MSG_CHECKING([default package type])
	case "$VENDOR" in
		fedora)     DEFAULT_PACKAGE=rpm ;;
		redhat)     DEFAULT_PACKAGE=rpm ;;
		sles)       DEFAULT_PACKAGE=rpm ;;
		ubuntu)     DEFAULT_PACKAGE=deb ;;
		debian)     DEFAULT_PACKAGE=deb ;;
		slackware)  DEFAULT_PACKAGE=tgz ;;
		*)          DEFAULT_PACKAGE=rpm ;;
	esac

	AC_MSG_RESULT([$DEFAULT_PACKAGE])
	AC_SUBST(DEFAULT_PACKAGE)
])

dnl #
dnl # Default SPL user configuration
dnl #
AC_DEFUN([SPL_AC_PACKAGE], [
	SPL_AC_RPM
	SPL_AC_DPKG
	SPL_AC_ALIEN
	SPL_AC_DEFAULT_PACKAGE
])

AC_DEFUN([SPL_AC_LICENSE], [
	AC_MSG_CHECKING([spl license])
	LICENSE=GPL
	AC_MSG_RESULT([$LICENSE])
	KERNELCPPFLAGS="${KERNELCPPFLAGS} -DHAVE_GPL_ONLY_SYMBOLS"
	AC_SUBST(LICENSE)
])

AC_DEFUN([SPL_AC_CONFIG], [
        SPL_CONFIG=all
        AC_ARG_WITH([config],
                AS_HELP_STRING([--with-config=CONFIG],
                [Config file 'kernel|user|all|srpm']),
                [SPL_CONFIG="$withval"])

        AC_MSG_CHECKING([spl config])
        AC_MSG_RESULT([$SPL_CONFIG]);
        AC_SUBST(SPL_CONFIG)

        case "$SPL_CONFIG" in
                kernel) SPL_AC_CONFIG_KERNEL ;;
                user)   SPL_AC_CONFIG_USER   ;;
                all)    SPL_AC_CONFIG_KERNEL
                        SPL_AC_CONFIG_USER   ;;
		srpm)                        ;;
                *)
                AC_MSG_RESULT([Error!])
                AC_MSG_ERROR([Bad value "$SPL_CONFIG" for --with-config,
                              user kernel|user|all|srpm]) ;;
        esac

        AM_CONDITIONAL([CONFIG_USER],
                       [test "$SPL_CONFIG" = user] ||
                       [test "$SPL_CONFIG" = all])
        AM_CONDITIONAL([CONFIG_KERNEL],
                       [test "$SPL_CONFIG" = kernel] ||
                       [test "$SPL_CONFIG" = all])
])

dnl #
dnl # Enable if the SPL should be compiled with internal debugging enabled.
dnl # By default this support is disabled.
dnl #
AC_DEFUN([SPL_AC_DEBUG], [
	AC_MSG_CHECKING([whether debugging is enabled])
	AC_ARG_ENABLE([debug],
		[AS_HELP_STRING([--enable-debug],
		[Enable generic debug support @<:@default=no@:>@])],
		[],
		[enable_debug=no])

	AS_IF([test "x$enable_debug" = xyes],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG -Werror"
		DEBUG_CFLAGS="-DDEBUG -Werror"
	],
	[
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG"
		DEBUG_CFLAGS="-DNDEBUG"
	])

	AC_SUBST(DEBUG_CFLAGS)
	AC_MSG_RESULT([$enable_debug])
])

dnl #
dnl # Enabled by default it provides a minimal level of memory tracking.
dnl # A total count of bytes allocated is kept for each alloc and free.
dnl # Then at module unload time a report to the console will be printed
dnl # if memory was leaked.  Additionally, /proc/spl/kmem/slab will exist
dnl # and provide an easy way to inspect the kmem based slab.
dnl #
AC_DEFUN([SPL_AC_DEBUG_KMEM], [
	AC_ARG_ENABLE([debug-kmem],
		[AS_HELP_STRING([--enable-debug-kmem],
		[Enable basic kmem accounting @<:@default=yes@:>@])],
		[],
		[enable_debug_kmem=yes])

	AS_IF([test "x$enable_debug_kmem" = xyes],
		[AC_DEFINE([DEBUG_KMEM], [1],
		[Define to 1 to enable basic kmem accounting])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM"])

	AC_MSG_CHECKING([whether basic kmem accounting is enabled])
	AC_MSG_RESULT([$enable_debug_kmem])
])

dnl #
dnl # Disabled by default it provides detailed memory tracking.  This
dnl # feature also requires --enable-debug-kmem to be set.  When enabled
dnl # not only will total bytes be tracked but also the location of every
dnl # alloc and free.  When the SPL module is unloaded a list of all leaked
dnl # addresses and where they were allocated will be dumped to the console.
dnl # Enabling this feature has a significant impact on performance but it
dnl # makes finding memory leaks pretty straight forward.
dnl #
AC_DEFUN([SPL_AC_DEBUG_KMEM_TRACKING], [
	AC_ARG_ENABLE([debug-kmem-tracking],
		[AS_HELP_STRING([--enable-debug-kmem-tracking],
		[Enable detailed kmem tracking  @<:@default=no@:>@])],
		[],
		[enable_debug_kmem_tracking=no])

	AS_IF([test "x$enable_debug_kmem_tracking" = xyes],
		[AC_DEFINE([DEBUG_KMEM_TRACKING], [1],
		[Define to 1 to enable detailed kmem tracking])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM_TRACKING"])

	AC_MSG_CHECKING([whether detailed kmem tracking is enabled])
	AC_MSG_RESULT([$enable_debug_kmem_tracking])
])

dnl #
dnl # SPL_LINUX_CONFTEST
dnl #
AC_DEFUN([SPL_LINUX_CONFTEST], [
cat confdefs.h - <<_ACEOF >conftest.c
$1
_ACEOF
])

dnl #
dnl # SPL_LANG_PROGRAM(C)([PROLOGUE], [BODY])
dnl #
m4_define([SPL_LANG_PROGRAM], [
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
dnl # SPL_LINUX_COMPILE_IFELSE / like AC_COMPILE_IFELSE
dnl #
AC_DEFUN([SPL_LINUX_COMPILE_IFELSE], [
	m4_ifvaln([$1], [SPL_LINUX_CONFTEST([$1])])
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
dnl # SPL_LINUX_TRY_COMPILE like AC_TRY_COMPILE
dnl #
AC_DEFUN([SPL_LINUX_TRY_COMPILE],
	[SPL_LINUX_COMPILE_IFELSE(
	[AC_LANG_SOURCE([SPL_LANG_PROGRAM([[$1]], [[$2]])])],
	[modules],
	[test -s build/conftest.o],
	[$3], [$4])
])

dnl #
dnl # SPL_LINUX_CONFIG
dnl #
AC_DEFUN([SPL_LINUX_CONFIG],
	[AC_MSG_CHECKING([whether Linux was built with CONFIG_$1])
	SPL_LINUX_TRY_COMPILE([
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
dnl # SPL_CHECK_SYMBOL_EXPORT
dnl # check symbol exported or not
dnl #
AC_DEFUN([SPL_CHECK_SYMBOL_EXPORT],
	[AC_MSG_CHECKING([whether symbol $1 is exported])
	grep -q -E '[[[:space:]]]$1[[[:space:]]]' \
		$LINUX_OBJ/Module*.symvers 2>/dev/null
	rc=$?
	if test $rc -ne 0; then
		export=0
		for file in $2; do
			grep -q -E "EXPORT_SYMBOL.*($1)" \
				"$LINUX_OBJ/$file" 2>/dev/null
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

dnl #
dnl # SPL_CHECK_HEADER
dnl # check whether header exists and define HAVE_$2_HEADER
dnl #
AC_DEFUN([SPL_CHECK_HEADER],
	[AC_MSG_CHECKING([whether header $1 exists])
	SPL_LINUX_TRY_COMPILE([
		#include <$1>
	],[
		return 0;
	],[
		AC_DEFINE(HAVE_$2_HEADER, 1, [$1 exists])
		AC_MSG_RESULT(yes)
		$3
	],[
		AC_MSG_RESULT(no)
		$4
	])
])

dnl #
dnl # Use the atomic implemenation based on global spinlocks.  This
dnl # should only be needed by 32-bit kernels which do not provide
dnl # the atomic64_* API.  It may be optionally enabled as a fallback
dnl # if problems are observed with the direct mapping to the native
dnl # Linux atomic operations.  You may not disable atomic spinlocks
dnl # if you kernel does not an atomic64_* API.
dnl #
AC_DEFUN([SPL_AC_ATOMIC_SPINLOCK], [
	AC_ARG_ENABLE([atomic-spinlocks],
		[AS_HELP_STRING([--enable-atomic-spinlocks],
		[Atomic types use spinlocks @<:@default=check@:>@])],
		[],
		[enable_atomic_spinlocks=check])

	SPL_LINUX_TRY_COMPILE([
		#include <asm/atomic.h>
	],[
		atomic64_t *ptr;
	],[
		have_atomic64_t=yes
		AC_DEFINE(HAVE_ATOMIC64_T, 1,
			[kernel defines atomic64_t])
	],[
		have_atomic64_t=no
	])

	AS_IF([test "x$enable_atomic_spinlocks" = xcheck], [
		AS_IF([test "x$have_atomic64_t" = xyes], [
			enable_atomic_spinlocks=no
		],[
			enable_atomic_spinlocks=yes
		])
	])

	AS_IF([test "x$enable_atomic_spinlocks" = xyes], [
		AC_DEFINE([ATOMIC_SPINLOCK], [1],
			[Atomic types use spinlocks])
	],[
		AS_IF([test "x$have_atomic64_t" = xno], [
			AC_MSG_FAILURE(
			[--disable-atomic-spinlocks given but required atomic64 support is unavailable])
		])
	])

	AC_MSG_CHECKING([whether atomic types use spinlocks])
	AC_MSG_RESULT([$enable_atomic_spinlocks])

	AC_MSG_CHECKING([whether kernel defines atomic64_t])
	AC_MSG_RESULT([$have_atomic64_t])
])

dnl #
dnl # 2.6.24 API change,
dnl # check if atomic64_cmpxchg is defined
dnl #
AC_DEFUN([SPL_AC_TYPE_ATOMIC64_CMPXCHG],
	[AC_MSG_CHECKING([whether kernel defines atomic64_cmpxchg])
	SPL_LINUX_TRY_COMPILE([
		#include <asm/atomic.h>
		#include <asm/system.h>
	],[
		atomic64_cmpxchg((atomic64_t *)NULL, 0, 0);
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_ATOMIC64_CMPXCHG, 1,
		          [kernel defines atomic64_cmpxchg])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # 2.6.24 API change,
dnl # check if atomic64_xchg is defined
dnl #
AC_DEFUN([SPL_AC_TYPE_ATOMIC64_XCHG],
	[AC_MSG_CHECKING([whether kernel defines atomic64_xchg])
	SPL_LINUX_TRY_COMPILE([
		#include <asm/atomic.h>
	],[
		atomic64_xchg((atomic64_t *)NULL, 0);
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_ATOMIC64_XCHG, 1,
		          [kernel defines atomic64_xchg])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # 2.6.24 API change,
dnl # check if uintptr_t typedef is defined
dnl #
AC_DEFUN([SPL_AC_TYPE_UINTPTR_T],
	[AC_MSG_CHECKING([whether kernel defines uintptr_t])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/types.h>
	],[
		uintptr_t *ptr;
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_UINTPTR_T, 1,
		          [kernel defines uintptr_t])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # 2.6.20 API change,
dnl # INIT_WORK use 2 args and not store data inside
dnl #
AC_DEFUN([SPL_AC_3ARGS_INIT_WORK],
	[AC_MSG_CHECKING([whether INIT_WORK wants 3 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/workqueue.h>
	],[
		struct work_struct work;
		INIT_WORK(&work, NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_INIT_WORK, 1,
		          [INIT_WORK wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.21 API change,
dnl # 'register_sysctl_table' use only one argument instead of two
dnl #
AC_DEFUN([SPL_AC_2ARGS_REGISTER_SYSCTL],
	[AC_MSG_CHECKING([whether register_sysctl_table() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sysctl.h>
	],[
		return register_sysctl_table(NULL,0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_REGISTER_SYSCTL, 1,
		          [register_sysctl_table() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.23 API change
dnl # Old set_shrinker API replaced with register_shrinker
dnl #
AC_DEFUN([SPL_AC_SET_SHRINKER], [
	AC_MSG_CHECKING([whether set_shrinker() available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		return set_shrinker(DEFAULT_SEEKS, NULL);
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_SET_SHRINKER, 1,
		          [set_shrinker() available])
	],[
		AC_MSG_RESULT([no])
	])
])

dnl #
dnl # 2.6.35 API change,
dnl # Add context to shrinker callback
dnl #
AC_DEFUN([SPL_AC_3ARGS_SHRINKER_CALLBACK],
	[AC_MSG_CHECKING([whether shrinker callback wants 3 args])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>

		int shrinker_cb(struct shrinker *, int, unsigned int);
	],[
		struct shrinker cache_shrinker = {
			.shrink = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_SHRINKER_CALLBACK, 1,
		          [shrinker callback wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 2.6.25 API change,
dnl # struct path entry added to struct nameidata
dnl #
AC_DEFUN([SPL_AC_PATH_IN_NAMEIDATA],
	[AC_MSG_CHECKING([whether struct path used in struct nameidata])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/namei.h>
	],[
		struct nameidata nd;

		nd.path.mnt = NULL;
		nd.path.dentry = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_PATH_IN_NAMEIDATA, 1,
		          [struct path used in struct nameidata])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Custom SPL patch may export this system it is not required
dnl #
AC_DEFUN([SPL_AC_TASK_CURR], [
	SPL_CHECK_SYMBOL_EXPORT([task_curr], [kernel/sched.c],
		[AC_DEFINE(HAVE_TASK_CURR, 1, [task_curr() exported])],
		[])
])

dnl #
dnl # 2.6.19 API change,
dnl # Use CTL_UNNUMBERED when binary sysctl is not required
dnl #
AC_DEFUN([SPL_AC_CTL_UNNUMBERED],
	[AC_MSG_CHECKING([whether unnumbered sysctl support exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sysctl.h>
	],[
		#ifndef CTL_UNNUMBERED
		#error CTL_UNNUMBERED undefined
		#endif
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CTL_UNNUMBERED, 1,
		          [unnumbered sysctl support exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.33 API change,
dnl # Removed .ctl_name from struct ctl_table.
dnl #
AC_DEFUN([SPL_AC_CTL_NAME], [
	AC_MSG_CHECKING([whether struct ctl_table has ctl_name])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sysctl.h>
	],[
		struct ctl_table ctl;
		ctl.ctl_name = 0;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CTL_NAME, 1, [struct ctl_table has ctl_name])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.16 API change.
dnl # Check if 'fls64()' is available
dnl #
AC_DEFUN([SPL_AC_FLS64],
	[AC_MSG_CHECKING([whether fls64() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/bitops.h>
	],[
		return fls64(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FLS64, 1, [fls64() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.18 API change, check whether device_create() is available.
dnl # Device_create() was introduced in 2.6.18 and depricated 
dnl # class_device_create() which was fully removed in 2.6.26.
dnl #
AC_DEFUN([SPL_AC_DEVICE_CREATE], [
	SPL_CHECK_SYMBOL_EXPORT(
		[device_create],
		[drivers/base/core.c],
		[AC_DEFINE(HAVE_DEVICE_CREATE, 1,
		[device_create() is available])],
		[])
])

dnl #
dnl # 2.6.27 API change,
dnl # device_create() uses 5 args, new 'drvdata' argument.
dnl #
AC_DEFUN([SPL_AC_5ARGS_DEVICE_CREATE], [
	AC_MSG_CHECKING([whether device_create() wants 5 args])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
		#include <linux/device.h>
	],[
		device_create(NULL, NULL, 0, NULL, "%d", 1);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_5ARGS_DEVICE_CREATE, 1,
		          [device_create wants 5 args])
	],[
		AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 2.6.13 API change, check whether class_device_create() is available.
dnl # Class_device_create() was introduced in 2.6.13 and depricated
dnl # class_simple_device_add() which was fully removed in 2.6.13.
dnl #
AC_DEFUN([SPL_AC_CLASS_DEVICE_CREATE], [
	SPL_CHECK_SYMBOL_EXPORT(
		[class_device_create],
		[drivers/base/class.c],
		[AC_DEFINE(HAVE_CLASS_DEVICE_CREATE, 1,
		[class_device_create() is available])],
		[])
])

dnl #
dnl # 2.6.26 API change, set_normalized_timespec() is exported.
dnl #
AC_DEFUN([SPL_AC_SET_NORMALIZED_TIMESPEC_EXPORT], [
	SPL_CHECK_SYMBOL_EXPORT(
		[set_normalized_timespec],
		[kernel/time.c],
		[AC_DEFINE(HAVE_SET_NORMALIZED_TIMESPEC_EXPORT, 1,
		[set_normalized_timespec() is available as export])],
		[])
])

dnl #
dnl # 2.6.16 API change, set_normalize_timespec() moved to time.c
dnl # previously it was available in time.h as an inline.
dnl #
AC_DEFUN([SPL_AC_SET_NORMALIZED_TIMESPEC_INLINE], [
	AC_MSG_CHECKING([whether set_normalized_timespec() is an inline])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/time.h>
		void set_normalized_timespec(struct timespec *ts,
		                             time_t sec, long nsec) { }
	],
	[],
	[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SET_NORMALIZED_TIMESPEC_INLINE, 1,
		          [set_normalized_timespec() is available as inline])
	])
])

dnl #
dnl # 2.6.18 API change,
dnl # timespec_sub() inline function available in linux/time.h
dnl #
AC_DEFUN([SPL_AC_TIMESPEC_SUB], [
	AC_MSG_CHECKING([whether timespec_sub() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/time.h>
	],[
		struct timespec a, b, c = { 0 };
		c = timespec_sub(a, b);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_TIMESPEC_SUB, 1, [timespec_sub() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.19 API change,
dnl # check if init_utsname() is available in linux/utsname.h
dnl #
AC_DEFUN([SPL_AC_INIT_UTSNAME], [
	AC_MSG_CHECKING([whether init_utsname() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/utsname.h>
	],[
		struct new_utsname *a = init_utsname();
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INIT_UTSNAME, 1, [init_utsname() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.26 API change,
dnl # definition of struct fdtable relocated to linux/fdtable.h
dnl #
AC_DEFUN([SPL_AC_FDTABLE_HEADER], [
	SPL_CHECK_HEADER([linux/fdtable.h], [FDTABLE], [], [])
])

dnl #
dnl # 2.6.14 API change,
dnl # check whether 'files_fdtable()' exists
dnl #
AC_DEFUN([SPL_AC_FILES_FDTABLE], [
	AC_MSG_CHECKING([whether files_fdtable() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sched.h>
		#include <linux/file.h>
		#ifdef HAVE_FDTABLE_HEADER
		#include <linux/fdtable.h>
		#endif
	],[
		struct files_struct *files = current->files;
		struct fdtable *fdt = files_fdtable(files);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FILES_FDTABLE, 1, [files_fdtable() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.18 API change,
dnl # added linux/uaccess.h
dnl #
AC_DEFUN([SPL_AC_UACCESS_HEADER], [
	SPL_CHECK_HEADER([linux/uaccess.h], [UACCESS], [], [])
])

dnl #
dnl # 2.6.12 API change,
dnl # check whether 'kmalloc_node()' is available.
dnl #
AC_DEFUN([SPL_AC_KMALLOC_NODE], [
	AC_MSG_CHECKING([whether kmalloc_node() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/slab.h>
	],[
		void *a = kmalloc_node(1, GFP_KERNEL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_KMALLOC_NODE, 1, [kmalloc_node() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.9 API change,
dnl # check whether 'monotonic_clock()' is available it may
dnl # be available for some archs but not others.
dnl #
AC_DEFUN([SPL_AC_MONOTONIC_CLOCK], [
	SPL_CHECK_SYMBOL_EXPORT(
		[monotonic_clock],
		[],
		[AC_DEFINE(HAVE_MONOTONIC_CLOCK, 1,
		[monotonic_clock() is available])],
		[])
])

dnl #
dnl # 2.6.16 API change,
dnl # check whether 'struct inode' has i_mutex
dnl #
AC_DEFUN([SPL_AC_INODE_I_MUTEX], [
	AC_MSG_CHECKING([whether struct inode has i_mutex])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
		#include <linux/mutex.h>
	],[
		struct inode i;
		mutex_init(&i.i_mutex);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_INODE_I_MUTEX, 1, [struct inode has i_mutex])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.29 API change,
dnl # Adaptive mutexs introduced.
dnl #
AC_DEFUN([SPL_AC_MUTEX_OWNER], [
	AC_MSG_CHECKING([whether struct mutex has owner])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mutex.h>
	],[
		struct mutex mtx;
		mtx.owner = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MUTEX_OWNER, 1, [struct mutex has owner])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.18 API change,
dnl # First introduced 'mutex_lock_nested()' in include/linux/mutex.h,
dnl # as part of the mutex validator.  Fallback to using 'mutex_lock()' 
dnl # if the mutex validator is disabled or otherwise unavailable.
dnl #
AC_DEFUN([SPL_AC_MUTEX_LOCK_NESTED], [
	AC_MSG_CHECKING([whether mutex_lock_nested() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mutex.h>
	],[
		struct mutex mutex;
		mutex_init(&mutex);
		mutex_lock_nested(&mutex, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_MUTEX_LOCK_NESTED, 1,
		[mutex_lock_nested() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.27 API change,
dnl # on_each_cpu() uses 3 args, no 'retry' argument
dnl #
AC_DEFUN([SPL_AC_3ARGS_ON_EACH_CPU], [
	AC_MSG_CHECKING([whether on_each_cpu() wants 3 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/smp.h>
	],[
		on_each_cpu(NULL, NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_ON_EACH_CPU, 1,
		          [on_each_cpu wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.18 API change,
dnl # kallsyms_lookup_name no longer exported
dnl #
AC_DEFUN([SPL_AC_KALLSYMS_LOOKUP_NAME], [
	SPL_CHECK_SYMBOL_EXPORT(
		[kallsyms_lookup_name],
		[],
		[AC_DEFINE(HAVE_KALLSYMS_LOOKUP_NAME, 1,
		[kallsyms_lookup_name() is available])],
		[])
])

dnl #
dnl # Proposed API change,
dnl # This symbol is not available in stock kernels.  You may build a
dnl # custom kernel with the *-spl-export-symbols.patch which will export
dnl # these symbols for use.  If your already rolling a custom kernel for
dnl # your environment this is recommended.
dnl #
AC_DEFUN([SPL_AC_GET_VMALLOC_INFO], [
	SPL_CHECK_SYMBOL_EXPORT(
		[get_vmalloc_info],
		[],
		[AC_DEFINE(HAVE_GET_VMALLOC_INFO, 1,
		[get_vmalloc_info() is available])],
		[])
])

dnl #
dnl # 2.6.17 API change
dnl # The helper functions first_online_pgdat(), next_online_pgdat(), and
dnl # next_zone() are introduced to simplify for_each_zone().  These symbols
dnl # were exported in 2.6.17 for use by modules which was consistent with
dnl # the previous implementation of for_each_zone().  From 2.6.18 - 2.6.19
dnl # the symbols were exported as 'unused', and by 2.6.20 they exports
dnl # were dropped entirely leaving modules no way to directly iterate over
dnl # the zone list.  Because we need access to the zone helpers we check
dnl # if the kernel contains the old or new implementation.  Then we check
dnl # to see if the symbols we need for each version are available.  If they
dnl # are not, dynamically aquire the addresses with kallsyms_lookup_name().
dnl #
AC_DEFUN([SPL_AC_PGDAT_HELPERS], [
	AC_MSG_CHECKING([whether symbol *_pgdat exist])
	grep -q -E 'first_online_pgdat' $LINUX/include/linux/mmzone.h 2>/dev/null
	rc=$?
	if test $rc -eq 0; then
		AC_MSG_RESULT([yes])
                AC_DEFINE(HAVE_PGDAT_HELPERS, 1, [pgdat helpers are available])
	else
		AC_MSG_RESULT([no])
	fi
])

dnl #
dnl # Proposed API change,
dnl # This symbol is not available in stock kernels.  You may build a
dnl # custom kernel with the *-spl-export-symbols.patch which will export
dnl # these symbols for use.  If your already rolling a custom kernel for
dnl # your environment this is recommended.
dnl #
AC_DEFUN([SPL_AC_FIRST_ONLINE_PGDAT], [
	SPL_CHECK_SYMBOL_EXPORT(
		[first_online_pgdat],
		[],
		[AC_DEFINE(HAVE_FIRST_ONLINE_PGDAT, 1,
		[first_online_pgdat() is available])],
		[])
])

dnl #
dnl # Proposed API change,
dnl # This symbol is not available in stock kernels.  You may build a
dnl # custom kernel with the *-spl-export-symbols.patch which will export
dnl # these symbols for use.  If your already rolling a custom kernel for
dnl # your environment this is recommended.
dnl #
AC_DEFUN([SPL_AC_NEXT_ONLINE_PGDAT], [
	SPL_CHECK_SYMBOL_EXPORT(
		[next_online_pgdat],
		[],
		[AC_DEFINE(HAVE_NEXT_ONLINE_PGDAT, 1,
		[next_online_pgdat() is available])],
		[])
])

dnl #
dnl # Proposed API change,
dnl # This symbol is not available in stock kernels.  You may build a
dnl # custom kernel with the *-spl-export-symbols.patch which will export
dnl # these symbols for use.  If your already rolling a custom kernel for
dnl # your environment this is recommended.
dnl #
AC_DEFUN([SPL_AC_NEXT_ZONE], [
	SPL_CHECK_SYMBOL_EXPORT(
		[next_zone],
		[],
		[AC_DEFINE(HAVE_NEXT_ZONE, 1,
		[next_zone() is available])],
		[])
])

dnl #
dnl # 2.6.17 API change,
dnl # See SPL_AC_PGDAT_HELPERS for details.
dnl #
AC_DEFUN([SPL_AC_PGDAT_LIST], [
	SPL_CHECK_SYMBOL_EXPORT(
		[pgdat_list],
		[],
		[AC_DEFINE(HAVE_PGDAT_LIST, 1,
		[pgdat_list is available])],
		[])
])

dnl #
dnl # 2.6.18 API change,
dnl # First introduced global_page_state() support as an inline.
dnl #
AC_DEFUN([SPL_AC_GLOBAL_PAGE_STATE], [
	AC_MSG_CHECKING([whether global_page_state() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		unsigned long state;
		state = global_page_state(0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_GLOBAL_PAGE_STATE, 1,
		          [global_page_state() is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.21 API change (plus subsequent naming convention changes),
dnl # Public global zone stats now include a free page count.  However
dnl # the enumerated names of the counters have changed since this API
dnl # was introduced.  We need to deduce the corrent name to use.  This
dnl # replaces the priviate get_zone_counts() interface.
dnl #
dnl # NR_FREE_PAGES was available from 2.6.21 to current kernels, which
dnl # is 2.6.30 as of when this was written.
dnl #
AC_DEFUN([SPL_AC_ZONE_STAT_ITEM_FREE], [
	AC_MSG_CHECKING([whether page state NR_FREE_PAGES is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_FREE_PAGES;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_FREE_PAGES, 1,
		          [Page state NR_FREE_PAGES is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.21 API change (plus subsequent naming convention changes),
dnl # Public global zone stats now include an inactive page count.  However
dnl # the enumerated names of the counters have changed since this API
dnl # was introduced.  We need to deduce the corrent name to use.  This
dnl # replaces the priviate get_zone_counts() interface.
dnl #
dnl # NR_INACTIVE was available from 2.6.21 to 2.6.27 and included both
dnl # anonymous and file inactive pages.  As of 2.6.28 it was split in
dnl # to NR_INACTIVE_ANON and NR_INACTIVE_FILE.
dnl #
AC_DEFUN([SPL_AC_ZONE_STAT_ITEM_INACTIVE], [
	AC_MSG_CHECKING([whether page state NR_INACTIVE is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_INACTIVE;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_INACTIVE, 1,
		          [Page state NR_INACTIVE is available])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether page state NR_INACTIVE_ANON is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_INACTIVE_ANON;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_INACTIVE_ANON, 1,
		          [Page state NR_INACTIVE_ANON is available])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether page state NR_INACTIVE_FILE is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_INACTIVE_FILE;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_INACTIVE_FILE, 1,
		          [Page state NR_INACTIVE_FILE is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.21 API change (plus subsequent naming convention changes),
dnl # Public global zone stats now include an active page count.  However
dnl # the enumerated names of the counters have changed since this API
dnl # was introduced.  We need to deduce the corrent name to use.  This
dnl # replaces the priviate get_zone_counts() interface.
dnl #
dnl # NR_ACTIVE was available from 2.6.21 to 2.6.27 and included both
dnl # anonymous and file active pages.  As of 2.6.28 it was split in
dnl # to NR_ACTIVE_ANON and NR_ACTIVE_FILE.
dnl #
AC_DEFUN([SPL_AC_ZONE_STAT_ITEM_ACTIVE], [
	AC_MSG_CHECKING([whether page state NR_ACTIVE is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_ACTIVE;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_ACTIVE, 1,
		          [Page state NR_ACTIVE is available])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether page state NR_ACTIVE_ANON is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_ACTIVE_ANON;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_ACTIVE_ANON, 1,
		          [Page state NR_ACTIVE_ANON is available])
	],[
		AC_MSG_RESULT(no)
	])

	AC_MSG_CHECKING([whether page state NR_ACTIVE_FILE is available])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/mm.h>
	],[
		enum zone_stat_item zsi;
		zsi = NR_ACTIVE_FILE;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_ZONE_STAT_ITEM_NR_ACTIVE_FILE, 1,
		          [Page state NR_ACTIVE_FILE is available])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Proposed API change for legacy kernels.
dnl # This symbol is not available in older kernels.  For kernels post
dnl # 2.6.21 the global_page_state() API is used to get free/inactive/active
dnl # page state information.  This symbol is only used in legacy kernels
dnl # any only as a last resort.
dnl
AC_DEFUN([SPL_AC_GET_ZONE_COUNTS], [
	AC_MSG_CHECKING([whether symbol get_zone_counts is needed])
	SPL_LINUX_TRY_COMPILE([
	],[
		#if !defined(HAVE_ZONE_STAT_ITEM_NR_FREE_PAGES)
		#error "global_page_state needs NR_FREE_PAGES"
		#endif

		#if !defined(HAVE_ZONE_STAT_ITEM_NR_ACTIVE) && \
		    !defined(HAVE_ZONE_STAT_ITEM_NR_ACTIVE_ANON) && \
		    !defined(HAVE_ZONE_STAT_ITEM_NR_ACTIVE_FILE)
		#error "global_page_state needs NR_ACTIVE*"
		#endif

		#if !defined(HAVE_ZONE_STAT_ITEM_NR_INACTIVE) && \
		    !defined(HAVE_ZONE_STAT_ITEM_NR_INACTIVE_ANON) && \
		    !defined(HAVE_ZONE_STAT_ITEM_NR_INACTIVE_FILE)
		#error "global_page_state needs NR_INACTIVE*"
		#endif
	],[
		AC_MSG_RESULT(no)
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(NEED_GET_ZONE_COUNTS, 1,
		          [get_zone_counts() is needed])

		SPL_CHECK_SYMBOL_EXPORT(
			[get_zone_counts],
			[],
			[AC_DEFINE(HAVE_GET_ZONE_COUNTS, 1,
			[get_zone_counts() is available])],
			[])
	])
])

dnl #
dnl # 2.6.27 API change,
dnl # The user_path_dir() replaces __user_walk()
dnl #
AC_DEFUN([SPL_AC_USER_PATH_DIR], [
	SPL_CHECK_SYMBOL_EXPORT(
		[user_path_at],
		[],
		[AC_DEFINE(HAVE_USER_PATH_DIR, 1,
		[user_path_dir() is available])],
		[])
])

dnl #
dnl # Symbol available in RHEL kernels not in stock kernels.
dnl #
AC_DEFUN([SPL_AC_SET_FS_PWD], [
	SPL_CHECK_SYMBOL_EXPORT(
		[set_fs_pwd],
		[],
		[AC_DEFINE(HAVE_SET_FS_PWD, 1,
		[set_fs_pwd() is available])],
		[])
])

dnl #
dnl # 2.6.25 API change,
dnl # Simplied API by replacing mnt+dentry args with a single path arg.
dnl #
AC_DEFUN([SPL_AC_2ARGS_SET_FS_PWD],
	[AC_MSG_CHECKING([whether set_fs_pwd() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sched.h>
		#include <linux/fs_struct.h>
	],[
		set_fs_pwd(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_SET_FS_PWD, 1,
		          [set_fs_pwd() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # SLES API change, never adopted in mainline,
dnl # Third 'struct vfsmount *' argument removed.
dnl #
AC_DEFUN([SPL_AC_2ARGS_VFS_UNLINK],
	[AC_MSG_CHECKING([whether vfs_unlink() wants 2 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_unlink(NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_VFS_UNLINK, 1,
		          [vfs_unlink() wants 2 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # SLES API change, never adopted in mainline,
dnl # Third and sixth 'struct vfsmount *' argument removed.
dnl #
AC_DEFUN([SPL_AC_4ARGS_VFS_RENAME],
	[AC_MSG_CHECKING([whether vfs_rename() wants 4 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		vfs_rename(NULL, NULL, NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_4ARGS_VFS_RENAME, 1,
		          [vfs_rename() wants 4 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.36 API change,
dnl # The 'struct fs_struct->lock' was changed from a rwlock_t to
dnl # a spinlock_t to improve the fastpath performance.
dnl #
AC_DEFUN([SPL_AC_FS_STRUCT_SPINLOCK], [
	AC_MSG_CHECKING([whether struct fs_struct uses spinlock_t])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sched.h>
		#include <linux/fs_struct.h>
	],[
		struct fs_struct fs;
		spin_lock_init(&fs.lock);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FS_STRUCT_SPINLOCK, 1,
		          [struct fs_struct uses spinlock_t])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.29 API change,
dnl # check whether 'struct cred' exists
dnl #
AC_DEFUN([SPL_AC_CRED_STRUCT], [
	AC_MSG_CHECKING([whether struct cred exists])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/cred.h>
	],[
		struct cred *cr;
		cr  = NULL;
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CRED_STRUCT, 1, [struct cred exists])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # Custom SPL patch may export this symbol.
dnl #
AC_DEFUN([SPL_AC_GROUPS_SEARCH], [
	SPL_CHECK_SYMBOL_EXPORT(
		[groups_search],
		[],
		[AC_DEFINE(HAVE_GROUPS_SEARCH, 1,
		[groups_search() is available])],
		[])
])

dnl #
dnl # 2.6.x API change,
dnl # __put_task_struct() was exported in RHEL5 but unavailable elsewhere.
dnl #
AC_DEFUN([SPL_AC_PUT_TASK_STRUCT], [
	SPL_CHECK_SYMBOL_EXPORT(
		[__put_task_struct],
		[],
		[AC_DEFINE(HAVE_PUT_TASK_STRUCT, 1,
		[__put_task_struct() is available])],
		[])
])

dnl #
dnl # 2.6.32 API change,
dnl # Unused 'struct file *' removed from prototype.
dnl #
AC_DEFUN([SPL_AC_5ARGS_PROC_HANDLER], [
	AC_MSG_CHECKING([whether proc_handler() wants 5 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/sysctl.h>
	],[
		proc_dostring(NULL, 0, NULL, NULL, NULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_5ARGS_PROC_HANDLER, 1,
		          [proc_handler() wants 5 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.x API change,
dnl # kvasprintf() function added.
dnl #
AC_DEFUN([SPL_AC_KVASPRINTF], [
	SPL_CHECK_SYMBOL_EXPORT(
		[kvasprintf],
		[],
		[AC_DEFINE(HAVE_KVASPRINTF, 1,
		[kvasprintf() is available])],
		[])
])

dnl #
dnl # 2.6.35 API change,
dnl # Unused 'struct dentry *' removed from prototype.
dnl #
AC_DEFUN([SPL_AC_3ARGS_FILE_FSYNC], [
	AC_MSG_CHECKING([whether file_fsync() wants 3 args])
	SPL_LINUX_TRY_COMPILE([
		#include <linux/buffer_head.h>
	],[
		file_fsync(NULL, NULL, 0);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_3ARGS_FILE_FSYNC, 1,
		          [file_fsync() wants 3 args])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.33 API change. Also backported in RHEL5 as of 2.6.18-190.el5.
dnl # Earlier versions of rwsem_is_locked() were inline and had a race
dnl # condition.  The fixed version is exported as a symbol.  The race
dnl # condition is fixed by acquiring sem->wait_lock, so we must not
dnl # call that version while holding sem->wait_lock.
dnl #
AC_DEFUN([SPL_AC_EXPORTED_RWSEM_IS_LOCKED], [
	SPL_CHECK_SYMBOL_EXPORT(
		[rwsem_is_locked],
		[lib/rwsem-spinlock.c],
		[AC_DEFINE(RWSEM_IS_LOCKED_TAKES_WAIT_LOCK, 1,
		[rwsem_is_locked() acquires sem->wait_lock])],
		[])
])
