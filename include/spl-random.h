#ifndef _SOLARIS_RANDOM_H
#define	_SOLARIS_RANDOM_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <linux/module.h>
#include <linux/random.h>

/* FIXME:
 * Should add support for blocking in the future to
 * ensure that proper entopy is collected.  ZFS doesn't
 * use it at the moment so this is good enough for now.
 * Always will succeed by returning 0.
 */
static __inline__ int
random_get_bytes(uint8_t *ptr, size_t len)
{
	BUG_ON(len < 0);
	get_random_bytes((void *)ptr,(int)len);
	return 0;
}

 /* Always will succeed by returning 0. */
static __inline__ int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	BUG_ON(len < 0);
	get_random_bytes((void *)ptr,(int)len);
	return 0;
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SOLARIS_RANDOM_H */
