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

#ifndef _SPL_PROC_H
#define _SPL_PROC_H

#include <linux/proc_fs.h>

#ifdef CONFIG_SYSCTL
#ifdef HAVE_2ARGS_REGISTER_SYSCTL
#define spl_register_sysctl_table(t, a)	register_sysctl_table(t, a)
#else
#define spl_register_sysctl_table(t, a)	register_sysctl_table(t)
#endif /* HAVE_2ARGS_REGISTER_SYSCTL */
#define spl_unregister_sysctl_table(t)	unregister_sysctl_table(t)
#endif /* CONFIG_SYSCTL */

#ifdef HAVE_CTL_NAME
#define CTL_NAME(cname)                 .ctl_name = (cname),
#else
#define CTL_NAME(cname)
#endif

extern struct proc_dir_entry *proc_spl_kstat;
struct proc_dir_entry *proc_dir_entry_find(struct proc_dir_entry *root,
					   const char *str);
int proc_dir_entries(struct proc_dir_entry *root);

int spl_proc_init(void);
void spl_proc_fini(void);

#endif /* SPL_PROC_H */
