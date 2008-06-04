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
	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
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
dnl # 2.6.x API change
dnl # Slab can now be implemented in terms of the Slub which provides
dnl # slightly different semantics in terms of merged caches.
dnl #
AC_DEFUN([SPL_AC_SLUB], [
	SPL_LINUX_CONFIG([SLUB],
	        [AC_DEFINE(HAVE_SLUB, 1, [slub support configured])],
                [])
])

dnl #
dnl # 2.6.x API change
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
dnl # 2.6.x API change
dnl # check if kmem_cache_t typedef is defined
dnl #
AC_DEFUN([SPL_AC_TYPE_KMEM_CACHE_T],
	[AC_MSG_CHECKING([whether kernel defines kmem_cache_t])
	SPL_LINUX_TRY_COMPILE([
	        #include <linux/slab.h>
	],[
	        kmem_cache_t *cache;
	],[
	        AC_MSG_RESULT([yes])
	        AC_DEFINE(HAVE_KMEM_CACHE_T, 1, 
		          [kernel defines kmem_cache_t])
	],[
	        AC_MSG_RESULT([no])
	])
])

dnl #
dnl # 2.6.19 API change
dnl # kmem_cache_destroy() return void instead of int
dnl #
AC_DEFUN([SPL_AC_KMEM_CACHE_DESTROY_INT],
	[AC_MSG_CHECKING([whether kmem_cache_destroy() returns int])
	SPL_LINUX_TRY_COMPILE([
	        #include <linux/slab.h>
	],[
	        int i = kmem_cache_destroy(NULL);
	],[
	        AC_MSG_RESULT(yes)
	        AC_DEFINE(HAVE_KMEM_CACHE_DESTROY_INT, 1,
	                [kmem_cache_destroy() returns int])
	],[
	        AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.19 API change
dnl # panic_notifier_list use atomic_notifier operations
dnl #

AC_DEFUN([SPL_AC_ATOMIC_PANIC_NOTIFIER],
	[AC_MSG_CHECKING([whether panic_notifier_list is atomic])
	SPL_LINUX_TRY_COMPILE([
	        #include <linux/notifier.h>
	        #include <linux/kernel.h>
	],[
	        struct atomic_notifier_head panic_notifier_list;
	],[
	        AC_MSG_RESULT(yes)
	        AC_DEFINE(HAVE_ATOMIC_PANIC_NOTIFIER, 1,
	                [panic_notifier_list is atomic])
	],[
	        AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.20 API change
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
dnl # 2.6.21 api change.
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
dnl # 2.6.21 API change
dnl # Use struct kmem_cache for missing kmem_cache_t
dnl #
AC_DEFUN([SPL_AC_KMEM_CACHE_T], [
	AC_MSG_CHECKING([whether kernel has kmem_cache_t])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
	        #include <linux/slab.h>
	],[
	        kmem_cache_t *cachep = NULL;
	        kmem_cache_free(cachep, NULL);

	],[
	        AC_MSG_RESULT([yes])
	        AC_DEFINE(HAVE_KMEM_CACHE_T, 1,
	                  [kernel has struct kmem_cache_t])
	],[
        	AC_MSG_RESULT([no])
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 2.6.23 API change
dnl # Slab no longer accepts a dtor argument
dnl # 
AC_DEFUN([SPL_AC_KMEM_CACHE_CREATE_DTOR],
	[AC_MSG_CHECKING([whether kmem_cache_create() has dtor arg])
	SPL_LINUX_TRY_COMPILE([
	        #include <linux/slab.h>
	],[
	        kmem_cache_create(NULL, 0, 0, 0, NULL, NULL);
	],[
	        AC_MSG_RESULT(yes)
	        AC_DEFINE(HAVE_KMEM_CACHE_CREATE_DTOR, 1,
	                  [kmem_cache_create() has dtor arg])
	],[
	        AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 2.6.x API change
dnl # Slab ctor no longer takes 3 args
dnl # 
AC_DEFUN([SPL_AC_3ARG_KMEM_CACHE_CREATE_CTOR],
	[AC_MSG_CHECKING([whether slab ctor wants 3 args])
	tmp_flags="$EXTRA_KCFLAGS"
	EXTRA_KCFLAGS="-Werror"
	SPL_LINUX_TRY_COMPILE([
	        #include <linux/slab.h>
	],[
		void (*ctor)(void *,struct kmem_cache *,unsigned long) = NULL;

		#ifdef HAVE_KMEM_CACHE_CREATE_DTOR
	        kmem_cache_create(NULL, 0, 0, 0, ctor, NULL);
		#else
	        kmem_cache_create(NULL, 0, 0, 0, ctor);
		#endif
	],[
	        AC_MSG_RESULT(yes)
	        AC_DEFINE(HAVE_3ARG_KMEM_CACHE_CREATE_CTOR, 1,
	                  [slab ctor wants 3 args])
	],[
	        AC_MSG_RESULT(no)
	])
	EXTRA_KCFLAGS="$tmp_flags"
])

dnl #
dnl # 2.6.x API change
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
dnl # 2.6.x API change
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
dnl # 2.6.x API change
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
