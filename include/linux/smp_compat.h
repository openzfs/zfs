#ifndef _SPL_SMP_COMPAT_H
#define _SPL_SMP_COMPAT_H

#include <linux/smp.h>

#ifdef HAVE_3ARGS_ON_EACH_CPU

#define spl_on_each_cpu(func,info,wait)	on_each_cpu(func,info,wait)

#else

#define spl_on_each_cpu(func,info,wait)	on_each_cpu(func,info,0,wait)

#endif /* HAVE_3ARGS_ON_EACH_CPU */

#endif /* _SPL_SMP_COMPAT_H */
