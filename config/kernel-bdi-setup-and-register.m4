dnl #
dnl # 2.6.32 - 2.6.33, bdi_setup_and_register() is not exported.
dnl # 2.6.34 - 3.19, bdi_setup_and_register() takes 3 arguments.
dnl # 4.0 - x.y, bdi_setup_and_register() takes 2 arguments.
dnl #
AC_DEFUN([ZFS_AC_KERNEL_BDI_SETUP_AND_REGISTER], [
	AC_MSG_CHECKING([whether bdi_setup_and_register() wants 2 args])
	ZFS_LINUX_TRY_COMPILE_SYMBOL([
		#include <linux/backing-dev.h>
		struct backing_dev_info bdi;
	], [
		char *name = "bdi";
		int error __attribute__((unused)) =
		    bdi_setup_and_register(&bdi, name);
	], [bdi_setup_and_register], [mm/backing-dev.c], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_2ARGS_BDI_SETUP_AND_REGISTER, 1,
		    [bdi_setup_and_register() wants 2 args])
	], [
		AC_MSG_RESULT(no)
		AC_MSG_CHECKING([whether bdi_setup_and_register() wants 3 args])
		ZFS_LINUX_TRY_COMPILE_SYMBOL([
			#include <linux/backing-dev.h>
			struct backing_dev_info bdi;
		], [
			char *name = "bdi";
			unsigned int cap = BDI_CAP_MAP_COPY;
			int error __attribute__((unused)) =
			    bdi_setup_and_register(&bdi, name, cap);
		], [bdi_setup_and_register], [mm/backing-dev.c], [
			AC_MSG_RESULT(yes)
			AC_DEFINE(HAVE_3ARGS_BDI_SETUP_AND_REGISTER, 1,
			    [bdi_setup_and_register() wants 3 args])
		], [
			AC_MSG_RESULT(no)
		])
	])
])
