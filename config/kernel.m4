dnl #
dnl # Default ZFS kernel configuration
dnl #
AC_DEFUN([ZFS_AC_CONFIG_KERNEL], [
	AM_COND_IF([BUILD_LINUX], [
		dnl # Setup the kernel build environment.
		ZFS_AC_KERNEL
		ZFS_AC_QAT

		dnl # Sanity checks for module building and CONFIG_* defines
		ZFS_AC_KERNEL_TEST_MODULE
		ZFS_AC_KERNEL_CONFIG_DEFINED

		dnl # Sequential ZFS_LINUX_TRY_COMPILE tests
		ZFS_AC_KERNEL_FPU_HEADER
		ZFS_AC_KERNEL_OBJTOOL_HEADER
		ZFS_AC_KERNEL_WAIT_QUEUE_ENTRY_T
		ZFS_AC_KERNEL_MISC_MINOR
		ZFS_AC_KERNEL_DECLARE_EVENT_CLASS

		dnl # Parallel ZFS_LINUX_TEST_SRC / ZFS_LINUX_TEST_RESULT tests
		ZFS_AC_KERNEL_TEST_SRC
		ZFS_AC_KERNEL_TEST_RESULT

		AS_IF([test "$LINUX_OBJ" != "$LINUX"], [
			KERNEL_MAKE="$KERNEL_MAKE O=$LINUX_OBJ"
		])

		AC_SUBST(KERNEL_MAKE)
	])
])

