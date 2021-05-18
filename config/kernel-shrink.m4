dnl #
dnl # 3.12 API change
dnl # The nid member was added to struct shrink_control to support
dnl # NUMA-aware shrinkers.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_HAS_NID], [
	ZFS_LINUX_TEST_SRC([shrink_control_nid], [
		#include <linux/fs.h>
	],[
		struct shrink_control sc __attribute__ ((unused));
		unsigned long scnidsize __attribute__ ((unused)) =
		    sizeof(sc.nid);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID], [
	AC_MSG_CHECKING([whether shrink_control has nid])
	ZFS_LINUX_TEST_RESULT([shrink_control_nid], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(SHRINK_CONTROL_HAS_NID, 1,
		    [struct shrink_control has nid])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER_CALLBACK], [
	ZFS_LINUX_TEST_SRC([shrinker_cb_shrink_control], [
		#include <linux/mm.h>
		int shrinker_cb(struct shrinker *shrink,
		    struct shrink_control *sc) { return 0; }
	],[
		struct shrinker cache_shrinker = {
			.shrink = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	])

	ZFS_LINUX_TEST_SRC([shrinker_cb_shrink_control_split], [
		#include <linux/mm.h>
		unsigned long shrinker_cb(struct shrinker *shrink,
		    struct shrink_control *sc) { return 0; }
	],[
		struct shrinker cache_shrinker = {
			.count_objects = shrinker_cb,
			.scan_objects = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER_CALLBACK],[
	dnl #
	dnl # 3.0 - 3.11 API change
	dnl # ->shrink(struct shrinker *, struct shrink_control *sc)
	dnl #
	AC_MSG_CHECKING([whether new 2-argument shrinker exists])
	ZFS_LINUX_TEST_RESULT([shrinker_cb_shrink_control], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SINGLE_SHRINKER_CALLBACK, 1,
		    [new shrinker callback wants 2 args])
	],[
		AC_MSG_RESULT(no)

		dnl #
		dnl # 3.12 API change,
		dnl # ->shrink() is logically split in to
		dnl # ->count_objects() and ->scan_objects()
		dnl #
		AC_MSG_CHECKING([whether ->count_objects callback exists])
		ZFS_LINUX_TEST_RESULT([shrinker_cb_shrink_control_split], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK, 1,
			    [->count_objects exists])
		],[
			ZFS_LINUX_TEST_ERROR([shrinker])
		])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER], [
	ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SRC_SHRINKER_CALLBACK
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER], [
	ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SHRINKER_CALLBACK
])
