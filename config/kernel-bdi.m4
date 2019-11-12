dnl #
dnl # Check available BDI interfaces.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_BDI], [
	ZFS_LINUX_TEST_SRC([super_setup_bdi_name], [
		#include <linux/fs.h>
		struct super_block sb;
	], [
		char *name = "bdi";
		atomic_long_t zfs_bdi_seq;
		int error __attribute__((unused)) =
		    super_setup_bdi_name(&sb, "%.28s-%ld", name,
		    atomic_long_inc_return(&zfs_bdi_seq));
	])

	ZFS_LINUX_TEST_SRC([bdi_setup_and_register], [
		#include <linux/backing-dev.h>
		struct backing_dev_info bdi;
	], [
		char *name = "bdi";
		int error __attribute__((unused)) =
		    bdi_setup_and_register(&bdi, name);
	])

	ZFS_LINUX_TEST_SRC([bdi_setup_and_register_3args], [
		#include <linux/backing-dev.h>
		struct backing_dev_info bdi;
	], [
		char *name = "bdi";
		unsigned int cap = BDI_CAP_MAP_COPY;
		int error __attribute__((unused)) =
		    bdi_setup_and_register(&bdi, name, cap);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_BDI], [
	dnl #
	dnl # 4.12, super_setup_bdi_name() introduced.
	dnl #
	AC_MSG_CHECKING([whether super_setup_bdi_name() exists])
	ZFS_LINUX_TEST_RESULT_SYMBOL([super_setup_bdi_name],
	    [super_setup_bdi_name], [fs/super.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_SUPER_SETUP_BDI_NAME, 1,
                    [super_setup_bdi_name() exits])
	], [
		AC_MSG_RESULT(no)

		dnl #
		dnl # 4.0 - 4.11, bdi_setup_and_register() takes 2 arguments.
		dnl #
		AC_MSG_CHECKING(
		    [whether bdi_setup_and_register() wants 2 args])
		ZFS_LINUX_TEST_RESULT_SYMBOL([bdi_setup_and_register],
		    [bdi_setup_and_register], [mm/backing-dev.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_2ARGS_BDI_SETUP_AND_REGISTER, 1,
			    [bdi_setup_and_register() wants 2 args])
		], [
			AC_MSG_RESULT(no)

			dnl #
			dnl # 2.6.34 - 3.19, bdi_setup_and_register()
			dnl # takes 3 arguments.
			dnl #
			AC_MSG_CHECKING(
			    [whether bdi_setup_and_register() wants 3 args])
			ZFS_LINUX_TEST_RESULT_SYMBOL(
			    [bdi_setup_and_register_3args],
			    [bdi_setup_and_register], [mm/backing-dev.c], [
				AC_MSG_RESULT(yes)
				AC_DEFINE(HAVE_3ARGS_BDI_SETUP_AND_REGISTER, 1,
				    [bdi_setup_and_register() wants 3 args])
			], [
				ZFS_LINUX_TEST_ERROR([bdi_setup])
			])
		])
	])
])
