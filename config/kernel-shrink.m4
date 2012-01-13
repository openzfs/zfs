dnl #
dnl # 3.1 API change
dnl # The super_block structure now stores a per-filesystem shrinker.
dnl # This interface is preferable because it can be used to specifically
dnl # target only the zfs filesystem for pruning.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SHRINK], [
	AC_MSG_CHECKING([whether super_block has s_shrink])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*shrink)(struct shrinker *, struct shrink_control *sc)
			__attribute__ ((unused)) = NULL;
		struct super_block sb __attribute__ ((unused)) = {
			.s_shrink.shrink = shrink,
			.s_shrink.seeks = DEFAULT_SEEKS,
			.s_shrink.batch = 0,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINK, 1, [struct super_block has s_shrink])

	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_NR_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->nr_cached_objects() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		int (*nr_cached_objects)(struct super_block *)
			__attribute__ ((unused)) = NULL;
		struct super_operations sops __attribute__ ((unused)) = {
			.nr_cached_objects = nr_cached_objects,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NR_CACHED_OBJECTS, 1,
			[sops->nr_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FREE_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->free_cached_objects() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		void (*free_cached_objects)(struct super_block *, int)
			__attribute__ ((unused)) = NULL;
		struct super_operations sops __attribute__ ((unused)) = {
			.free_cached_objects = free_cached_objects,
		};
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FREE_CACHED_OBJECTS, 1,
			[sops->free_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])
