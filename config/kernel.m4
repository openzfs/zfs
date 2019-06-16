dnl #
dnl # Default ZFS kernel configuration
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL], [
	ZFS_AC_KERNEL
	ZFS_AC_QAT
	ZFS_AC_KERNEL_ACCESS_OK_TYPE
	ZFS_AC_TEST_MODULE
	ZFS_AC_KERNEL_MISC_MINOR
	ZFS_AC_KERNEL_OBJTOOL
	ZFS_AC_KERNEL_CONFIG
	ZFS_AC_KERNEL_CTL_NAME
	ZFS_AC_KERNEL_PDE_DATA
	ZFS_AC_KERNEL_2ARGS_VFS_FSYNC
	ZFS_AC_KERNEL_KUIDGID_T
	ZFS_AC_KERNEL_FALLOCATE
	ZFS_AC_KERNEL_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE
	ZFS_AC_KERNEL_RWSEM_SPINLOCK_IS_RAW
	ZFS_AC_KERNEL_RWSEM_ACTIVITY
	ZFS_AC_KERNEL_RWSEM_ATOMIC_LONG_COUNT
	ZFS_AC_KERNEL_SCHED_RT_HEADER
	ZFS_AC_KERNEL_SCHED_SIGNAL_HEADER
	ZFS_AC_KERNEL_IO_SCHEDULE_TIMEOUT
	ZFS_AC_KERNEL_4ARGS_VFS_GETATTR
	ZFS_AC_KERNEL_3ARGS_VFS_GETATTR
	ZFS_AC_KERNEL_2ARGS_VFS_GETATTR
	ZFS_AC_KERNEL_USLEEP_RANGE
	ZFS_AC_KERNEL_KVMALLOC
	ZFS_AC_KERNEL_KMEM_CACHE_ALLOCFLAGS
	ZFS_AC_KERNEL_KMEM_CACHE_CREATE_USERCOPY
	ZFS_AC_KERNEL_WAIT_ON_BIT
	ZFS_AC_KERNEL_WAIT_QUEUE_ENTRY_T
	ZFS_AC_KERNEL_WAIT_QUEUE_HEAD_ENTRY
	ZFS_AC_KERNEL_INODE_TIMES
	ZFS_AC_KERNEL_INODE_LOCK
	ZFS_AC_KERNEL_GROUP_INFO_GID
	ZFS_AC_KERNEL_WRITE
	ZFS_AC_KERNEL_READ
	ZFS_AC_KERNEL_TIMER_SETUP
	ZFS_AC_KERNEL_DECLARE_EVENT_CLASS
	ZFS_AC_KERNEL_CURRENT_BIO_TAIL
	ZFS_AC_KERNEL_SUPER_USER_NS
	ZFS_AC_KERNEL_SUBMIT_BIO
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_CHECK_EVENTS
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS_RELEASE_VOID
	ZFS_AC_KERNEL_TYPE_FMODE_T
	ZFS_AC_KERNEL_BLKDEV_GET_BY_PATH
	ZFS_AC_KERNEL_BLKDEV_REREAD_PART
	ZFS_AC_KERNEL_OPEN_BDEV_EXCLUSIVE
	ZFS_AC_KERNEL_LOOKUP_BDEV
	ZFS_AC_KERNEL_INVALIDATE_BDEV_ARGS
	ZFS_AC_KERNEL_BDEV_LOGICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BDEV_PHYSICAL_BLOCK_SIZE
	ZFS_AC_KERNEL_BIO_BVEC_ITER
	ZFS_AC_KERNEL_BIO_FAILFAST_DTD
	ZFS_AC_KERNEL_BIO_SET_DEV
	ZFS_AC_KERNEL_REQ_FAILFAST_MASK
	ZFS_AC_KERNEL_REQ_OP_DISCARD
	ZFS_AC_KERNEL_REQ_OP_SECURE_ERASE
	ZFS_AC_KERNEL_REQ_OP_FLUSH
	ZFS_AC_KERNEL_BIO_BI_OPF
	ZFS_AC_KERNEL_BIO_END_IO_T_ARGS
	ZFS_AC_KERNEL_BIO_BI_STATUS
	ZFS_AC_KERNEL_BIO_RW_BARRIER
	ZFS_AC_KERNEL_BIO_RW_DISCARD
	ZFS_AC_KERNEL_BLK_QUEUE_BDI
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_CLEAR
	ZFS_AC_KERNEL_BLK_QUEUE_FLAG_SET
	ZFS_AC_KERNEL_BLK_QUEUE_FLUSH
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_HW_SECTORS
	ZFS_AC_KERNEL_BLK_QUEUE_MAX_SEGMENTS
	ZFS_AC_KERNEL_BLK_QUEUE_HAVE_BIO_RW_UNPLUG
	ZFS_AC_KERNEL_BLK_QUEUE_HAVE_BLK_PLUG
	ZFS_AC_KERNEL_GET_DISK_AND_MODULE
	ZFS_AC_KERNEL_GET_DISK_RO
	ZFS_AC_KERNEL_HAVE_BIO_SET_OP_ATTRS
	ZFS_AC_KERNEL_GENERIC_READLINK_GLOBAL
	ZFS_AC_KERNEL_DISCARD_GRANULARITY
	ZFS_AC_KERNEL_CONST_XATTR_HANDLER
	ZFS_AC_KERNEL_XATTR_HANDLER_NAME
	ZFS_AC_KERNEL_XATTR_HANDLER_GET
	ZFS_AC_KERNEL_XATTR_HANDLER_SET
	ZFS_AC_KERNEL_XATTR_HANDLER_LIST
	ZFS_AC_KERNEL_INODE_OWNER_OR_CAPABLE
	ZFS_AC_KERNEL_POSIX_ACL_FROM_XATTR_USERNS
	ZFS_AC_KERNEL_POSIX_ACL_RELEASE
	ZFS_AC_KERNEL_SET_CACHED_ACL_USABLE
	ZFS_AC_KERNEL_POSIX_ACL_CHMOD
	ZFS_AC_KERNEL_POSIX_ACL_EQUIV_MODE_WANTS_UMODE_T
	ZFS_AC_KERNEL_POSIX_ACL_VALID_WITH_NS
	ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION
	ZFS_AC_KERNEL_INODE_OPERATIONS_PERMISSION_WITH_NAMEIDATA
	ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_CHECK_ACL_WITH_FLAGS
	ZFS_AC_KERNEL_INODE_OPERATIONS_GET_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_SET_ACL
	ZFS_AC_KERNEL_INODE_OPERATIONS_GETATTR
	ZFS_AC_KERNEL_INODE_SET_FLAGS
	ZFS_AC_KERNEL_INODE_SET_IVERSION
	ZFS_AC_KERNEL_GET_ACL_HANDLE_CACHE
	ZFS_AC_KERNEL_SHOW_OPTIONS
	ZFS_AC_KERNEL_FILE_INODE
	ZFS_AC_KERNEL_FILE_DENTRY
	ZFS_AC_KERNEL_FSYNC
	ZFS_AC_KERNEL_EVICT_INODE
	ZFS_AC_KERNEL_DIRTY_INODE_WITH_FLAGS
	ZFS_AC_KERNEL_NR_CACHED_OBJECTS
	ZFS_AC_KERNEL_FREE_CACHED_OBJECTS
	ZFS_AC_KERNEL_FALLOCATE
	ZFS_AC_KERNEL_AIO_FSYNC
	ZFS_AC_KERNEL_MKDIR_UMODE_T
	ZFS_AC_KERNEL_LOOKUP_NAMEIDATA
	ZFS_AC_KERNEL_CREATE_NAMEIDATA
	ZFS_AC_KERNEL_GET_LINK
	ZFS_AC_KERNEL_PUT_LINK
	ZFS_AC_KERNEL_TMPFILE
	ZFS_AC_KERNEL_TRUNCATE_RANGE
	ZFS_AC_KERNEL_AUTOMOUNT
	ZFS_AC_KERNEL_ENCODE_FH_WITH_INODE
	ZFS_AC_KERNEL_COMMIT_METADATA
	ZFS_AC_KERNEL_CLEAR_INODE
	ZFS_AC_KERNEL_SETATTR_PREPARE
	ZFS_AC_KERNEL_INSERT_INODE_LOCKED
	ZFS_AC_KERNEL_D_MAKE_ROOT
	ZFS_AC_KERNEL_D_OBTAIN_ALIAS
	ZFS_AC_KERNEL_D_PRUNE_ALIASES
	ZFS_AC_KERNEL_D_SET_D_OP
	ZFS_AC_KERNEL_D_REVALIDATE_NAMEIDATA
	ZFS_AC_KERNEL_CONST_DENTRY_OPERATIONS
	ZFS_AC_KERNEL_TRUNCATE_SETSIZE
	ZFS_AC_KERNEL_6ARGS_SECURITY_INODE_INIT_SECURITY
	ZFS_AC_KERNEL_CALLBACK_SECURITY_INODE_INIT_SECURITY
	ZFS_AC_KERNEL_FST_MOUNT
	ZFS_AC_KERNEL_SHRINK
	ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SHRINK_CONTROL_STRUCT
	ZFS_AC_KERNEL_SHRINKER_CALLBACK
	ZFS_AC_KERNEL_S_INSTANCES_LIST_HEAD
	ZFS_AC_KERNEL_S_D_OP
	ZFS_AC_KERNEL_BDI
	ZFS_AC_KERNEL_SET_NLINK
	ZFS_AC_KERNEL_ELEVATOR_CHANGE
	ZFS_AC_KERNEL_5ARG_SGET
	ZFS_AC_KERNEL_LSEEK_EXECUTE
	ZFS_AC_KERNEL_VFS_ITERATE
	ZFS_AC_KERNEL_VFS_RW_ITERATE
	ZFS_AC_KERNEL_VFS_DIRECT_IO
	ZFS_AC_KERNEL_GENERIC_WRITE_CHECKS
	ZFS_AC_KERNEL_KMAP_ATOMIC_ARGS
	ZFS_AC_KERNEL_FOLLOW_DOWN_ONE
	ZFS_AC_KERNEL_MAKE_REQUEST_FN
	ZFS_AC_KERNEL_GENERIC_IO_ACCT_3ARG
	ZFS_AC_KERNEL_GENERIC_IO_ACCT_4ARG
	ZFS_AC_KERNEL_FPU
	ZFS_AC_KERNEL_KUID_HELPERS
	ZFS_AC_KERNEL_MODULE_PARAM_CALL_CONST
	ZFS_AC_KERNEL_RENAME_WANTS_FLAGS
	ZFS_AC_KERNEL_HAVE_GENERIC_SETXATTR
	ZFS_AC_KERNEL_CURRENT_TIME
	ZFS_AC_KERNEL_GLOBAL_PAGE_STATE
	ZFS_AC_KERNEL_ACL_HAS_REFCOUNT
	ZFS_AC_KERNEL_USERNS_CAPABILITIES
	ZFS_AC_KERNEL_IN_COMPAT_SYSCALL
	ZFS_AC_KERNEL_KTIME_GET_COARSE_REAL_TS64
	ZFS_AC_KERNEL_TOTALRAM_PAGES_FUNC
	ZFS_AC_KERNEL_TOTALHIGH_PAGES
	ZFS_AC_KERNEL_BLK_QUEUE_DISCARD
	ZFS_AC_KERNEL_BLK_QUEUE_SECURE_ERASE
	ZFS_AC_KERNEL_KSTRTOUL

	AS_IF([test "$LINUX_OBJ" != "$LINUX"], [
		KERNEL_MAKE="$KERNEL_MAKE O=$LINUX_OBJ"
	])

	AC_SUBST(KERNEL_MAKE)
])

