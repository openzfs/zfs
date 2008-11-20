#if defined(_KERNEL) && defined(HAVE_SPL)

#include <linux/module.h>
#include <sys/zfs_context.h>

static int __init zport_init(void)
{
	return 0;
}

static void zport_fini(void)
{
	return;
}

module_init(zport_init);
module_exit(zport_fini);

EXPORT_SYMBOL(u8_validate);
EXPORT_SYMBOL(u8_strcmp);
EXPORT_SYMBOL(u8_textprep_str);

MODULE_AUTHOR("Sun Microsystems, Inc");
MODULE_DESCRIPTION("zport implementation");
MODULE_LICENSE("CDDL");
#endif
