# dnl
# dnl 5.8 (735e4ae5ba28) introduced a superblock scoped errseq_t to use to
# dnl record writeback errors for syncfs() to return. Up until 5.17, when
# dnl sync_fs errors were returned directly, this is the only way for us to
# dnl report an error from syncfs().
# dnl
AC_DEFUN([ZFS_AC_KERNEL_SRC_SUPER_BLOCK_S_WB_ERR], [
	ZFS_LINUX_TEST_SRC([super_block_s_wb_err], [
		#include <linux/fs.h>

		static const struct super_block
		    sb __attribute__ ((unused)) = {
			.s_wb_err = 0,
		};
	],[])
])

AC_DEFUN([ZFS_AC_KERNEL_SUPER_BLOCK_S_WB_ERR], [
	AC_MSG_CHECKING([whether super_block has s_wb_err])
	ZFS_LINUX_TEST_RESULT([super_block_s_wb_err], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SUPER_BLOCK_S_WB_ERR, 1,
			[have super_block s_wb_err])
	],[
		AC_MSG_RESULT(no)
	])
])
