
#ifndef _SPL_DNLC_H
#define _SPL_DNLC_H

/*
 * Reduce the dcache and icache then reap the free'd slabs.  Note the
 * interface takes a reclaim percentage but we don't have easy access to
 * the total number of entries to calculate the reclaim count.  However,
 * in practice this doesn't need to be even close to correct.  We simply
 * need to reclaim some useful fraction of the cache.  The caller can
 * determine if more needs to be done.
 */
static inline void
dnlc_reduce_cache(void *reduce_percent)
{
#if 0
	int nr = (uintptr_t)reduce_percent * 10000;
	shrink_dcache_memory(nr, GFP_KERNEL);
	shrink_icache_memory(nr, GFP_KERNEL);
	kmem_reap();
#endif
}

#endif /* SPL_DNLC_H */
