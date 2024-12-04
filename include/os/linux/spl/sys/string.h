/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
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
 */

#ifndef _SPL_STRING_H
#define	_SPL_STRING_H

#include <linux/string.h>

#ifndef HAVE_KERNEL_STRLCPY
/*
 * strscpy is strlcpy, but returns an error on truncation. strlcpy is defined
 * to return strlen(src), so detect error and override it.
 */
static inline size_t
strlcpy(char *dest, const char *src, size_t size)
{
	ssize_t ret = strscpy(dest, src, size);
	if (likely(ret > 0))
		return ((size_t)ret);
	return (strlen(src));
}
#endif /* HAVE_KERNEL_STRLCPY */

#endif /* _SPL_STRING_H */
