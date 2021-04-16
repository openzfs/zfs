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

#include <sys/systeminfo.h>
#include <sys/kstat.h>
#include <sys/kmem.h>
#include <sys/kmem_cache.h>
#include <sys/vmem.h>
#include <sys/taskq.h>
#include <sys/proc.h>
#include <linux/ctype.h>
#include <linux/kmod.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/version.h>

#if defined(CONSTIFY_PLUGIN) && LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0)
typedef struct ctl_table __no_const spl_ctl_table;
#else
typedef struct ctl_table spl_ctl_table;
#endif

static unsigned long table_min = 0;
static unsigned long table_max = ~0;

static struct ctl_table_header *spl_header = NULL;
static struct proc_dir_entry *proc_spl = NULL;
static struct proc_dir_entry *proc_spl_kmem = NULL;
static struct proc_dir_entry *proc_spl_kmem_slab = NULL;
static struct proc_dir_entry *proc_spl_taskq_all = NULL;
static struct proc_dir_entry *proc_spl_taskq = NULL;
struct proc_dir_entry *proc_spl_kstat = NULL;

#ifdef DEBUG_KMEM
static int
proc_domemused(struct ctl_table *table, int write,
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
proc_doslab(struct ctl_table *table, int write,
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
proc_dohostid(struct ctl_table *table, int write,
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
taskq_seq_show_headers(struct seq_file *f)
{
	seq_printf(f, "%-25s %5s %5s %5s %5s %5s %5s %12s %5s %10s\n",
	    "taskq", "act", "nthr", "spwn", "maxt", "pri",
	    "mina", "maxa", "cura", "flags");
}

/* indices into the lheads array below */
#define	LHEAD_PEND	0
#define	LHEAD_PRIO	1
#define	LHEAD_DELAY	2
#define	LHEAD_WAIT	3
#define	LHEAD_ACTIVE	4
#define	LHEAD_SIZE	5

/* BEGIN CSTYLED */
static unsigned int spl_max_show_tasks = 512;
module_param(spl_max_show_tasks, uint, 0644);
MODULE_PARM_DESC(spl_max_show_tasks, "Max number of tasks shown in taskq proc");
/* END CSTYLED */

static int
taskq_seq_show_impl(struct seq_file *f, void *p, boolean_t allflag)
{
	taskq_t *tq = p;
	taskq_thread_t *tqt = NULL;
	spl_wait_queue_entry_t *wq;
	struct task_struct *tsk;
	taskq_ent_t *tqe;
	char name[100];
	struct list_head *lheads[LHEAD_SIZE], *lh;
	static char *list_names[LHEAD_SIZE] =
	    {"pend", "prio", "delay", "wait", "active" };
	int i, j, have_lheads = 0;
	unsigned long wflags, flags;

	spin_lock_irqsave_nested(&tq->tq_lock, flags, tq->tq_lock_class);
	spin_lock_irqsave(&tq->tq_wait_waitq.lock, wflags);

	/* get the various lists and check whether they're empty */
	lheads[LHEAD_PEND] = &tq->tq_pend_list;
	lheads[LHEAD_PRIO] = &tq->tq_prio_list;
	lheads[LHEAD_DELAY] = &tq->tq_delay_list;
#ifdef HAVE_WAIT_QUEUE_HEAD_ENTRY
	lheads[LHEAD_WAIT] = &tq->tq_wait_waitq.head;
#else
	lheads[LHEAD_WAIT] = &tq->tq_wait_waitq.task_list;
#endif
	lheads[LHEAD_ACTIVE] = &tq->tq_active_list;

	for (i = 0; i < LHEAD_SIZE; ++i) {
		if (list_empty(lheads[i]))
			lheads[i] = NULL;
		else
			++have_lheads;
	}

	/* early return in non-"all" mode if lists are all empty */
	if (!allflag && !have_lheads) {
		spin_unlock_irqrestore(&tq->tq_wait_waitq.lock, wflags);
		spin_unlock_irqrestore(&tq->tq_lock, flags);
		return (0);
	}

	/* unlock the waitq quickly */
	if (!lheads[LHEAD_WAIT])
		spin_unlock_irqrestore(&tq->tq_wait_waitq.lock, wflags);

	/* show the base taskq contents */
	snprintf(name, sizeof (name), "%s/%d", tq->tq_name, tq->tq_instance);
	seq_printf(f, "%-25s ", name);
	seq_printf(f, "%5d %5d %5d %5d %5d %5d %12d %5d %10x\n",
	    tq->tq_nactive, tq->tq_nthreads, tq->tq_nspawn,
	    tq->tq_maxthreads, tq->tq_pri, tq->tq_minalloc, tq->tq_maxalloc,
	    tq->tq_nalloc, tq->tq_flags);

	/* show the active list */
	if (lheads[LHEAD_ACTIVE]) {
		j = 0;
		list_for_each_entry(tqt, &tq->tq_active_list, tqt_active_list) {
			if (j == 0)
				seq_printf(f, "\t%s:",
				    list_names[LHEAD_ACTIVE]);
			else if (j == 2) {
				seq_printf(f, "\n\t       ");
				j = 0;
			}
			seq_printf(f, " [%d]%pf(%ps)",
			    tqt->tqt_thread->pid,
			    tqt->tqt_task->tqent_func,
			    tqt->tqt_task->tqent_arg);
			++j;
		}
		seq_printf(f, "\n");
	}

	for (i = LHEAD_PEND; i <= LHEAD_WAIT; ++i)
		if (lheads[i]) {
			j = 0;
			list_for_each(lh, lheads[i]) {
				if (spl_max_show_tasks != 0 &&
				    j >= spl_max_show_tasks) {
					seq_printf(f, "\n\t(truncated)");
					break;
				}
				/* show the wait waitq list */
				if (i == LHEAD_WAIT) {
#ifdef HAVE_WAIT_QUEUE_HEAD_ENTRY
					wq = list_entry(lh,
					    spl_wait_queue_entry_t, entry);
#else
					wq = list_entry(lh,
					    spl_wait_queue_entry_t, task_list);
#endif
					if (j == 0)
						seq_printf(f, "\t%s:",
						    list_names[i]);
					else if (j % 8 == 0)
						seq_printf(f, "\n\t     ");

					tsk = wq->private;
					seq_printf(f, " %d", tsk->pid);
				/* pend, prio and delay lists */
				} else {
					tqe = list_entry(lh, taskq_ent_t,
					    tqent_list);
					if (j == 0)
						seq_printf(f, "\t%s:",
						    list_names[i]);
					else if (j % 2 == 0)
						seq_printf(f, "\n\t     ");

					seq_printf(f, " %pf(%ps)",
					    tqe->tqent_func,
					    tqe->tqent_arg);
				}
				++j;
			}
			seq_printf(f, "\n");
		}
	if (lheads[LHEAD_WAIT])
		spin_unlock_irqrestore(&tq->tq_wait_waitq.lock, wflags);
	spin_unlock_irqrestore(&tq->tq_lock, flags);

	return (0);
}

static int
taskq_all_seq_show(struct seq_file *f, void *p)
{
	return (taskq_seq_show_impl(f, p, B_TRUE));
}

static int
taskq_seq_show(struct seq_file *f, void *p)
{
	return (taskq_seq_show_impl(f, p, B_FALSE));
}

static void *
taskq_seq_start(struct seq_file *f, loff_t *pos)
{
	struct list_head *p;
	loff_t n = *pos;

	down_read(&tq_list_sem);
	if (!n)
		taskq_seq_show_headers(f);

	p = tq_list.next;
	while (n--) {
		p = p->next;
		if (p == &tq_list)
		return (NULL);
	}

	return (list_entry(p, taskq_t, tq_taskqs));
}

static void *
taskq_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	taskq_t *tq = p;

	++*pos;
	return ((tq->tq_taskqs.next == &tq_list) ?
	    NULL : list_entry(tq->tq_taskqs.next, taskq_t, tq_taskqs));
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

static struct seq_operations slab_seq_ops = {
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

static void
taskq_seq_stop(struct seq_file *f, void *v)
{
	up_read(&tq_list_sem);
}

static struct seq_operations taskq_all_seq_ops = {
	.show	= taskq_all_seq_show,
	.start	= taskq_seq_start,
	.next	= taskq_seq_next,
	.stop	= taskq_seq_stop,
};

static struct seq_operations taskq_seq_ops = {
	.show	= taskq_seq_show,
	.start	= taskq_seq_start,
	.next	= taskq_seq_next,
	.stop	= taskq_seq_stop,
};

static int
proc_taskq_all_open(struct inode *inode, struct file *filp)
{
	return (seq_open(filp, &taskq_all_seq_ops));
}

static int
proc_taskq_open(struct inode *inode, struct file *filp)
{
	return (seq_open(filp, &taskq_seq_ops));
}

static const kstat_proc_op_t proc_taskq_all_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= proc_taskq_all_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
#else
	.open		= proc_taskq_all_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
#endif
};

static const kstat_proc_op_t proc_taskq_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= proc_taskq_open,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
#else
	.open		= proc_taskq_open,
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
		.data		= spl_gitrev,
		.maxlen		= sizeof (spl_gitrev),
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
	{},
};

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
	.procname = "kernel",
	.mode = 0555,
	.child = spl_dir,
	},
	{}
};

