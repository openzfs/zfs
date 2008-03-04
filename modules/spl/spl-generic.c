#include <sys/sysmacros.h>
#include "config.h"

/*
 * Generic support
 */

int p0 = 0;
EXPORT_SYMBOL(p0);

static int __init spl_init(void)
{
        printk(KERN_INFO "spl: Loaded Solaris Porting Layer v%s\n", VERSION);
	return 0;
}

static void spl_fini(void)
{
	return;
}

module_init(spl_init);
module_exit(spl_fini);

MODULE_AUTHOR("Lawrence Livermore National Labs");
MODULE_DESCRIPTION("Solaris Porting Layer");
MODULE_LICENSE("GPL");
