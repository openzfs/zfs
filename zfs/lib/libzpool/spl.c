#if defined(_KERNEL) && defined(HAVE_SPL)

#include <linux/module.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>

void
kernel_init(int mode)
{
	dprintf("physmem = %llu pages\n", physmem);
	spa_init(mode);
}

static int __init zpool_init(void)
{
	kernel_init(FREAD | FWRITE);
	return 0;
}

void
kernel_fini(void)
{
	spa_fini();
}

static void zpool_fini(void)
{
	kernel_fini();
}

module_init(zpool_init);
module_exit(zpool_fini);

MODULE_AUTHOR("Sun Microsystems, Inc");
MODULE_DESCRIPTION("zpool implementation");
MODULE_LICENSE("CDDL");
#endif
