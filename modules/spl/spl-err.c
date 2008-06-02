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

#include <sys/sysmacros.h>
#include <sys/cmn_err.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_GENERIC

#ifndef NDEBUG
static char ce_prefix[CE_IGNORE][10] = { "", "NOTICE: ", "WARNING: ", "" };
static char ce_suffix[CE_IGNORE][2] = { "", "\n", "\n", "" };
#endif

void
vpanic(const char *fmt, va_list ap)
{
	char msg[MAXMSGLEN];

	vsnprintf(msg, MAXMSGLEN - 1, fmt, ap);
	panic(msg);
} /* vpanic() */
EXPORT_SYMBOL(vpanic);

void
cmn_err(int ce, const char *fmt, ...)
{
	char msg[MAXMSGLEN];
	va_list ap;

	va_start(ap, fmt);
	vsnprintf(msg, MAXMSGLEN - 1, fmt, ap);
	va_end(ap);

	CERROR("%s", msg);
} /* cmn_err() */
EXPORT_SYMBOL(cmn_err);

void
vcmn_err(int ce, const char *fmt, va_list ap)
{
	char msg[MAXMSGLEN];

        if (ce == CE_PANIC)
                vpanic(fmt, ap);

        if (ce != CE_NOTE) { /* suppress noise in stress testing */
		vsnprintf(msg, MAXMSGLEN - 1, fmt, ap);
		CERROR("%s%s%s", ce_prefix[ce], msg, ce_suffix[ce]);
        }
} /* vcmn_err() */
EXPORT_SYMBOL(vcmn_err);
