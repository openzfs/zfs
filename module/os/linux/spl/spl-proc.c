// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
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
 *
 *  Solaris Porting Layer (SPL) Proc Implementation.
 */
/*
 * Copyright (c) 2024, Rob Norris <robn@despairlabs.com>
 */

#include <sys/systeminfo.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <sys/proc.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/version.h>
#include "zfs_gitrev.h"

#if defined(CONSTIFY_PLUGIN)
typedef struct ctl_table __no_const spl_ctl_table;
#else
typedef struct ctl_table spl_ctl_table;
#endif

#ifdef HAVE_PROC_HANDLER_CTL_TABLE_CONST
#define	CONST_CTL_TABLE		const struct ctl_table
#else
#define	CONST_CTL_TABLE		struct ctl_table
#endif

static unsigned long table_min = 0;
static unsigned long table_max = ~0;

static struct ctl_table_header *spl_header = NULL;
#ifndef HAVE_REGISTER_SYSCTL_TABLE
static struct ctl_table_header *spl_kmem = NULL;
static struct ctl_table_header *spl_kstat = NULL;
#endif
static struct proc_dir_entry *proc_spl = NULL;
static struct proc_dir_entry *proc_spl_kmem = NULL;
static struct proc_dir_entry *proc_spl_kmem_slab = NULL;
struct proc_dir_entry *proc_spl_kstat = NULL;

#ifdef DEBUG_KMEM
static int
proc_domemused(CONST_CTL_TABLE *table, int write,
    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int rc = 0;
	unsigned long val;
	spl_ctl_table dummy = *table;

	dummy.data = &val;
	dummy.proc_handler = &proc_dointvec;
	dummy.extra1 = &table_min;
	dummy.extra2 = &table_max;

	if (write) {
		*ppos += *lenp;
	} else {
#ifdef HAVE_ATOMIC64_T
		val = atomic64_read((atomic64_t *)table->data);
#else
		val = atomic_read((atomic_t *)table->data);
#endif /* HAVE_ATOMIC64_T */
		rc = proc_doulongvec_minmax(&dummy, write, buffer, lenp, ppos);
	}

	return (rc);
}
#endif /* DEBUG_KMEM */

static int
proc_doslab(CONST_CTL_TABLE *table, int write,
    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	int rc = 0;
	unsigned long val = 0, mask;
	spl_ctl_table dummy = *table;
	spl_kmem_cache_t *skc = NULL;

	dummy.data = &val;
	dummy.proc_handler = &proc_dointvec;
	dummy.extra1 = &table_min;
	dummy.extra2 = &table_max;

	if (write) {
		*ppos += *lenp;
	} else {
		down_read(&spl_kmem_cache_sem);
		mask = (unsigned long)table->data;

		list_for_each_entry(skc, &spl_kmem_cache_list, skc_list) {

			/* Only use slabs of the correct kmem/vmem type */
			if (!(skc->skc_flags & mask))
				continue;

			/* Sum the specified field for selected slabs */
			switch (mask & (KMC_TOTAL | KMC_ALLOC | KMC_MAX)) {
			case KMC_TOTAL:
				val += skc->skc_slab_size * skc->skc_slab_total;
				break;
			case KMC_ALLOC:
				val += skc->skc_obj_size * skc->skc_obj_alloc;
				break;
			case KMC_MAX:
				val += skc->skc_obj_size * skc->skc_obj_max;
				break;
			}
		}

		up_read(&spl_kmem_cache_sem);
		rc = proc_doulongvec_minmax(&dummy, write, buffer, lenp, ppos);
	}

	return (rc);
}

