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

#ifndef _SPL_PROC_H
#define _SPL_PROC_H

#include <linux/proc_fs.h>
#include <linux/kmod.h>
#include <linux/ctype.h>
#include <linux/sysctl.h>
#include <linux/seq_file.h>
#include <sys/sysmacros.h>
#include <sys/systeminfo.h>
#include <sys/kmem.h>
#include <sys/mutex.h>
#include <sys/kstat.h>
#include <sys/debug.h>

#ifdef CONFIG_SYSCTL
#ifdef HAVE_2ARGS_REGISTER_SYSCTL
#define spl_register_sysctl_table(t, a)	register_sysctl_table(t, a)
#else
#define spl_register_sysctl_table(t, a)	register_sysctl_table(t)
#endif /* HAVE_2ARGS_REGISTER_SYSCTL */
#define spl_unregister_sysctl_table(t)	unregister_sysctl_table(t)
#endif /* CONFIG_SYSCTL */

#ifdef DEBUG_KSTAT
extern struct proc_dir_entry *proc_spl_kstat;
struct proc_dir_entry *proc_dir_entry_find(struct proc_dir_entry *root,
					   const char *str);
int proc_dir_entries(struct proc_dir_entry *root);
#endif

int proc_init(void);
void proc_fini(void);

#endif /* SPL_PROC_H */
