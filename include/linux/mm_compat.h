/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
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

#ifndef _SPL_MM_COMPAT_H
#define _SPL_MM_COMPAT_H

#include <linux/mm.h>
#include <linux/fs.h>

/*
 * Linux 2.6.31 API Change.
 * Individual pages_{min,low,high} moved in to watermark array.
 */
#ifndef min_wmark_pages
#define min_wmark_pages(z)	(z->pages_min)
#endif

#ifndef low_wmark_pages
#define low_wmark_pages(z)	(z->pages_low)
#endif

#ifndef high_wmark_pages
#define high_wmark_pages(z)	(z->pages_high)
#endif

/*
 * 2.6.37 API compat,
 * The function invalidate_inodes() is no longer exported by the kernel.
 * The prototype however is still available which means it is safe
 * to acquire the symbol's address using spl_kallsyms_lookup_name().
 */
#ifndef HAVE_INVALIDATE_INODES
typedef int (*invalidate_inodes_t)(struct super_block *sb);
extern invalidate_inodes_t invalidate_inodes_fn;
#define invalidate_inodes(sb)	invalidate_inodes_fn(sb)
#endif /* HAVE_INVALIDATE_INODES */

#endif /* SPL_MM_COMPAT_H */