dnl #
dnl # Detect name used for Module.symvers file in kernel
dnl #
AC_DEFUN([ZFS_AC_MODULE_SYMVERS], [
	modpost=$LINUX/scripts/Makefile.modpost
	AC_MSG_CHECKING([kernel file name for module symbols])
	AS_IF([test "x$enable_linux_builtin" != xyes -a -f "$modpost"], [
		AS_IF([grep -q Modules.symvers $modpost], [
			LINUX_SYMBOLS=Modules.symvers
		], [
			LINUX_SYMBOLS=Module.symvers
		])

		AS_IF([test ! -f "$LINUX_OBJ/$LINUX_SYMBOLS"], [
			AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed.  If you are building with a custom kernel, make sure the
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
			kernelsrc="[Not found]"
		])
	], [
		AS_IF([test "$kernelsrc" = "NONE"], [
			kernsrcver=NONE
		])
		withlinux=yes
	])

	AC_MSG_RESULT([$kernelsrc])
	AS_IF([test ! -d "$kernelsrc"], [
		AC_MSG_ERROR([
	*** Please make sure the kernel devel package for your distribution
	*** is installed and then try again.  If that fails, you can specify the
	*** location of the kernel source with the '--with-linux=PATH' option.])
	])

	AC_MSG_CHECKING([kernel build directory])
	AS_IF([test -z "$kernelbuild"], [
		AS_IF([test x$withlinux != xyes -a -e "/lib/modules/$(uname -r)/build"], [
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
		             ${CPP} -I $kernelbuild/include - |
		             grep "^kernsrcver=" | cut -d \" -f 2`

		AS_IF([test -z "$kernsrcver"], [
			AC_MSG_RESULT([Not found])
			AC_MSG_ERROR([*** Cannot determine kernel version.])
		])
	], [
		AC_MSG_RESULT([Not found])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([*** Cannot find UTS_RELEASE definition.])
		else
			AC_MSG_ERROR([
	*** Cannot find UTS_RELEASE definition.
	*** Please run 'make prepare' inside the kernel source tree.])
		fi
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
dnl # Detect the QAT module to be built against
dnl # QAT provides hardware acceleration for data compression:
dnl # 	https://01.org/intel-quickassist-technology
dnl # * Download and install QAT driver from the above link
dnl # * Start QAT driver in your system:
dnl # 	service qat_service start
dnl # * Enable QAT in ZFS, e.g.:
dnl # 	./configure --with-qat=<qat-driver-path>/QAT1.6
dnl #	make
dnl # * Set GZIP compression in ZFS dataset:
dnl # 	zfs set compression = gzip <dataset>
dnl # Then the data written to this ZFS pool is compressed
dnl # by QAT accelerator automatically, and de-compressed by
dnl # QAT when read from the pool.
dnl # * Get QAT hardware statistics by:
dnl #	cat /proc/icp_dh895xcc_dev/qat
dnl # * To disable QAT:
dnl # 	insmod zfs.ko zfs_qat_disable=1
dnl #
AC_DEFUN([ZFS_AC_QAT], [
	AC_ARG_WITH([qat],
		AS_HELP_STRING([--with-qat=PATH],
		[Path to qat source]),
		AS_IF([test "$withval" = "yes"],
			AC_MSG_ERROR([--with-qat=PATH requires a PATH]),
			[qatsrc="$withval"]))

	AC_ARG_WITH([qat-obj],
		AS_HELP_STRING([--with-qat-obj=PATH],
		[Path to qat build objects]),
		[qatbuild="$withval"])

	AS_IF([test ! -z "${qatsrc}"], [
		AC_MSG_CHECKING([qat source directory])
		AC_MSG_RESULT([$qatsrc])
		QAT_SRC="${qatsrc}/quickassist"
		AS_IF([ test ! -e "$QAT_SRC/include/cpa.h"], [
			AC_MSG_ERROR([
		*** Please make sure the qat driver package is installed
		*** and specify the location of the qat source with the
		*** '--with-qat=PATH' option then try again. Failed to
		*** find cpa.h in:
		${QAT_SRC}/include])
		])
	])

	AS_IF([test ! -z "${qatsrc}"], [
		AC_MSG_CHECKING([qat build directory])
		AS_IF([test -z "$qatbuild"], [
			qatbuild="${qatsrc}/build"
		])

		AC_MSG_RESULT([$qatbuild])
		QAT_OBJ=${qatbuild}
		AS_IF([ ! test -e "$QAT_OBJ/icp_qa_al.ko" && ! test -e "$QAT_OBJ/qat_api.ko"], [
			AC_MSG_ERROR([
		*** Please make sure the qat driver is installed then try again.
		*** Failed to find icp_qa_al.ko or qat_api.ko in:
		$QAT_OBJ])
		])

		AC_SUBST(QAT_SRC)
		AC_SUBST(QAT_OBJ)

		AC_DEFINE(HAVE_QAT, 1,
		[qat is enabled and existed])
	])

	dnl #
	dnl # Detect the name used for the QAT Module.symvers file.
	dnl #
	AS_IF([test ! -z "${qatsrc}"], [
		AC_MSG_CHECKING([qat file for module symbols])
		QAT_SYMBOLS=$QAT_SRC/lookaside/access_layer/src/Module.symvers

		AS_IF([test -r $QAT_SYMBOLS], [
			AC_MSG_RESULT([$QAT_SYMBOLS])
			AC_SUBST(QAT_SYMBOLS)
		],[
                       AC_MSG_ERROR([
			*** Please make sure the qat driver is installed then try again.
			*** Failed to find Module.symvers in:
			$QAT_SYMBOLS])
			])
		])
	])
])

dnl #
dnl # Basic toolchain sanity check.
dnl #
AC_DEFUN([ZFS_AC_TEST_MODULE], [
	AC_MSG_CHECKING([whether modules can be built])
	ZFS_LINUX_TRY_COMPILE([],[],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([*** Unable to build an empty module.])
		else
			AC_MSG_ERROR([
	*** Unable to build an empty module.
	*** Please run 'make scripts' inside the kernel source tree.])
		fi
	])
])

dnl #
dnl # Certain kernel build options are not supported.  These must be
dnl # detected at configure time and cause a build failure.  Otherwise
dnl # modules may be successfully built that behave incorrectly.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG], [
	AS_IF([test "x$cross_compiling" != xyes], [
		AC_RUN_IFELSE([
			AC_LANG_PROGRAM([
				#include "$LINUX/include/linux/license.h"
			], [
				return !license_is_gpl_compatible("$ZFS_META_LICENSE");
			])
		], [
			AC_DEFINE([ZFS_IS_GPL_COMPATIBLE], [1],
			    [Define to 1 if GPL-only symbols can be used])
		], [
		])
	])

	ZFS_AC_KERNEL_CONFIG_THREAD_SIZE
	ZFS_AC_KERNEL_CONFIG_DEBUG_LOCK_ALLOC
	ZFS_AC_KERNEL_CONFIG_TRIM_UNUSED_KSYMS
	ZFS_AC_KERNEL_CONFIG_ZLIB_INFLATE
	ZFS_AC_KERNEL_CONFIG_ZLIB_DEFLATE
])

dnl #
dnl # Check configured THREAD_SIZE
dnl #
dnl # The stack size will vary by architecture, but as of Linux 3.15 on x86_64
dnl # the default thread stack size was increased to 16K from 8K.  Therefore,
dnl # on newer kernels and some architectures stack usage optimizations can be
dnl # conditionally applied to improve performance without negatively impacting
dnl # stability.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_THREAD_SIZE], [
	AC_MSG_CHECKING([whether kernel was built with 16K or larger stacks])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/module.h>
	],[
		#if (THREAD_SIZE < 16384)
		#error "THREAD_SIZE is less than 16K"
		#endif
	],[
		AC_MSG_RESULT([yes])
		AC_DEFINE(HAVE_LARGE_STACKS, 1, [kernel has large stacks])
	],[
		AC_MSG_RESULT([no])
	])
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
	*** with the CDDL license and will prevent the module linking stage
	*** from succeeding.  You must rebuild your kernel without this
	*** option enabled.])
		])
		EXTRA_KCFLAGS="$tmp_flags"
	], [])
])