static int
proc_dohostid(CONST_CTL_TABLE *table, int write,
    void __user *buffer, size_t *lenp, loff_t *ppos)
{
	char *end, str[32];
	unsigned long hid;
	spl_ctl_table dummy = *table;

	dummy.data = str;
	dummy.maxlen = sizeof (str) - 1;

	if (!write)
		snprintf(str, sizeof (str), "%lx",
		    (unsigned long) zone_get_hostid(NULL));

	/* always returns 0 */
	proc_dostring(&dummy, write, buffer, lenp, ppos);

	if (write) {
		/*
		 * We can't use proc_doulongvec_minmax() in the write
		 * case here because hostid, while a hex value, has no
		 * leading 0x, which confuses the helper function.
		 */

		hid = simple_strtoul(str, &end, 16);
		if (str == end)
			return (-EINVAL);
		spl_hostid = hid;
	}

	return (0);
}

static void
slab_seq_show_headers(struct seq_file *f)
{
	seq_printf(f,
	    "--------------------- cache ----------"
	    "---------------------------------------------  "
	    "----- slab ------  "
	    "---- object -----  "
	    "--- emergency ---\n");
	seq_printf(f,
	    "name                                  "
	    "  flags      size     alloc slabsize  objsize  "
	    "total alloc   max  "
	    "total alloc   max  "
	    "dlock alloc   max\n");
}

static int
slab_seq_show(struct seq_file *f, void *p)
{
	spl_kmem_cache_t *skc = p;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	if (skc->skc_flags & KMC_SLAB) {
		/*
		 * This cache is backed by a generic Linux kmem cache which
		 * has its own accounting. For these caches we only track
		 * the number of active allocated objects that exist within
		 * the underlying Linux slabs. For the overall statistics of
		 * the underlying Linux cache please refer to /proc/slabinfo.
		 */
		spin_lock(&skc->skc_lock);
		uint64_t objs_allocated =
		    percpu_counter_sum(&skc->skc_linux_alloc);
		seq_printf(f, "%-36s  ", skc->skc_name);
		seq_printf(f, "0x%05lx %9s %9lu %8s %8u  "
		    "%5s %5s %5s  %5s %5lu %5s  %5s %5s %5s\n",
		    (long unsigned)skc->skc_flags,
		    "-",
		    (long unsigned)(skc->skc_obj_size * objs_allocated),
		    "-",
		    (unsigned)skc->skc_obj_size,
		    "-", "-", "-", "-",
		    (long unsigned)objs_allocated,
		    "-", "-", "-", "-");
		spin_unlock(&skc->skc_lock);
		return (0);
	}

	spin_lock(&skc->skc_lock);
	seq_printf(f, "%-36s  ", skc->skc_name);
	seq_printf(f, "0x%05lx %9lu %9lu %8u %8u  "
	    "%5lu %5lu %5lu  %5lu %5lu %5lu  %5lu %5lu %5lu\n",
	    (long unsigned)skc->skc_flags,
	    (long unsigned)(skc->skc_slab_size * skc->skc_slab_total),
	    (long unsigned)(skc->skc_obj_size * skc->skc_obj_alloc),
	    (unsigned)skc->skc_slab_size,
	    (unsigned)skc->skc_obj_size,
	    (long unsigned)skc->skc_slab_total,
	    (long unsigned)skc->skc_slab_alloc,
	    (long unsigned)skc->skc_slab_max,
	    (long unsigned)skc->skc_obj_total,
	    (long unsigned)skc->skc_obj_alloc,
	    (long unsigned)skc->skc_obj_max,
	    (long unsigned)skc->skc_obj_deadlock,
	    (long unsigned)skc->skc_obj_emergency,
	    (long unsigned)skc->skc_obj_emergency_max);
	spin_unlock(&skc->skc_lock);
	return (0);
}

static void *
slab_seq_start(struct seq_file *f, loff_t *pos)
{
	struct list_head *p;
	loff_t n = *pos;

	down_read(&spl_kmem_cache_sem);
	if (!n)
		slab_seq_show_headers(f);

	p = spl_kmem_cache_list.next;
	while (n--) {
		p = p->next;
		if (p == &spl_kmem_cache_list)
			return (NULL);
	}

	return (list_entry(p, spl_kmem_cache_t, skc_list));
}

