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

#ifndef _SPL_MODULE_COMPAT_H
#define _SPL_MODULE_COMPAT_H

#include <linux/module.h>

#define spl_module_init(init_fn)                                        \
static int                                                              \
spl_##init_fn(void)                                                     \
{                                                                       \
	int rc;                                                         \
	                                                                \
	spl_setup();                                                    \
	rc = init_fn();                                                 \
                                                                        \
	return rc;                                                      \
}                                                                       \
                                                                        \
module_init(spl_##init_fn)

#define spl_module_exit(exit_fn)                                        \
static void                                                             \
spl_##exit_fn(void)                                                     \
{                                                                       \
	int rc;                                                         \
                                                                        \
	rc = exit_fn();                                                 \
	spl_cleanup();                                                  \
	if (rc)                                                         \
		printk(KERN_ERR "SPL: Failure %d unloading "            \
		       "dependent module\n", rc);                       \
}                                                                       \
                                                                        \
module_exit(spl_##exit_fn)

#endif /* _SPL_MODULE_COMPAT_H */