dnl #
dnl # Check CONFIG_TRIM_UNUSED_KSYMS
dnl #
dnl # Verify the kernel has CONFIG_TRIM_UNUSED_KSYMS disabled.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_CONFIG_TRIM_UNUSED_KSYMS], [
	AC_MSG_CHECKING([whether CONFIG_TRIM_UNUSED_KSYM is disabled])
	ZFS_LINUX_TRY_COMPILE([
		#if defined(CONFIG_TRIM_UNUSED_KSYMS)
		#error CONFIG_TRIM_UNUSED_KSYMS not defined
		#endif
	],[ ],[
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		AS_IF([test "x$enable_linux_builtin" != xyes], [
			AC_MSG_ERROR([
	*** This kernel has unused symbols trimming enabled, please disable.
	*** Rebuild the kernel with CONFIG_TRIM_UNUSED_KSYMS=n set.])
	])])
])

dnl #
dnl # ZFS_LINUX_CONFTEST_H
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_H], [
cat - <<_ACEOF >conftest.h
$1
_ACEOF
])

dnl #
dnl # ZFS_LINUX_CONFTEST_C
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_C], [
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
	m4_ifvaln([$1], [ZFS_LINUX_CONFTEST_C([$1])])
	m4_ifvaln([$6], [ZFS_LINUX_CONFTEST_H([$6])], [ZFS_LINUX_CONFTEST_H([])])
	rm -Rf build && mkdir -p build && touch build/conftest.mod.c
	echo "obj-m := conftest.o" >build/Makefile
	modpost_flag=''
	test "x$enable_linux_builtin" = xyes && modpost_flag='modpost=true' # fake modpost stage
	AS_IF(
		[AC_TRY_COMMAND(cp conftest.c conftest.h build && make [$2] -C $LINUX_OBJ EXTRA_CFLAGS="-Werror $FRAME_LARGER_THAN $EXTRA_KCFLAGS" $ARCH_UM M=$PWD/build $modpost_flag) >/dev/null && AC_TRY_COMMAND([$3])],
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
	[AC_MSG_CHECKING([whether kernel was built with CONFIG_$1])
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
AC_DEFUN([ZFS_CHECK_SYMBOL_EXPORT], [
	grep -q -E '[[[:space:]]]$1[[[:space:]]]' \
		$LINUX_OBJ/$LINUX_SYMBOLS 2>/dev/null
	rc=$?
	if test $rc -ne 0; then
		export=0
		for file in $2; do
			grep -q -E "EXPORT_SYMBOL.*($1)" \
				"$LINUX/$file" 2>/dev/null
			rc=$?
			if test $rc -eq 0; then
				export=1
				break;
			fi
		done
		if test $export -eq 0; then :
			$4
		else :
			$3
		fi
	else :
		$3
	fi
])

dnl #
dnl # ZFS_LINUX_TRY_COMPILE_SYMBOL
dnl # like ZFS_LINUX_TRY_COMPILE, except ZFS_CHECK_SYMBOL_EXPORT
dnl # is called if not compiling for builtin
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE_SYMBOL], [
	ZFS_LINUX_TRY_COMPILE([$1], [$2], [rc=0], [rc=1])
	if test $rc -ne 0; then :
		$6
	else
		if test "x$enable_linux_builtin" != xyes; then
			ZFS_CHECK_SYMBOL_EXPORT([$3], [$4], [rc=0], [rc=1])
		fi
		if test $rc -ne 0; then :
			$6
		else :
			$5
		fi
	fi
])

dnl #
dnl # ZFS_LINUX_TRY_COMPILE_HEADER
dnl # like ZFS_LINUX_TRY_COMPILE, except the contents conftest.h are
dnl # provided via the fifth parameter
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE_HEADER],
	[ZFS_LINUX_COMPILE_IFELSE(
	[AC_LANG_SOURCE([ZFS_LANG_PROGRAM([[$1]], [[$2]])])],
	[modules],
	[test -s build/conftest.o],
	[$3], [$4], [$5])
])
