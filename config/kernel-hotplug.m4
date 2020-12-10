dnl #
dnl # 4.6 API change
dnl # Added CPU hotplug APIs
dnl #
AC_DEFUN([ZFS_AC_KERNEL_SRC_CPU_HOTPLUG], [
	ZFS_LINUX_TEST_SRC([cpu_hotplug], [
		#include <linux/cpuhotplug.h>
	],[
		enum cpuhp_state state = CPUHP_ONLINE;
		int (*fp)(unsigned int, struct hlist_node *) = NULL;
		cpuhp_state_add_instance_nocalls(0, (struct hlist_node *)NULL);
		cpuhp_state_remove_instance_nocalls(0, (struct hlist_node *)NULL);
		cpuhp_setup_state_multi(state, "", fp, fp);
		cpuhp_remove_multi_state(0);
	])
])

AC_DEFUN([ZFS_AC_KERNEL_CPU_HOTPLUG], [
	AC_MSG_CHECKING([whether CPU hotplug APIs exist])
	ZFS_LINUX_TEST_RESULT([cpu_hotplug], [
		AC_MSG_RESULT(yes)
		AC_DEFINE(HAVE_CPU_HOTPLUG, 1, [yes])
	],[
		AC_MSG_RESULT(no)
	])
])
