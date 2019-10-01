dnl #
dnl # 3.1 API change
dnl # The super_block structure now stores a per-filesystem shrinker.
dnl # This interface is preferable because it can be used to specifically
dnl # target only the zfs filesystem for pruning.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK], [
	ZFS_LINUX_TEST_SRC([super_block_s_shrink], [
		#include <linux/fs.h>

		int shrink(struct shrinker *s, struct shrink_control *sc)
		    { return 0; }

		static const struct super_block
		    sb __attribute__ ((unused)) = {
			.s_shrink.shrink = shrink,
			.s_shrink.seeks = DEFAULT_SEEKS,
			.s_shrink.batch = 0,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SUPER_BLOCK_S_SHRINK], [
	AC_MSG_CHECKING([whether super_block has s_shrink])
	ZFS_LINUX_TEST_RESULT([super_block_s_shrink], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINK, 1, [struct super_block has s_shrink])

	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 3.3 API change
dnl # The super_block structure was changed to use an hlist_node instead
dnl # of a list_head for the .s_instance linkage.
dnl #
dnl # This was done in part to resolve a race in the iterate_supers_type()
dnl # function which was introduced in Linux 3.0 kernel.  The iterator
dnl # was supposed to provide a safe way to call an arbitrary function on
dnl # all super blocks of a specific type.  Unfortunately, because a
dnl # list_head was used it was possible for iterate_supers_type() to
dnl # get stuck spinning a super block which was just deactivated.
dnl #
dnl # This can occur because when the list head is removed from the
dnl # fs_supers list it is reinitialized to point to itself.  If the
dnl # iterate_supers_type() function happened to be processing the
dnl # removed list_head it will get stuck spinning on that list_head.
dnl #
dnl # To resolve the issue for existing 3.0 - 3.2 kernels we detect when
dnl # a list_head is used.  Then to prevent the spinning from occurring
dnl # the .next pointer is set to the fs_supers list_head which ensures
dnl # the iterate_supers_type() function will always terminate.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_INSTANCES_LIST_HEAD], [
	ZFS_LINUX_TEST_SRC([super_block_s_instances_list_head], [
		#include <linux/fs.h>
	],[
		struct super_block sb __attribute__ ((unused));
		INIT_LIST_HEAD(&sb.s_instances);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SUPER_BLOCK_S_INSTANCES_LIST_HEAD], [
	AC_MSG_CHECKING([whether super_block has s_instances list_head])
	ZFS_LINUX_TEST_RESULT([super_block_s_instances_list_head], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_S_INSTANCES_LIST_HEAD, 1,
		    [struct super_block has s_instances list_head])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_NR_CACHED_OBJECTS], [
	ZFS_LINUX_TEST_SRC([nr_cached_objects], [
		#include <linux/fs.h>

		int nr_cached_objects(struct super_block *sb) { return 0; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.nr_cached_objects = nr_cached_objects,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_NR_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->nr_cached_objects() exists])
	ZFS_LINUX_TEST_RESULT([nr_cached_objects], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_NR_CACHED_OBJECTS, 1,
		    [sops->nr_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_FREE_CACHED_OBJECTS], [
	ZFS_LINUX_TEST_SRC([free_cached_objects], [
		#include <linux/fs.h>

		void free_cached_objects(struct super_block *sb, int x)
		    { return; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.free_cached_objects = free_cached_objects,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_FREE_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->free_cached_objects() exists])
	ZFS_LINUX_TEST_RESULT([free_cached_objects], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FREE_CACHED_OBJECTS, 1,
		    [sops->free_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])

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
	ZFS_LINUX_TEST_SRC([shrinker_cb_2arg], [
		#include <linux/mm.h>
		int shrinker_cb(int nr_to_scan, gfp_t gfp_mask) { return 0; }
	],[
		struct shrinker cache_shrinker = {
			.shrink = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	])

	ZFS_LINUX_TEST_SRC([shrinker_cb_3arg], [
		#include <linux/mm.h>
		int shrinker_cb(struct shrinker *shrink, int nr_to_scan,
		    gfp_t gfp_mask) { return 0; }
	],[
		struct shrinker cache_shrinker = {
			.shrink = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker);
	])

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
	dnl # 2.6.23 to 2.6.34 API change
	dnl # ->shrink(int nr_to_scan, gfp_t gfp_mask)
	dnl #
	AC_MSG_CHECKING([whether old 2-argument shrinker exists])
	ZFS_LINUX_TEST_RESULT([shrinker_cb_2arg], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_OLD_SHRINKER_CALLBACK, 1,
		    [old shrinker callback wants 2 args])
	],[
		AC_MSG_RESULT(no)

		dnl #
		dnl # 2.6.35 - 2.6.39 API change
		dnl # ->shrink(struct shrinker *,
		dnl #          int nr_to_scan, gfp_t gfp_mask)
		dnl #
		AC_MSG_CHECKING([whether old 3-argument shrinker exists])
		ZFS_LINUX_TEST_RESULT([shrinker_cb_3arg], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_3ARGS_SHRINKER_CALLBACK, 1,
				[old shrinker callback wants 3 args])
		],[
			AC_MSG_RESULT(no)

			dnl #
			dnl # 3.0 - 3.11 API change
			dnl # ->shrink(struct shrinker *,
			dnl #          struct shrink_control *sc)
			dnl #
			AC_MSG_CHECKING(
			    [whether new 2-argument shrinker exists])
			ZFS_LINUX_TEST_RESULT([shrinker_cb_shrink_control], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_2ARGS_NEW_SHRINKER_CALLBACK, 1,
					[new shrinker callback wants 2 args])
			],[
				AC_MSG_RESULT(no)

				dnl #
				dnl # 3.12 API change,
				dnl # ->shrink() is logically split in to
				dnl # ->count_objects() and ->scan_objects()
				dnl #
				AC_MSG_CHECKING(
				    [whether ->count_objects callback exists])
				ZFS_LINUX_TEST_RESULT(
				    [shrinker_cb_shrink_control_split], [
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK,
						1, [->count_objects exists])
				],[
					ZFS_LINUX_TEST_ERROR([shrinker])
				])
			])
		])
	])
])

dnl #
dnl # 2.6.39 API change,
dnl # Shrinker adjust to use common shrink_control structure.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_STRUCT], [
	ZFS_LINUX_TEST_SRC([shrink_control_struct], [
		#include <linux/mm.h>
	],[
		struct shrink_control sc __attribute__ ((unused));

		sc.nr_to_scan = 0;
		sc.gfp_mask = GFP_KERNEL;
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINK_CONTROL_STRUCT], [
	AC_MSG_CHECKING([whether struct shrink_control exists])
	ZFS_LINUX_TEST_RESULT([shrink_control_struct], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINK_CONTROL_STRUCT, 1,
		    [struct shrink_control exists])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER], [
	ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK
	ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_INSTANCES_LIST_HEAD
	ZFS_AC_KERNEL_SRC_NR_CACHED_OBJECTS
	ZFS_AC_KERNEL_SRC_FREE_CACHED_OBJECTS
	ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SRC_SHRINKER_CALLBACK
	ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_STRUCT
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER], [
	ZFS_AC_KERNEL_SUPER_BLOCK_S_SHRINK
	ZFS_AC_KERNEL_SUPER_BLOCK_S_INSTANCES_LIST_HEAD
	ZFS_AC_KERNEL_NR_CACHED_OBJECTS
	ZFS_AC_KERNEL_FREE_CACHED_OBJECTS
	ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SHRINKER_CALLBACK
	ZFS_AC_KERNEL_SHRINK_CONTROL_STRUCT
])
