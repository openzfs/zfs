#include <sys/sysmacros.h>
#include "config.h"

/*
 * Generic support
 */

int p0 = 0;
EXPORT_SYMBOL(p0);

int
highbit(unsigned long i)
{
        register int h = 1;

        if (i == 0)
                return (0);
#if BITS_PER_LONG == 64
        if (i & 0xffffffff00000000ul) {
                h += 32; i >>= 32;
        }
#endif
        if (i & 0xffff0000) {
                h += 16; i >>= 16;
        }
        if (i & 0xff00) {
                h += 8; i >>= 8;
        }
        if (i & 0xf0) {
                h += 4; i >>= 4;
        }
        if (i & 0xc) {
                h += 2; i >>= 2;
        }
        if (i & 0x2) {
                h += 1;
        }
        return (h);
}
EXPORT_SYMBOL(highbit);

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