dnl #
dnl # Generate and compile all of the kernel API test cases to determine
dnl # which interfaces are available.  By invoking the kernel build system
dnl # only once the compilation can be done in parallel significantly
dnl # speeding up the process.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TEST_SRC], [
	ZFS_AC_KERNEL_SRC_OBJTOOL
	ZFS_AC_KERNEL_SRC_GLOBAL_PAGE_STATE
	ZFS_AC_KERNEL_SRC_ACCESS_OK_TYPE
	ZFS_AC_KERNEL_SRC_PDE_DATA
	ZFS_AC_KERNEL_SRC_FALLOCATE
	ZFS_AC_KERNEL_SRC_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE
	ZFS_AC_KERNEL_SRC_RWSEM
	ZFS_AC_KERNEL_SRC_SCHED
	ZFS_AC_KERNEL_SRC_USLEEP_RANGE
	ZFS_AC_KERNEL_SRC_KMEM_CACHE
	ZFS_AC_KERNEL_SRC_KVMALLOC
	ZFS_AC_KERNEL_SRC_VMALLOC_PAGE_KERNEL
	ZFS_AC_KERNEL_SRC_WAIT
	ZFS_AC_KERNEL_SRC_INODE_TIMES
	ZFS_AC_KERNEL_SRC_INODE_LOCK
	ZFS_AC_KERNEL_SRC_GROUP_INFO_GID
	ZFS_AC_KERNEL_SRC_RW
	ZFS_AC_KERNEL_SRC_TIMER_SETUP
	ZFS_AC_KERNEL_SRC_SUPER_USER_NS
	ZFS_AC_KERNEL_SRC_PROC_OPERATIONS
	ZFS_AC_KERNEL_SRC_BLOCK_DEVICE_OPERATIONS
	ZFS_AC_KERNEL_SRC_BIO
	ZFS_AC_KERNEL_SRC_BLKDEV
	ZFS_AC_KERNEL_SRC_BLK_QUEUE
	ZFS_AC_KERNEL_SRC_GET_DISK_AND_MODULE
	ZFS_AC_KERNEL_SRC_REVALIDATE_DISK_SIZE
	ZFS_AC_KERNEL_SRC_GET_DISK_RO
	ZFS_AC_KERNEL_SRC_GENERIC_READLINK_GLOBAL
	ZFS_AC_KERNEL_SRC_DISCARD_GRANULARITY
	ZFS_AC_KERNEL_SRC_INODE_OWNER_OR_CAPABLE
	ZFS_AC_KERNEL_SRC_XATTR
	ZFS_AC_KERNEL_SRC_ACL
	ZFS_AC_KERNEL_SRC_INODE_GETATTR
	ZFS_AC_KERNEL_SRC_INODE_SET_FLAGS
	ZFS_AC_KERNEL_SRC_INODE_SET_IVERSION
	ZFS_AC_KERNEL_SRC_SHOW_OPTIONS
	ZFS_AC_KERNEL_SRC_FILE_INODE
	ZFS_AC_KERNEL_SRC_FILE_DENTRY
	ZFS_AC_KERNEL_SRC_FSYNC
	ZFS_AC_KERNEL_SRC_AIO_FSYNC
	ZFS_AC_KERNEL_SRC_EVICT_INODE
	ZFS_AC_KERNEL_SRC_DIRTY_INODE
	ZFS_AC_KERNEL_SRC_SHRINKER
	ZFS_AC_KERNEL_SRC_MKDIR_UMODE_T
	ZFS_AC_KERNEL_SRC_LOOKUP_FLAGS
	ZFS_AC_KERNEL_SRC_CREATE_FLAGS
	ZFS_AC_KERNEL_SRC_GET_LINK
	ZFS_AC_KERNEL_SRC_PUT_LINK
	ZFS_AC_KERNEL_SRC_TMPFILE
	ZFS_AC_KERNEL_SRC_AUTOMOUNT
	ZFS_AC_KERNEL_SRC_ENCODE_FH_WITH_INODE
	ZFS_AC_KERNEL_SRC_COMMIT_METADATA
	ZFS_AC_KERNEL_SRC_CLEAR_INODE
	ZFS_AC_KERNEL_SRC_SETATTR_PREPARE
	ZFS_AC_KERNEL_SRC_INSERT_INODE_LOCKED
	ZFS_AC_KERNEL_SRC_DENTRY
	ZFS_AC_KERNEL_SRC_TRUNCATE_SETSIZE
	ZFS_AC_KERNEL_SRC_SECURITY_INODE
	ZFS_AC_KERNEL_SRC_FST_MOUNT
	ZFS_AC_KERNEL_SRC_BDI
	ZFS_AC_KERNEL_SRC_SET_NLINK
	ZFS_AC_KERNEL_SRC_SGET
	ZFS_AC_KERNEL_SRC_LSEEK_EXECUTE
	ZFS_AC_KERNEL_SRC_VFS_GETATTR
	ZFS_AC_KERNEL_SRC_VFS_FSYNC_2ARGS
	ZFS_AC_KERNEL_SRC_VFS_ITERATE
	ZFS_AC_KERNEL_SRC_VFS_DIRECT_IO
	ZFS_AC_KERNEL_SRC_VFS_RW_ITERATE
	ZFS_AC_KERNEL_SRC_VFS_GENERIC_WRITE_CHECKS
	ZFS_AC_KERNEL_SRC_KMAP_ATOMIC_ARGS
	ZFS_AC_KERNEL_SRC_FOLLOW_DOWN_ONE
	ZFS_AC_KERNEL_SRC_MAKE_REQUEST_FN
	ZFS_AC_KERNEL_SRC_GENERIC_IO_ACCT
	ZFS_AC_KERNEL_SRC_FPU
	ZFS_AC_KERNEL_SRC_FMODE_T
	ZFS_AC_KERNEL_SRC_KUIDGID_T
	ZFS_AC_KERNEL_SRC_KUID_HELPERS
	ZFS_AC_KERNEL_SRC_MODULE_PARAM_CALL_CONST
	ZFS_AC_KERNEL_SRC_RENAME_WANTS_FLAGS
	ZFS_AC_KERNEL_SRC_CURRENT_TIME
	ZFS_AC_KERNEL_SRC_USERNS_CAPABILITIES
	ZFS_AC_KERNEL_SRC_IN_COMPAT_SYSCALL
	ZFS_AC_KERNEL_SRC_KTIME
	ZFS_AC_KERNEL_SRC_TOTALRAM_PAGES_FUNC
	ZFS_AC_KERNEL_SRC_TOTALHIGH_PAGES
	ZFS_AC_KERNEL_SRC_KSTRTOUL
	ZFS_AC_KERNEL_SRC_PERCPU

	AC_MSG_CHECKING([for available kernel interfaces])
	ZFS_LINUX_TEST_COMPILE_ALL([kabi])
	AC_MSG_RESULT([done])
])

