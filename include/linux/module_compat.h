#ifndef _SPL_MODULE_COMPAT_H
#define _SPL_MODULE_COMPAT_H

#include <linux/module.h>

#define spl_module_init(init_fn)                                        \
static int                                                              \
spl_##init_fn(void)                                                     \
{                                                                       \
	int rc;                                                         \
	                                                                \
	spl_setup();                                                    \
	rc = init_fn();                                                 \
                                                                        \
	return rc;                                                      \
}                                                                       \
                                                                        \
module_init(spl_##init_fn)

#define spl_module_exit(exit_fn)                                        \
static void                                                             \
spl_##exit_fn(void)                                                     \
{                                                                       \
	int rc;                                                         \
                                                                        \
	rc = exit_fn();                                                 \
	spl_cleanup();                                                  \
	if (rc)                                                         \
		printk(KERN_ERR "SPL: Failure %d unloading "            \
		       "dependent module\n", rc);                       \
}                                                                       \
                                                                        \
module_exit(spl_##exit_fn)

#endif /* _SPL_MODULE_COMPAT_H */
