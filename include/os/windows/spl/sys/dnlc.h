/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
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
