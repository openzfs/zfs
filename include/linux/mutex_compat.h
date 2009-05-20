#ifndef _SPL_MUTEX_COMPAT_H
#define _SPL_MUTEX_COMPAT_H

#include <linux/mutex.h>

/* mutex_lock_nested() introduced in 2.6.18 */
#ifndef HAVE_MUTEX_LOCK_NESTED
# define mutex_lock_nested(lock, subclass)	mutex_lock(lock)
#endif /* HAVE_MUTEX_LOCK_NESTED */

#endif /* _SPL_MUTEX_COMPAT_H */