dnl #
dnl # Check results of kernel interface tests.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TEST_RESULT], [
	ZFS_AC_KERNEL_ACCESS_OK_TYPE
	ZFS_AC_KERNEL_GLOBAL_PAGE_STATE
	ZFS_AC_KERNEL_OBJTOOL
	ZFS_AC_KERNEL_PDE_DATA
	ZFS_AC_KERNEL_FALLOCATE
	ZFS_AC_KERNEL_2ARGS_ZLIB_DEFLATE_WORKSPACESIZE
	ZFS_AC_KERNEL_RWSEM
	ZFS_AC_KERNEL_SCHED
	ZFS_AC_KERNEL_USLEEP_RANGE
	ZFS_AC_KERNEL_KMEM_CACHE
	ZFS_AC_KERNEL_KVMALLOC
	ZFS_AC_KERNEL_VMALLOC_PAGE_KERNEL
	ZFS_AC_KERNEL_WAIT
	ZFS_AC_KERNEL_INODE_TIMES
	ZFS_AC_KERNEL_INODE_LOCK
	ZFS_AC_KERNEL_GROUP_INFO_GID
	ZFS_AC_KERNEL_RW
	ZFS_AC_KERNEL_TIMER_SETUP
	ZFS_AC_KERNEL_SUPER_USER_NS
	ZFS_AC_KERNEL_PROC_OPERATIONS
	ZFS_AC_KERNEL_BLOCK_DEVICE_OPERATIONS
	ZFS_AC_KERNEL_BIO
	ZFS_AC_KERNEL_BLKDEV
	ZFS_AC_KERNEL_BLK_QUEUE
	ZFS_AC_KERNEL_GET_DISK_AND_MODULE
	ZFS_AC_KERNEL_REVALIDATE_DISK_SIZE
	ZFS_AC_KERNEL_GET_DISK_RO
	ZFS_AC_KERNEL_GENERIC_READLINK_GLOBAL
	ZFS_AC_KERNEL_DISCARD_GRANULARITY
	ZFS_AC_KERNEL_INODE_OWNER_OR_CAPABLE
	ZFS_AC_KERNEL_XATTR
	ZFS_AC_KERNEL_ACL
	ZFS_AC_KERNEL_INODE_GETATTR
	ZFS_AC_KERNEL_INODE_SET_FLAGS
	ZFS_AC_KERNEL_INODE_SET_IVERSION
	ZFS_AC_KERNEL_SHOW_OPTIONS
	ZFS_AC_KERNEL_FILE_INODE
	ZFS_AC_KERNEL_FILE_DENTRY
	ZFS_AC_KERNEL_FSYNC
	ZFS_AC_KERNEL_AIO_FSYNC
	ZFS_AC_KERNEL_EVICT_INODE
	ZFS_AC_KERNEL_DIRTY_INODE
	ZFS_AC_KERNEL_SHRINKER
	ZFS_AC_KERNEL_MKDIR_UMODE_T
	ZFS_AC_KERNEL_LOOKUP_FLAGS
	ZFS_AC_KERNEL_CREATE_FLAGS
	ZFS_AC_KERNEL_GET_LINK
	ZFS_AC_KERNEL_PUT_LINK
	ZFS_AC_KERNEL_TMPFILE
	ZFS_AC_KERNEL_AUTOMOUNT
	ZFS_AC_KERNEL_ENCODE_FH_WITH_INODE
	ZFS_AC_KERNEL_COMMIT_METADATA
	ZFS_AC_KERNEL_CLEAR_INODE
	ZFS_AC_KERNEL_SETATTR_PREPARE
	ZFS_AC_KERNEL_INSERT_INODE_LOCKED
	ZFS_AC_KERNEL_DENTRY
	ZFS_AC_KERNEL_TRUNCATE_SETSIZE
	ZFS_AC_KERNEL_SECURITY_INODE
	ZFS_AC_KERNEL_FST_MOUNT
	ZFS_AC_KERNEL_BDI
	ZFS_AC_KERNEL_SET_NLINK
	ZFS_AC_KERNEL_SGET
	ZFS_AC_KERNEL_LSEEK_EXECUTE
	ZFS_AC_KERNEL_VFS_GETATTR
	ZFS_AC_KERNEL_VFS_FSYNC_2ARGS
	ZFS_AC_KERNEL_VFS_ITERATE
	ZFS_AC_KERNEL_VFS_DIRECT_IO
	ZFS_AC_KERNEL_VFS_RW_ITERATE
	ZFS_AC_KERNEL_VFS_GENERIC_WRITE_CHECKS
	ZFS_AC_KERNEL_KMAP_ATOMIC_ARGS
	ZFS_AC_KERNEL_FOLLOW_DOWN_ONE
	ZFS_AC_KERNEL_MAKE_REQUEST_FN
	ZFS_AC_KERNEL_GENERIC_IO_ACCT
	ZFS_AC_KERNEL_FPU
	ZFS_AC_KERNEL_FMODE_T
	ZFS_AC_KERNEL_KUIDGID_T
	ZFS_AC_KERNEL_KUID_HELPERS
	ZFS_AC_KERNEL_MODULE_PARAM_CALL_CONST
	ZFS_AC_KERNEL_RENAME_WANTS_FLAGS
	ZFS_AC_KERNEL_CURRENT_TIME
	ZFS_AC_KERNEL_USERNS_CAPABILITIES
	ZFS_AC_KERNEL_IN_COMPAT_SYSCALL
	ZFS_AC_KERNEL_KTIME
	ZFS_AC_KERNEL_TOTALRAM_PAGES_FUNC
	ZFS_AC_KERNEL_TOTALHIGH_PAGES
	ZFS_AC_KERNEL_KSTRTOUL
	ZFS_AC_KERNEL_PERCPU
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
	*** is installed.  If you are building with a custom kernel, make sure
	*** the kernel is configured, built, and the '--with-linux=PATH'
	*** configure option refers to the location of the kernel source.
			])
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
			AC_MSG_ERROR([
	*** Cannot determine kernel version.
			])
		])
	], [
		AC_MSG_RESULT([Not found])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([
	*** Cannot find UTS_RELEASE definition.
			])
		else
			AC_MSG_ERROR([
	*** Cannot find UTS_RELEASE definition.
	*** Please run 'make prepare' inside the kernel source tree.])
		fi
	])

	AC_MSG_RESULT([$kernsrcver])

	AS_VERSION_COMPARE([$kernsrcver], [$ZFS_META_KVER_MIN], [
		 AC_MSG_ERROR([
	*** Cannot build against kernel version $kernsrcver.
	*** The minimum supported kernel version is $ZFS_META_KVER_MIN.
		])
	])

	LINUX=${kernelsrc}
	LINUX_OBJ=${kernelbuild}
	LINUX_VERSION=${kernsrcver}

	AC_SUBST(LINUX)
	AC_SUBST(LINUX_OBJ)
	AC_SUBST(LINUX_VERSION)

	ZFS_AC_MODULE_SYMVERS
])