static void *
slab_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	spl_kmem_cache_t *skc = p;

	++*pos;
	return ((skc->skc_list.next == &spl_kmem_cache_list) ?
	    NULL : list_entry(skc->skc_list.next, spl_kmem_cache_t, skc_list));
}

static void
slab_seq_stop(struct seq_file *f, void *v)
{
	up_read(&spl_kmem_cache_sem);
}

static const struct seq_operations slab_seq_ops = {
	.show  = slab_seq_show,
	.start = slab_seq_start,
	.next  = slab_seq_next,
	.stop  = slab_seq_stop,
};

static int
proc_slab_open(struct inode *inode, struct file *filp)
{
	return (seq_open(filp, &slab_seq_ops));
}

static const kstat_proc_op_t proc_slab_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= proc_slab_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
#else
	.open		= proc_slab_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
#endif
};

static struct ctl_table spl_kmem_table[] = {
#ifdef DEBUG_KMEM
	{
		.procname	= "kmem_used",
		.data		= &kmem_alloc_used,
#ifdef HAVE_ATOMIC64_T
		.maxlen		= sizeof (atomic64_t),
#else
		.maxlen		= sizeof (atomic_t),
#endif /* HAVE_ATOMIC64_T */
		.mode		= 0444,
		.proc_handler	= &proc_domemused,
	},
	{
		.procname	= "kmem_max",
		.data		= &kmem_alloc_max,
		.maxlen		= sizeof (unsigned long),
		.extra1		= &table_min,
		.extra2		= &table_max,
		.mode		= 0444,
		.proc_handler	= &proc_doulongvec_minmax,
	},
#endif /* DEBUG_KMEM */
	{
		.procname	= "slab_kvmem_total",
		.data		= (void *)(KMC_KVMEM | KMC_TOTAL),
		.maxlen		= sizeof (unsigned long),
		.extra1		= &table_min,
		.extra2		= &table_max,
		.mode		= 0444,
		.proc_handler	= &proc_doslab,
	},
	{
		.procname	= "slab_kvmem_alloc",
		.data		= (void *)(KMC_KVMEM | KMC_ALLOC),
		.maxlen		= sizeof (unsigned long),
		.extra1		= &table_min,
		.extra2		= &table_max,
		.mode		= 0444,
		.proc_handler	= &proc_doslab,
	},
	{
		.procname	= "slab_kvmem_max",
		.data		= (void *)(KMC_KVMEM | KMC_MAX),
		.maxlen		= sizeof (unsigned long),
		.extra1		= &table_min,
		.extra2		= &table_max,
		.mode		= 0444,
		.proc_handler	= &proc_doslab,
	},
	{},
};

static struct ctl_table spl_kstat_table[] = {
	{},
};

static struct ctl_table spl_table[] = {
	/*
	 * NB No .strategy entries have been provided since
	 * sysctl(8) prefers to go via /proc for portability.
	 */
	{
		.procname	= "gitrev",
		.data		= (char *)ZFS_META_GITREV,
		.maxlen		= sizeof (ZFS_META_GITREV),
		.mode		= 0444,
		.proc_handler	= &proc_dostring,
	},
	{
		.procname	= "hostid",
		.data		= &spl_hostid,
		.maxlen		= sizeof (unsigned long),
		.mode		= 0644,
		.proc_handler	= &proc_dohostid,
	},
#ifdef HAVE_REGISTER_SYSCTL_TABLE
	{
		.procname	= "kmem",
		.mode		= 0555,
		.child		= spl_kmem_table,
	},
	{
		.procname	= "kstat",
		.mode		= 0555,
		.child		= spl_kstat_table,
	},
#endif
	{},
};

#ifdef HAVE_REGISTER_SYSCTL_TABLE
static struct ctl_table spl_dir[] = {
	{
		.procname	= "spl",
		.mode		= 0555,
		.child		= spl_table,
	},
	{}
};

