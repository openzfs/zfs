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

/*
 * Available debug functions.  These function should be used by any
 * package which needs to integrate with the SPL log infrastructure.
 *
 * SDEBUG()		- Log debug message with specified mask.
 * SDEBUG_LIMIT()	- Log just 1 debug message with specified mask.
 * SWARN()		- Log a warning message.
 * SERROR()		- Log an error message.
 * SEMERG()		- Log an emergency error message.
 * SCONSOLE()		- Log a generic message to the console.
 *
 * SENTRY		- Log entry point to a function.
 * SEXIT		- Log exit point from a function.
 * SRETURN(x)		- Log return from a function.
 * SGOTO(x, y)		- Log goto within a function.
 */

#ifndef _SPL_DEBUG_INTERNAL_H
#define _SPL_DEBUG_INTERNAL_H

//#include <linux/limits.h>
//#include <machine/limits.h>
//#include <linux/sched.h>
#include <osx/sched.h>
#ifdef  __cplusplus
// To make C++ happier about strnlen in kcdata.h
extern "C" {
#endif
#include <sys/debug.h>
#ifdef  __cplusplus
}
#endif



void spl_backtrace(char *thesignal);
int getpcstack(uintptr_t *pcstack, int pcstack_limit);
void print_symbol(uintptr_t symbol);

#endif /* SPL_DEBUG_INTERNAL_H */
