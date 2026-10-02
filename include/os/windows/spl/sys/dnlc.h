// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#ifndef _SPL_DNLC_H
#define	_SPL_DNLC_H

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
}

#endif /* SPL_DNLC_H */