dnl #
dnl # Detect the QAT module to be built against, QAT provides hardware
dnl # acceleration for data compression:
dnl #
dnl # https://01.org/intel-quickassist-technology
dnl #
dnl # 1) Download and install QAT driver from the above link
dnl # 2) Start QAT driver in your system:
dnl # 	 service qat_service start
dnl # 3) Enable QAT in ZFS, e.g.:
dnl # 	 ./configure --with-qat=<qat-driver-path>/QAT1.6
dnl # 	 make
dnl # 4) Set GZIP compression in ZFS dataset:
dnl # 	 zfs set compression = gzip <dataset>
dnl #
dnl # Then the data written to this ZFS pool is compressed by QAT accelerator
dnl # automatically, and de-compressed by QAT when read from the pool.
dnl #
dnl # 1) Get QAT hardware statistics with:
dnl #	 cat /proc/icp_dh895xcc_dev/qat
dnl # 2) To disable QAT:
dnl # 	 insmod zfs.ko zfs_qat_disable=1
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
	$QAT_SYMBOLS
			])
		])
	])
])

dnl #
dnl # Basic toolchain sanity check.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_TEST_MODULE], [
	AC_MSG_CHECKING([whether modules can be built])
	ZFS_LINUX_TRY_COMPILE([], [], [
		AC_MSG_RESULT([yes])
	],[
		AC_MSG_RESULT([no])
		if test "x$enable_linux_builtin" != xyes; then
			AC_MSG_ERROR([
	*** Unable to build an empty module.
			])
		else
			AC_MSG_ERROR([
	*** Unable to build an empty module.
	*** Please run 'make scripts' inside the kernel source tree.])
		fi
	])
])

