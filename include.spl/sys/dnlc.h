/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
\*****************************************************************************/

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
	int nr = (uintptr_t)reduce_percent * 10000;

	shrink_dcache_memory(nr, GFP_KERNEL);
	shrink_icache_memory(nr, GFP_KERNEL);
	kmem_reap();
}

#endif /* SPL_DNLC_H */
