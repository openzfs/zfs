#ifndef _SPL_WORKQUEUE_COMPAT_H
#define _SPL_WORKQUEUE_COMPAT_H

#include <linux/workqueue.h>
#include <sys/types.h>

#ifdef HAVE_3ARGS_INIT_WORK

#define delayed_work			work_struct

#define spl_init_work(wq, cb, d)	INIT_WORK((wq), (void *)(cb), \
						  (void *)(d))
#define spl_init_delayed_work(wq,cb,d)	INIT_WORK((wq), (void *)(cb), \
						  (void *)(d))
#define spl_get_work_data(d, t, f)	(t *)(d)

#else

#define spl_init_work(wq, cb, d)	INIT_WORK((wq), (void *)(cb));
#define spl_init_delayed_work(wq,cb,d)	INIT_DELAYED_WORK((wq), (void *)(cb));
#define spl_get_work_data(d, t, f)	(t *)container_of(d, t, f)

#endif /* HAVE_3ARGS_INIT_WORK */

#endif  /* _SPL_WORKQUEUE_COMPAT_H */
