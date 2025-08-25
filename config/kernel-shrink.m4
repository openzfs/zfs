dnl #
dnl # 3.1 API change
dnl # The super_block structure now stores a per-filesystem shrinker.
dnl # This interface is preferable because it can be used to specifically
dnl # target only the zfs filesystem for pruning.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK], [
	ZFS_LINUX_TEST_SRC([super_block_s_shrink], [
		#include <linux/fs.h>

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
		static unsigned long shrinker_cb(struct shrinker *shrink,
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
dnl # 6.0 API change
dnl # register_shrinker() becomes a var-arg function that takes
dnl # a printf-style format string as args > 0
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_REGISTER_SHRINKER_VARARG], [
	ZFS_LINUX_TEST_SRC([register_shrinker_vararg], [
		#include <linux/mm.h>
		static unsigned long shrinker_cb(struct shrinker *shrink,
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

AC_DEFUN([ZFS_AC_KERNEL_REGISTER_SHRINKER_VARARG],[
	AC_MSG_CHECKING([whether new var-arg register_shrinker() exists])
	ZFS_LINUX_TEST_RESULT([register_shrinker_vararg], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_REGISTER_SHRINKER_VARARG, 1,
		    [register_shrinker is vararg])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 6.7 API change
dnl # register_shrinker has been replaced by shrinker_register.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER_REGISTER], [
	ZFS_LINUX_TEST_SRC([shrinker_register], [
		#include <linux/shrinker.h>
		static unsigned long shrinker_cb(struct shrinker *shrink,
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

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER_REGISTER], [
	AC_MSG_CHECKING([whether shrinker_register() exists])
	ZFS_LINUX_TEST_RESULT([shrinker_register], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SHRINKER_REGISTER, 1, [shrinker_register exists])
	], [
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_SRC_SHRINKER], [
	ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK
	ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_SHRINK_PTR
	ZFS_AC_KERNEL_SRC_REGISTER_SHRINKER_VARARG
	ZFS_AC_KERNEL_SRC_SHRINKER_REGISTER
])

AC_DEFUN([ZFS_AC_KERNEL_SHRINKER], [
	ZFS_AC_KERNEL_SUPER_BLOCK_S_SHRINK
	ZFS_AC_KERNEL_REGISTER_SHRINKER_VARARG
	ZFS_AC_KERNEL_SHRINKER_REGISTER
])
