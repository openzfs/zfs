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

		int shrink(struct shrinker *s, struct shrink_control *sc)
		    { return 0; }

		static const struct super_block
		    sb __attribute__ ((unused)) = {
			.s_shrink.shrink = shrink,
			.s_shrink.seeks = DEFAULT_SEEKS,
			.s_shrink.batch = 0,
		};
	],[
	],[
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
AC_DEFUN([ZFS_AC_KERNEL_S_INSTANCES_LIST_HEAD], [
	AC_MSG_CHECKING([whether super_block has s_instances list_head])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>
	],[
		struct super_block sb __attribute__ ((unused));

		INIT_LIST_HEAD(&sb.s_instances);
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_S_INSTANCES_LIST_HEAD, 1,
		    [struct super_block has s_instances list_head])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_NR_CACHED_OBJECTS], [
	AC_MSG_CHECKING([whether sops->nr_cached_objects() exists])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		static const struct super_operations so;
		static const unsigned u = sizeof (so.nr_cached_objects);
	],[
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

		static const struct super_operations so;
		static const unsigned u = sizeof (so.free_cached_objects);
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_FREE_CACHED_OBJECTS, 1,
			[sops->free_cached_objects() exists])
	],[
		AC_MSG_RESULT(no)
	])
])


dnl #
dnl # 3.12 API change
dnl #
dnl # A node ID argument was added to the nr_cached_objects and
dnl # free_cached_objects callbacks in struct super_operations.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_NR_CACHED_OBJECTS_HAS_NID], [
	AC_MSG_CHECKING([whether sops->nr_cached_objects() has nid argument])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		long nr_cached_objects(struct super_block *sb, int nid) { return 0; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.nr_cached_objects = nr_cached_objects,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(NR_CACHED_OBJECTS_HAS_NID, 1,
			[sops->nr_cached_objects() has nid argument])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FREE_CACHED_OBJECTS_HAS_NID], [
	AC_MSG_CHECKING([whether sops->free_cached_objects() has nid argument])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		long free_cached_objects(struct super_block *sb, long nr_to_scan,
		    int nid) { return 0; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.free_cached_objects = free_cached_objects,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(FREE_CACHED_OBJECTS_HAS_NID, 1,
			[sops->free_cached_objects() has nid argument])
	],[
		AC_MSG_RESULT(no)
	])
])

dnl #
dnl # 4.0 API change
dnl #
dnl # The node ID and nr_to_scan arguments to the nr_cached_objects and
dnl # free_cached_objects callbacks in struct super_operations were replaced
dnl # with a single struct shrink_control argument.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_NR_CACHED_OBJECTS_HAS_SC], [
	AC_MSG_CHECKING([whether sops->nr_cached_objects() has shrink_control argument])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		long nr_cached_objects(struct super_block *sb,
		    struct shrink_control sc) { return 0; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.nr_cached_objects = nr_cached_objects,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(NR_CACHED_OBJECTS_HAS_SC, 1,
			[sops->nr_cached_objects() has shrink_control argument])
	],[
		AC_MSG_RESULT(no)
	])
])

AC_DEFUN([ZFS_AC_KERNEL_FREE_CACHED_OBJECTS_HAS_SC], [
	AC_MSG_CHECKING([whether sops->free_cached_objects() has shrink_control argument])
	ZFS_LINUX_TRY_COMPILE([
		#include <linux/fs.h>

		long free_cached_objects(struct super_block *sb,
		    struct shrink_control sc) { return 0; }

		static const struct super_operations
		    sops __attribute__ ((unused)) = {
			.free_cached_objects = free_cached_objects,
		};
	],[
	],[
		AC_MSG_RESULT(yes)
		AC_DEFINE(NR_CACHED_OBJECTS_HAS_SC, 1,
			[sops->free_cached_objects() has shrink_control argument])
	],[
		AC_MSG_RESULT(no)
	])
])