dnl #
dnl # ZFS_LINUX_CONFTEST_H
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_H], [
test -d build/$2 || mkdir -p build/$2
cat - <<_ACEOF >build/$2/$2.h
$1
_ACEOF
])

dnl #
dnl # ZFS_LINUX_CONFTEST_C
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_C], [
test -d build/$2 || mkdir -p build/$2
cat confdefs.h - <<_ACEOF >build/$2/$2.c
$1
_ACEOF
])

dnl #
dnl # ZFS_LINUX_CONFTEST_MAKEFILE
dnl #
dnl # $1 - test case name
dnl # $2 - add to top-level Makefile
dnl # $3 - additional build flags
dnl #
AC_DEFUN([ZFS_LINUX_CONFTEST_MAKEFILE], [
	test -d build || mkdir -p build
	test -d build/$1 || mkdir -p build/$1

	file=build/$1/Makefile

	dnl # Example command line to manually build source.
	cat - <<_ACEOF >$file
# Example command line to manually build source
# make modules -C $LINUX_OBJ $ARCH_UM M=$PWD/build/$1

ccflags-y := -Werror $FRAME_LARGER_THAN
_ACEOF

	dnl # Additional custom CFLAGS as requested.
	m4_ifval($3, [echo "ccflags-y += $3" >>$file], [])

	dnl # Test case source
	echo "obj-m := $1.o" >>$file

	AS_IF([test "x$2" = "xyes"], [echo "obj-m += $1/" >>build/Makefile], [])
])

dnl #
dnl # ZFS_LINUX_TEST_PROGRAM(C)([PROLOGUE], [BODY])
dnl #
m4_define([ZFS_LINUX_TEST_PROGRAM], [
$1
int
main (void)
{
$2
	;
	return 0;
}
])

dnl #
dnl # ZFS_LINUX_TEST_REMOVE
dnl #
dnl # Removes the specified test source and results.
dnl #
AC_DEFUN([ZFS_LINUX_TEST_REMOVE], [
	test -d build/$1 && rm -Rf build/$1
	test -f build/Makefile && sed '/$1/d' build/Makefile
])

dnl #
dnl # ZFS_LINUX_COMPILE
dnl #
dnl # $1 - build dir
dnl # $2 - test command
dnl # $3 - pass command
dnl # $4 - fail command
dnl # $5 - set KBUILD_MODPOST_NOFINAL='yes'
dnl # $6 - set KBUILD_MODPOST_WARN='yes'
dnl #
dnl # Used internally by ZFS_LINUX_TEST_{COMPILE,MODPOST}
dnl #
AC_DEFUN([ZFS_LINUX_COMPILE], [
	AC_TRY_COMMAND([
	    KBUILD_MODPOST_NOFINAL="$5" KBUILD_MODPOST_WARN="$6"
	    make modules -k -j$TEST_JOBS -C $LINUX_OBJ $ARCH_UM
	    M=$PWD/$1 >$1/build.log 2>&1])
	AS_IF([AC_TRY_COMMAND([$2])], [$3], [$4])
])

dnl #
dnl # ZFS_LINUX_TEST_COMPILE
dnl #
dnl # Perform a full compile excluding the final modpost phase.
dnl #
AC_DEFUN([ZFS_LINUX_TEST_COMPILE], [
	ZFS_LINUX_COMPILE([$2], [test -f $2/build.log], [
		mv $2/Makefile $2/Makefile.compile.$1
		mv $2/build.log $2/build.log.$1
	],[
	        AC_MSG_ERROR([
        *** Unable to compile test source to determine kernel interfaces.])
	], [yes], [])
])

dnl #
dnl # ZFS_LINUX_TEST_MODPOST
dnl #
dnl # Perform a full compile including the modpost phase.  This may
dnl # be an incremental build if the objects have already been built.
dnl #
AC_DEFUN([ZFS_LINUX_TEST_MODPOST], [
	ZFS_LINUX_COMPILE([$2], [test -f $2/build.log], [
		mv $2/Makefile $2/Makefile.modpost.$1
		cat $2/build.log >>build/build.log.$1
	],[
	        AC_MSG_ERROR([
        *** Unable to modpost test source to determine kernel interfaces.])
	], [], [yes])
])

