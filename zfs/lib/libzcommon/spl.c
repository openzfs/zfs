#if defined(_KERNEL) && defined(HAVE_SPL)

#include <sys/zfs_context.h>

static int __init zfscommon_init(void)
{
	        return 0;
}

static void zfscommon_fini(void)
{
	        return;
}

module_init(zfscommon_init);
module_exit(zfscommon_fini);

MODULE_AUTHOR("Sun Microsystems, Inc");
MODULE_DESCRIPTION("Generic ZFS support");
MODULE_LICENSE("CDDL");

#endif
