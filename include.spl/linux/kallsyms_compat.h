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

#ifndef _SPL_KALLSYMS_COMPAT_H
#define _SPL_KALLSYMS_COMPAT_H

#define SYMBOL_POISON ((void*)0xabcddcba)

#ifdef HAVE_KALLSYMS_LOOKUP_NAME

#include <linux/kallsyms.h>
#define spl_kallsyms_lookup_name(name) kallsyms_lookup_name(name)

#else

extern wait_queue_head_t spl_kallsyms_lookup_name_waitq;
typedef unsigned long (*kallsyms_lookup_name_t)(const char *);
extern kallsyms_lookup_name_t spl_kallsyms_lookup_name_fn;
#define spl_kallsyms_lookup_name(name) spl_kallsyms_lookup_name_fn(name)

#endif /* HAVE_KALLSYMS_LOOKUP_NAME */

#endif /* _SPL_KALLSYMS_COMPAT_H */