dnl #
dnl # Perform the compilation of the test cases in two phases.
dnl #
dnl # Phase 1) attempt to build the object files for all of the tests
dnl #          defined by the ZFS_LINUX_TEST_SRC macro.  But do not
dnl #          perform the final modpost stage.
dnl #
dnl # Phase 2) disable all tests which failed the initial compilation,
dnl #          then invoke the final modpost step for the remaining tests.
dnl #
dnl # This allows us efficiently build the test cases in parallel while
dnl # remaining resilient to build failures which are expected when
dnl # detecting the available kernel interfaces.
dnl #
dnl # The maximum allowed parallelism can be controlled by setting the
dnl # TEST_JOBS environment variable.  Otherwise, it default to $(nproc).
dnl #
AC_DEFUN([ZFS_LINUX_TEST_COMPILE_ALL], [
	dnl # Phase 1 - Compilation only, final linking is skipped.
	ZFS_LINUX_TEST_COMPILE([$1], [build])

	dnl #
	dnl # Phase 2 - When building external modules disable test cases
	dnl # which failed to compile and invoke modpost to verify the
	dnl # final linking.
	dnl #
	dnl # Test names suffixed with '_license' call modpost independently
	dnl # to ensure that a single incompatibility does not result in the
	dnl # modpost phase exiting early.  This check is not performed on
	dnl # every symbol since the majority are compatible and doing so
	dnl # would significantly slow down this phase.
	dnl #
	dnl # When configuring for builtin (--enable-linux-builtin)
	dnl # fake the linking step artificially create the expected .ko
	dnl # files for tests which did compile.  This is required for
	dnl # kernels which do not have loadable module support or have
	dnl # not yet been built.
	dnl #
	AS_IF([test "x$enable_linux_builtin" = "xno"], [
		for dir in $(awk '/^obj-m/ { print [$]3 }' \
		    build/Makefile.compile.$1); do
			name=${dir%/}
			AS_IF([test -f build/$name/$name.o], [
				AS_IF([test "${name##*_}" = "license"], [
					ZFS_LINUX_TEST_MODPOST([$1],
					    [build/$name])
					echo "obj-n += $dir" >>build/Makefile
				], [
					echo "obj-m += $dir" >>build/Makefile
				])
			], [
				echo "obj-n += $dir" >>build/Makefile
			])
		done

		ZFS_LINUX_TEST_MODPOST([$1], [build])
	], [
		for dir in $(awk '/^obj-m/ { print [$]3 }' \
		    build/Makefile.compile.$1); do
			name=${dir%/}
			AS_IF([test -f build/$name/$name.o], [
				touch build/$name/$name.ko
			])
		done
	])
])

dnl #
dnl # ZFS_LINUX_TEST_SRC
dnl #
dnl # $1 - name
dnl # $2 - global
dnl # $3 - source
dnl # $4 - extra cflags
dnl # $5 - check license-compatibility
dnl #
dnl # N.B because all of the test cases are compiled in parallel they
dnl # must never depend on the results of previous tests.  Each test
dnl # needs to be entirely independent.
dnl #
AC_DEFUN([ZFS_LINUX_TEST_SRC], [
	ZFS_LINUX_CONFTEST_C([ZFS_LINUX_TEST_PROGRAM([[$2]], [[$3]])], [$1])
	ZFS_LINUX_CONFTEST_MAKEFILE([$1], [yes], [$4])

	AS_IF([ test -n "$5" ], [
		ZFS_LINUX_CONFTEST_C([ZFS_LINUX_TEST_PROGRAM([[
			#include <linux/module.h>
			MODULE_LICENSE("$5");
			$2]], [[$3]])], [$1_license])
		ZFS_LINUX_CONFTEST_MAKEFILE([$1_license], [yes], [$4])
	])
])

dnl #
dnl # ZFS_LINUX_TEST_RESULT
dnl #
dnl # $1 - name of a test source (ZFS_LINUX_TEST_SRC)
dnl # $2 - run on success (valid .ko generated)
dnl # $3 - run on failure (unable to compile)
dnl #
AC_DEFUN([ZFS_LINUX_TEST_RESULT], [
	AS_IF([test -d build/$1], [
		AS_IF([test -f build/$1/$1.ko], [$2], [$3])
	], [
		AC_MSG_ERROR([
	*** No matching source for the "$1" test, check that
	*** both the test source and result macros refer to the same name.
		])
	])
])

