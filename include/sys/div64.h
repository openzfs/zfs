/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Sun Microsystems, Inc.
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

#ifndef _SPL_DIV64_H
#define _SPL_DIV64_H

#include <asm/div64.h>

#ifndef HAVE_DIV64_64
#if BITS_PER_LONG == 32

extern uint64_t spl_div64_64(uint64_t dividend, uint64_t divisor);
#define div64_64(a,b) spl_div64_64(a,b)

#else /* BITS_PER_LONG == 32 */

static inline uint64_t div64_64(uint64_t dividend, uint64_t divisor)
{
	return dividend / divisor;
}

#endif /* BITS_PER_LONG == 32 */
#endif /* HAVE_DIV64_64 */

#define roundup64(x, y) (div64_64((x) + ((y) - 1), (y)) * (y))

#endif /* _SPL_DIV64_H */
