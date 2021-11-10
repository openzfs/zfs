/*
 *  Copyright (C) 2018 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
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
#ifndef _SPL_SYS_STRINGS_H
#define	_SPL_SYS_STRINGS_H

#include <linux/string.h>

#define	bzero(ptr, size)		memset(ptr, 0, size)
#define	bcopy(src, dest, size)		memmove(dest, src, size)
#define	bcmp(src, dest, size)		memcmp((src), (dest), (size_t)(size))

#endif	/* _SPL_SYS_STRINGS_H */
