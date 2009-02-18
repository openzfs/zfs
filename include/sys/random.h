/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#ifndef _SPL_RANDOM_H
#define	_SPL_RANDOM_H

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
	get_random_bytes((void *)ptr,(int)len);
	return 0;
}

 /* Always will succeed by returning 0. */
static __inline__ int
random_get_pseudo_bytes(uint8_t *ptr, size_t len)
{
	get_random_bytes((void *)ptr,(int)len);
	return 0;
}

#ifdef	__cplusplus
}
#endif

#endif	/* _SPL_RANDOM_H */
