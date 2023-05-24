AC_DEFUN([ZFS_AC_KERNEL_SRC_RECLAIMED], [
	dnl #
	dnl # 6.4 API change
	dnl # The reclaimed_slab of struct reclaim_state
	dnl # is renamed to reclaimed
	dnl #
	ZFS_LINUX_TEST_SRC([reclaim_state_reclaimed], [
		#include <linux/swap.h>
		static const struct reclaim_state
		    rs  __attribute__ ((unused)) = {
		    .reclaimed = 100,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_RECLAIMED], [
	AC_MSG_CHECKING([whether struct reclaim_state has reclaimed field])
	ZFS_LINUX_TEST_RESULT([reclaim_state_reclaimed], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_RECLAIM_STATE_RECLAIMED, 1,
		   [struct reclaim_state has reclaimed])
	],[
		AC_MSG_RESULT(no)
	])
])

