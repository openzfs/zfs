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
			.s_shrink.seeks = DEFAULT_SEEKS,
			.s_shrink.batch = 0,
		};
	],[])
])

dnl #
dnl # 6.7 API change
dnl # s_shrink is now a pointer.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK_PTR], [
	ZFS_LINUX_TEST_SRC([super_block_s_shrink_ptr], [
		#include <linux/fs.h>
		unsigned long shrinker_cb(struct shrinker *shrink,
		    struct shrink_control *sc) { return 0; }
		static struct shrinker shrinker = {
			.count_objects = shrinker_cb,
			.scan_objects = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		static const struct super_block
		    sb __attribute__ ((unused)) = {
			.s_shrink = &shrinker,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SUPER_BLOCK_S_SHRINK], [
	AC_MSG_CHECKING([whether super_block has s_shrink])
	ZFS_LINUX_TEST_RESULT([super_block_s_shrink], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SUPER_BLOCK_S_SHRINK, 1,
			[have super_block s_shrink])
	],[
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether super_block has s_shrink pointer])
		ZFS_LINUX_TEST_RESULT([super_block_s_shrink_ptr], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_SUPER_BLOCK_S_SHRINK_PTR, 1,
				[have super_block s_shrink pointer])
		],[
			AC_MSG_RESULT(no)
			ZFS_LINUX_TEST_ERROR([sb->s_shrink()])
		])
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

AC_DEFUN([ZFS_AC_KERNEL_SRC_REGISTER_SHRINKER_VARARG], [
	ZFS_LINUX_TEST_SRC([register_shrinker_vararg], [
		#include <linux/mm.h>
		unsigned long shrinker_cb(struct shrinker *shrink,
		    struct shrink_control *sc) { return 0; }
	],[
		struct shrinker cache_shrinker = {
			.count_objects = shrinker_cb,
			.scan_objects = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		register_shrinker(&cache_shrinker, "vararg-reg-shrink-test");
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

dnl #
dnl # 6.7 API change
dnl # register_shrinker has been replaced by shrinker_register.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER_REGISTER], [
	ZFS_LINUX_TEST_SRC([shrinker_register], [
		#include <linux/shrinker.h>
		unsigned long shrinker_cb(struct shrinker *shrink,
		    struct shrink_control *sc) { return 0; }
	],[
		struct shrinker cache_shrinker = {
			.count_objects = shrinker_cb,
			.scan_objects = shrinker_cb,
			.seeks = DEFAULT_SEEKS,
		};
		shrinker_register(&cache_shrinker);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER_CALLBACK],[
	dnl #
	dnl # 6.0 API change
	dnl # register_shrinker() becomes a var-arg function that takes
	dnl # a printf-style format string as args > 0
	dnl #
	AC_MSG_CHECKING([whether new var-arg register_shrinker() exists])
	ZFS_LINUX_TEST_RESULT([register_shrinker_vararg], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REGISTER_SHRINKER_VARARG, 1,
		    [register_shrinker is vararg])

		dnl # We assume that the split shrinker callback exists if the
		dnl # vararg register_shrinker() exists, because the latter is
		dnl # a much more recent addition, and the macro test for the
		dnl # var-arg version only works if the callback is split
		AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK, 1,
			[cs->count_objects exists])
	],[
		AC_MSG_RESULT(no)
		dnl #
		dnl # 3.0 - 3.11 API change
		dnl # cs->shrink(struct shrinker *, struct shrink_control *sc)
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
			dnl # cs->shrink() is logically split in to
			dnl # cs->count_objects() and cs->scan_objects()
			dnl #
			AC_MSG_CHECKING(
			    [whether cs->count_objects callback exists])
			ZFS_LINUX_TEST_RESULT(
			    [shrinker_cb_shrink_control_split],[
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK, 1,
				    [cs->count_objects exists])
			],[
				AC_MSG_RESULT(no)

				AC_MSG_CHECKING(
				    [whether shrinker_register exists])
				ZFS_LINUX_TEST_RESULT([shrinker_register], [
					AC_MSG_RESULT(yes)
					AC_DEFINE(HAVE_SHRINKER_REGISTER, 1,
					    [shrinker_register exists])

					dnl # We assume that the split shrinker
					dnl # callback exists if
					dnl # shrinker_register() exists,
					dnl # because the latter is a much more
					dnl # recent addition, and the macro
					dnl # test for shrinker_register() only
					dnl # works if the callback is split
					AC_DEFINE(HAVE_SPLIT_SHRINKER_CALLBACK,
					    1, [cs->count_objects exists])
				],[
					AC_MSG_RESULT(no)
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
		ZFS_LINUX_TEST_ERROR([shrink_control])
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER], [
	ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK
	ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK_PTR
	ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SRC_SHRINKER_CALLBACK
	ZFS_AC_KERNEL_SRC_SHRINK_CONTROL_STRUCT
	ZFS_AC_KERNEL_SRC_REGISTER_SHRINKER_VARARG
	ZFS_AC_KERNEL_SRC_SHRINKER_REGISTER
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER], [
	ZFS_AC_KERNEL_SUPER_BLOCK_S_SHRINK
	ZFS_AC_KERNEL_SHRINK_CONTROL_HAS_NID
	ZFS_AC_KERNEL_SHRINKER_CALLBACK
	ZFS_AC_KERNEL_SHRINK_CONTROL_STRUCT
])