static struct ctl_table spl_root[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= spl_dir,
	},
	{}
};
#endif

static void spl_proc_cleanup(void)
{
	remove_proc_entry("kstat", proc_spl);
	remove_proc_entry("slab", proc_spl_kmem);
	remove_proc_entry("kmem", proc_spl);
	remove_proc_entry("spl", NULL);

#ifndef HAVE_REGISTER_SYSCTL_TABLE
	if (spl_kstat) {
		unregister_sysctl_table(spl_kstat);
		spl_kstat = NULL;
	}
	if (spl_kmem) {
		unregister_sysctl_table(spl_kmem);
		spl_kmem = NULL;
	}
#endif
	if (spl_header) {
		unregister_sysctl_table(spl_header);
		spl_header = NULL;
	}
}

#ifndef HAVE_REGISTER_SYSCTL_TABLE

/*
 * Traditionally, struct ctl_table arrays have been terminated by an "empty"
 * sentinel element (specifically, one with .procname == NULL).
 *
 * Linux 6.6 began migrating away from this, adding register_sysctl_sz() so
 * that callers could provide the size directly, and redefining
 * register_sysctl() to just call register_sysctl_sz() with the array size. It
 * retained support for the terminating element so that existing callers would
 * continue to work.
 *
 * Linux 6.11 removed support for the terminating element, instead interpreting
 * it as a real malformed element, and rejecting it.
 *
 * In order to continue support older kernels, we retain the terminating
 * sentinel element for our sysctl tables, but instead detect availability of
 * register_sysctl_sz(). If it exists, we pass it the array size -1, stopping
 * the kernel from trying to process the terminator. For pre-6.6 kernels that
 * don't have register_sysctl_sz(), we just use register_sysctl(), which can
 * handle the terminating element as it always has.
 */
#ifdef HAVE_REGISTER_SYSCTL_SZ
#define	spl_proc_register_sysctl(p, t)	\
	register_sysctl_sz(p, t, ARRAY_SIZE(t)-1)
#else
#define	spl_proc_register_sysctl(p, t)	\
	register_sysctl(p, t)
#endif
#endif

int
spl_proc_init(void)
{
	int rc = 0;

#ifdef HAVE_REGISTER_SYSCTL_TABLE
	spl_header = register_sysctl_table(spl_root);
	if (spl_header == NULL)
		return (-EUNATCH);
#else
	spl_header = spl_proc_register_sysctl("kernel/spl", spl_table);
	if (spl_header == NULL)
		return (-EUNATCH);

	spl_kmem = spl_proc_register_sysctl("kernel/spl/kmem", spl_kmem_table);
	if (spl_kmem == NULL) {
		rc = -EUNATCH;
		goto out;
	}
	spl_kstat = spl_proc_register_sysctl("kernel/spl/kstat",
	    spl_kstat_table);
	if (spl_kstat == NULL) {
		rc = -EUNATCH;
		goto out;
	}
#endif

	proc_spl = proc_mkdir("spl", NULL);
	if (proc_spl == NULL) {
		rc = -EUNATCH;
		goto out;
	}

	proc_spl_kmem = proc_mkdir("kmem", proc_spl);
	if (proc_spl_kmem == NULL) {
		rc = -EUNATCH;
		goto out;
	}

	proc_spl_kmem_slab = proc_create_data("slab", 0444, proc_spl_kmem,
	    &proc_slab_operations, NULL);
	if (proc_spl_kmem_slab == NULL) {
		rc = -EUNATCH;
		goto out;
	}

	proc_spl_kstat = proc_mkdir("kstat", proc_spl);
	if (proc_spl_kstat == NULL) {
		rc = -EUNATCH;
		goto out;
	}
out:
	if (rc)
		spl_proc_cleanup();

	return (rc);
}

void
spl_proc_fini(void)
{
	spl_proc_cleanup();
}