dnl #
dnl # ZFS_LINUX_TEST_ERROR
dnl #
dnl # Generic error message which can be used when none of the expected
dnl # kernel interfaces were detected.
dnl #
AC_DEFUN([ZFS_LINUX_TEST_ERROR], [
	AC_MSG_ERROR([
	*** None of the expected "$1" interfaces were detected.
	*** This may be because your kernel version is newer than what is
	*** supported, or you are using a patched custom kernel with
	*** incompatible modifications.
	***
	*** ZFS Version: $ZFS_META_ALIAS
	*** Compatible Kernels: $ZFS_META_KVER_MIN - $ZFS_META_KVER_MAX
	])
])

dnl #
dnl # ZFS_LINUX_TEST_RESULT_SYMBOL
dnl #
dnl # Like ZFS_LINUX_TEST_RESULT except ZFS_CHECK_SYMBOL_EXPORT is called to
dnl # verify symbol exports, unless --enable-linux-builtin was provided to
dnl # configure.
dnl #
AC_DEFUN([ZFS_LINUX_TEST_RESULT_SYMBOL], [
	AS_IF([ ! test -f build/$1/$1.ko], [
		$5
	], [
		AS_IF([test "x$enable_linux_builtin" != "xyes"], [
			ZFS_CHECK_SYMBOL_EXPORT([$2], [$3], [$4], [$5])
		], [
			$4
		])
	])
])

dnl #
dnl # ZFS_LINUX_COMPILE_IFELSE
dnl #
AC_DEFUN([ZFS_LINUX_COMPILE_IFELSE], [
	ZFS_LINUX_TEST_REMOVE([conftest])

	m4_ifvaln([$1], [ZFS_LINUX_CONFTEST_C([$1], [conftest])])
	m4_ifvaln([$5], [ZFS_LINUX_CONFTEST_H([$5], [conftest])],
	    [ZFS_LINUX_CONFTEST_H([], [conftest])])

	ZFS_LINUX_CONFTEST_MAKEFILE([conftest], [no],
	    [m4_ifvaln([$5], [-I$PWD/build/conftest], [])])
	ZFS_LINUX_COMPILE([build/conftest], [$2], [$3], [$4], [], [])
])

dnl #
dnl # ZFS_LINUX_TRY_COMPILE
dnl #
dnl # $1 - global
dnl # $2 - source
dnl # $3 - run on success (valid .ko generated)
dnl # $4 - run on failure (unable to compile)
dnl #
dnl # When configuring as builtin (--enable-linux-builtin) for kernels
dnl # without loadable module support (CONFIG_MODULES=n) only the object
dnl # file is created.  See ZFS_LINUX_TEST_COMPILE_ALL for details.
dnl #
AC_DEFUN([ZFS_LINUX_TRY_COMPILE], [
	AS_IF([test "x$enable_linux_builtin" = "xyes"], [
		ZFS_LINUX_COMPILE_IFELSE(
		    [ZFS_LINUX_TEST_PROGRAM([[$1]], [[$2]])],
		    [test -f build/conftest/conftest.o], [$3], [$4])
	], [
		ZFS_LINUX_COMPILE_IFELSE(
		    [ZFS_LINUX_TEST_PROGRAM([[$1]], [[$2]])],
		    [test -f build/conftest/conftest.ko], [$3], [$4])
	])
])

dnl #
dnl # ZFS_CHECK_SYMBOL_EXPORT
dnl #
dnl # Check if a symbol is exported on not by consulting the symbols
dnl # file, or optionally the source code.
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
dnl #
dnl # Like ZFS_LINUX_TRY_COMPILER except ZFS_CHECK_SYMBOL_EXPORT is called
dnl # to verify symbol exports, unless --enable-linux-builtin was provided
dnl # to configure.
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
AC_DEFUN([ZFS_LINUX_TRY_COMPILE_HEADER], [
	ZFS_LINUX_COMPILE_IFELSE(
	    [ZFS_LINUX_TEST_PROGRAM([[$1]], [[$2]])],
	    [test -f build/conftest/conftest.ko],
	    [$3], [$4], [$5])
])