int
spl_proc_init(void)
{
	int rc = 0;

	spl_header = register_sysctl_table(spl_root);
	if (spl_header == NULL)
		return (-EUNATCH);

	proc_spl = proc_mkdir("spl", NULL);
	if (proc_spl == NULL) {
		rc = -EUNATCH;
		goto out;
	}

	proc_spl_taskq_all = proc_create_data("taskq-all", 0444, proc_spl,
	    &proc_taskq_all_operations, NULL);
	if (proc_spl_taskq_all == NULL) {
		rc = -EUNATCH;
		goto out;
	}

	proc_spl_taskq = proc_create_data("taskq", 0444, proc_spl,
	    &proc_taskq_operations, NULL);
	if (proc_spl_taskq == NULL) {
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
	if (rc) {
		remove_proc_entry("kstat", proc_spl);
		remove_proc_entry("slab", proc_spl_kmem);
		remove_proc_entry("kmem", proc_spl);
		remove_proc_entry("taskq-all", proc_spl);
		remove_proc_entry("taskq", proc_spl);
		remove_proc_entry("spl", NULL);
		unregister_sysctl_table(spl_header);
	}

	return (rc);
}

void
spl_proc_fini(void)
{
	remove_proc_entry("kstat", proc_spl);
	remove_proc_entry("slab", proc_spl_kmem);
	remove_proc_entry("kmem", proc_spl);
	remove_proc_entry("taskq-all", proc_spl);
	remove_proc_entry("taskq", proc_spl);
	remove_proc_entry("spl", NULL);

	ASSERT(spl_header != NULL);
	unregister_sysctl_table(spl_header);
}
