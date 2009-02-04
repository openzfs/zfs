AC_DEFUN([SPL_AC_KERNEL], [
	ver=`uname -r`

	AC_ARG_WITH([linux],
		AS_HELP_STRING([--with-linux=PATH],
		[Path to kernel source]),
		[kernelsrc="$withval"; kernelbuild="$withval"])

	AC_ARG_WITH([linux-obj],
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

AC_DEFUN([SPL_AC_LICENSE], [
        AC_MSG_CHECKING([license])
        AC_MSG_RESULT([GPL])
	KERNELCPPFLAGS="${KERNELCPPFLAGS} -DHAVE_GPL_ONLY_SYMBOLS"
])

AC_DEFUN([SPL_AC_DEBUG], [
	AC_MSG_CHECKING([whether debugging is enabled])
	AC_ARG_ENABLE( [debug],
		AS_HELP_STRING([--enable-debug],
		[Enable generic debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug=yes ;;
			no)  spl_ac_debug=no  ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug]) ;;
		esac ]
	) 
	if test "$spl_ac_debug" = yes; then
		AC_MSG_RESULT([yes])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG"
	else
		AC_MSG_RESULT([no])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DNDEBUG"
	fi
])

AC_DEFUN([SPL_AC_DEBUG_KMEM], [
	AC_MSG_CHECKING([whether kmem debugging is enabled])
	AC_ARG_ENABLE( [debug-kmem],
		AS_HELP_STRING([--enable-debug-kmem],
		[Enable kmem debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug_kmem=yes ;;
			no)  spl_ac_debug_kmem=no  ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-kmem]) ;;
		esac ]
	) 
	if test "$spl_ac_debug_kmem" = yes; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([DEBUG_KMEM], [1],
		[Define to 1 to enable kmem debugging])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KMEM"
	else
		AC_MSG_RESULT([no])
	fi
])

AC_DEFUN([SPL_AC_DEBUG_MUTEX], [
	AC_MSG_CHECKING([whether mutex debugging is enabled])
	AC_ARG_ENABLE( [debug-mutex],
		AS_HELP_STRING([--enable-debug-mutex],
		[Enable mutex debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug_mutex=yes ;;
			no)  spl_ac_debug_mutex=no  ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-mutex]) ;;
		esac ]
	) 
	if test "$spl_ac_debug_mutex" = yes; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([DEBUG_MUTEX], [1],
		[Define to 1 to enable mutex debugging])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_MUTEX"
	else
		AC_MSG_RESULT([no])
	fi
])

AC_DEFUN([SPL_AC_DEBUG_KSTAT], [
	AC_MSG_CHECKING([whether kstat debugging is enabled])
	AC_ARG_ENABLE( [debug-kstat],
		AS_HELP_STRING([--enable-debug-kstat],
		[Enable kstat debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug_kstat=yes ;;
			no)  spl_ac_debug_kstat=no  ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-kstat]) ;;
		esac ]
	) 
	if test "$spl_ac_debug_kstat" = yes; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([DEBUG_KSTAT], [1],
		[Define to 1 to enable kstat debugging])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_KSTAT"
	else
		AC_MSG_RESULT([no])
	fi
])

AC_DEFUN([SPL_AC_DEBUG_CALLB], [
	AC_MSG_CHECKING([whether callb debugging is enabled])
	AC_ARG_ENABLE( [debug-callb],
		AS_HELP_STRING([--enable-debug-callb],
		[Enable callb debug support (default off)]),
		[ case "$enableval" in
			yes) spl_ac_debug_callb=yes ;;
			no)  spl_ac_debug_callb=no  ;;
			*) AC_MSG_RESULT([Error!])
			AC_MSG_ERROR([Bad value "$enableval" for --enable-debug-callb]) ;;
		esac ]
	) 
	if test "$spl_ac_debug_callb" = yes; then
		AC_MSG_RESULT([yes])
		AC_DEFINE([DEBUG_CALLB], [1],
		[Define to 1 to enable callb debugging])
		KERNELCPPFLAGS="${KERNELCPPFLAGS} -DDEBUG_CALLB"
	else
		AC_MSG_RESULT([no])
	fi
])

dnl #
dnl # SPL_LINUX_CONFTEST
dnl #
AC_DEFUN([SPL_LINUX_CONFTEST], [
cat >conftest.c <<_ACEOF
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
m4_ifvaln([$1], [SPL_LINUX_CONFTEST([$1])])dnl
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
dnl # 2.6.x API change,
dnl # check if atomic64_t typedef is defined
dnl #
AC_DEFUN([SPL_AC_TYPE_ATOMIC64_T],
	[AC_MSG_CHECKING([whether kernel defines atomic64_t])
	SPL_LINUX_TRY_COMPILE([
		#include <asm/atomic.h>
	],[
		atomic64_t *ptr;
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_ATOMIC64_T, 1,
		          [kernel defines atomic64_t])
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
dnl # 2.6.14 API change,
dnl # check whether 'div64_64()' is available
dnl #
AC_DEFUN([SPL_AC_DIV64_64], [
	AC_MSG_CHECKING([whether div64_64() is available])
	SPL_LINUX_TRY_COMPILE([
		#include <asm/div64.h>
		#include <linux/types.h>
	],[
		uint64_t i = div64_64(1ULL, 1ULL);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_DIV64_64, 1, [div64_64() is available])
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
dnl # Distro specific first_online_pgdat symbol export.
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
dnl # Distro specific next_online_pgdat symbol export.
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
dnl # Distro specific next_zone symbol export.
dnl #
AC_DEFUN([SPL_AC_NEXT_ZONE], [
	SPL_CHECK_SYMBOL_EXPORT(
		[next_zone],
		[],
		[AC_DEFINE(HAVE_NEXT_ZONE, 1,
		[next_zone() is available])],
		[])
])
