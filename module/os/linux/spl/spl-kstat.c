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
 *  Solaris Porting Layer (SPL) Kstat Implementation.
 *
 *  Links to Illumos.org for more information on kstat function:
 *  [1] https://illumos.org/man/1M/kstat
 *  [2] https://illumos.org/man/9f/kstat_create
 */

#include <linux/seq_file.h>
#include <sys/kstat.h>
#include <sys/vmem.h>
#include <sys/cmn_err.h>
#include <sys/sysmacros.h>

static kmutex_t kstat_module_lock;
static struct list_head kstat_module_list;
static kid_t kstat_id;

static int
kstat_resize_raw(kstat_t *ksp)
{
	if (ksp->ks_raw_bufsize == KSTAT_RAW_MAX)
		return (ENOMEM);

	vmem_free(ksp->ks_raw_buf, ksp->ks_raw_bufsize);
	ksp->ks_raw_bufsize = MIN(ksp->ks_raw_bufsize * 2, KSTAT_RAW_MAX);
	ksp->ks_raw_buf = vmem_alloc(ksp->ks_raw_bufsize, KM_SLEEP);

	return (0);
}

static int
kstat_seq_show_headers(struct seq_file *f)
{
	kstat_t *ksp = (kstat_t *)f->private;
	int rc = 0;

	ASSERT(ksp->ks_magic == KS_MAGIC);

	seq_printf(f, "%d %d 0x%02x %d %d %lld %lld\n",
	    ksp->ks_kid, ksp->ks_type, ksp->ks_flags,
	    ksp->ks_ndata, (int)ksp->ks_data_size,
	    ksp->ks_crtime, ksp->ks_snaptime);

	switch (ksp->ks_type) {
		case KSTAT_TYPE_RAW:
restart:
			if (ksp->ks_raw_ops.headers) {
				rc = ksp->ks_raw_ops.headers(
				    ksp->ks_raw_buf, ksp->ks_raw_bufsize);
				if (rc == ENOMEM && !kstat_resize_raw(ksp))
					goto restart;
				if (!rc)
					seq_puts(f, ksp->ks_raw_buf);
			} else {
				seq_printf(f, "raw data\n");
			}
			break;
		case KSTAT_TYPE_NAMED:
			seq_printf(f, "%-31s %-4s %s\n",
			    "name", "type", "data");
			break;
		case KSTAT_TYPE_INTR:
			seq_printf(f, "%-8s %-8s %-8s %-8s %-8s\n",
			    "hard", "soft", "watchdog",
			    "spurious", "multsvc");
			break;
		case KSTAT_TYPE_IO:
			seq_printf(f,
			    "%-8s %-8s %-8s %-8s %-8s %-8s "
			    "%-8s %-8s %-8s %-8s %-8s %-8s\n",
			    "nread", "nwritten", "reads", "writes",
			    "wtime", "wlentime", "wupdate",
			    "rtime", "rlentime", "rupdate",
			    "wcnt", "rcnt");
			break;
		case KSTAT_TYPE_TIMER:
			seq_printf(f,
			    "%-31s %-8s "
			    "%-8s %-8s %-8s %-8s %-8s\n",
			    "name", "events", "elapsed",
			    "min", "max", "start", "stop");
			break;
		default:
			PANIC("Undefined kstat type %d\n", ksp->ks_type);
	}

	return (-rc);
}

static int
kstat_seq_show_raw(struct seq_file *f, unsigned char *p, int l)
{
	int i, j;

	for (i = 0; ; i++) {
		seq_printf(f, "%03x:", i);

		for (j = 0; j < 16; j++) {
			if (i * 16 + j >= l) {
				seq_printf(f, "\n");
				goto out;
			}

			seq_printf(f, " %02x", (unsigned char)p[i * 16 + j]);
		}
		seq_printf(f, "\n");
	}
out:
	return (0);
}

static int
kstat_seq_show_named(struct seq_file *f, kstat_named_t *knp)
{
	seq_printf(f, "%-31s %-4d ", knp->name, knp->data_type);

	switch (knp->data_type) {
		case KSTAT_DATA_CHAR:
			knp->value.c[15] = '\0'; /* NULL terminate */
			seq_printf(f, "%-16s", knp->value.c);
			break;
		/*
		 * NOTE - We need to be more careful able what tokens are
		 * used for each arch, for now this is correct for x86_64.
		 */
		case KSTAT_DATA_INT32:
			seq_printf(f, "%d", knp->value.i32);
			break;
		case KSTAT_DATA_UINT32:
			seq_printf(f, "%u", knp->value.ui32);
			break;
		case KSTAT_DATA_INT64:
			seq_printf(f, "%lld", (signed long long)knp->value.i64);
			break;
		case KSTAT_DATA_UINT64:
			seq_printf(f, "%llu",
			    (unsigned long long)knp->value.ui64);
			break;
		case KSTAT_DATA_LONG:
			seq_printf(f, "%ld", knp->value.l);
			break;
		case KSTAT_DATA_ULONG:
			seq_printf(f, "%lu", knp->value.ul);
			break;
		case KSTAT_DATA_STRING:
			KSTAT_NAMED_STR_PTR(knp)
				[KSTAT_NAMED_STR_BUFLEN(knp)-1] = '\0';
			seq_printf(f, "%s", KSTAT_NAMED_STR_PTR(knp));
			break;
		default:
			PANIC("Undefined kstat data type %d\n", knp->data_type);
	}

	seq_printf(f, "\n");

	return (0);
}

static int
kstat_seq_show_intr(struct seq_file *f, kstat_intr_t *kip)
{
	seq_printf(f, "%-8u %-8u %-8u %-8u %-8u\n",
	    kip->intrs[KSTAT_INTR_HARD],
	    kip->intrs[KSTAT_INTR_SOFT],
	    kip->intrs[KSTAT_INTR_WATCHDOG],
	    kip->intrs[KSTAT_INTR_SPURIOUS],
	    kip->intrs[KSTAT_INTR_MULTSVC]);

	return (0);
}

static int
kstat_seq_show_io(struct seq_file *f, kstat_io_t *kip)
{
	/* though wlentime & friends are signed, they will never be negative */
	seq_printf(f,
	    "%-8llu %-8llu %-8u %-8u %-8llu %-8llu "
	    "%-8llu %-8llu %-8llu %-8llu %-8u %-8u\n",
	    kip->nread, kip->nwritten,
	    kip->reads, kip->writes,
	    kip->wtime, kip->wlentime, kip->wlastupdate,
	    kip->rtime, kip->rlentime, kip->rlastupdate,
	    kip->wcnt,  kip->rcnt);

	return (0);
}

static int
kstat_seq_show_timer(struct seq_file *f, kstat_timer_t *ktp)
{
	seq_printf(f,
	    "%-31s %-8llu %-8llu %-8llu %-8llu %-8llu %-8llu\n",
	    ktp->name, ktp->num_events, ktp->elapsed_time,
	    ktp->min_time, ktp->max_time,
	    ktp->start_time, ktp->stop_time);

	return (0);
}

static int
kstat_seq_show(struct seq_file *f, void *p)
{
	kstat_t *ksp = (kstat_t *)f->private;
	int rc = 0;

	ASSERT(ksp->ks_magic == KS_MAGIC);

	switch (ksp->ks_type) {
		case KSTAT_TYPE_RAW:
restart:
			if (ksp->ks_raw_ops.data) {
				rc = ksp->ks_raw_ops.data(
				    ksp->ks_raw_buf, ksp->ks_raw_bufsize, p);
				if (rc == ENOMEM && !kstat_resize_raw(ksp))
					goto restart;
				if (!rc)
					seq_puts(f, ksp->ks_raw_buf);
			} else {
				ASSERT(ksp->ks_ndata == 1);
				rc = kstat_seq_show_raw(f, ksp->ks_data,
				    ksp->ks_data_size);
			}
			break;
		case KSTAT_TYPE_NAMED:
			rc = kstat_seq_show_named(f, (kstat_named_t *)p);
			break;
		case KSTAT_TYPE_INTR:
			rc = kstat_seq_show_intr(f, (kstat_intr_t *)p);
			break;
		case KSTAT_TYPE_IO:
			rc = kstat_seq_show_io(f, (kstat_io_t *)p);
			break;
		case KSTAT_TYPE_TIMER:
			rc = kstat_seq_show_timer(f, (kstat_timer_t *)p);
			break;
		default:
			PANIC("Undefined kstat type %d\n", ksp->ks_type);
	}

	return (-rc);
}

static int
kstat_default_update(kstat_t *ksp, int rw)
{
	ASSERT(ksp != NULL);

	if (rw == KSTAT_WRITE)
		return (EACCES);

	return (0);
}

static void *
kstat_seq_data_addr(kstat_t *ksp, loff_t n)
{
	void *rc = NULL;

	switch (ksp->ks_type) {
		case KSTAT_TYPE_RAW:
			if (ksp->ks_raw_ops.addr)
				rc = ksp->ks_raw_ops.addr(ksp, n);
			else
				rc = ksp->ks_data;
			break;
		case KSTAT_TYPE_NAMED:
			rc = ksp->ks_data + n * sizeof (kstat_named_t);
			break;
		case KSTAT_TYPE_INTR:
			rc = ksp->ks_data + n * sizeof (kstat_intr_t);
			break;
		case KSTAT_TYPE_IO:
			rc = ksp->ks_data + n * sizeof (kstat_io_t);
			break;
		case KSTAT_TYPE_TIMER:
			rc = ksp->ks_data + n * sizeof (kstat_timer_t);
			break;
		default:
			PANIC("Undefined kstat type %d\n", ksp->ks_type);
	}

	return (rc);
}

static void *
kstat_seq_start(struct seq_file *f, loff_t *pos)
{
	loff_t n = *pos;
	kstat_t *ksp = (kstat_t *)f->private;
	ASSERT(ksp->ks_magic == KS_MAGIC);

	mutex_enter(ksp->ks_lock);

	if (ksp->ks_type == KSTAT_TYPE_RAW) {
		ksp->ks_raw_bufsize = PAGE_SIZE;
		ksp->ks_raw_buf = vmem_alloc(ksp->ks_raw_bufsize, KM_SLEEP);
	}

	/* Dynamically update kstat, on error existing kstats are used */
	(void) ksp->ks_update(ksp, KSTAT_READ);

	ksp->ks_snaptime = gethrtime();

	if (!(ksp->ks_flags & KSTAT_FLAG_NO_HEADERS) && !n &&
	    kstat_seq_show_headers(f))
		return (NULL);

	if (n >= ksp->ks_ndata)
		return (NULL);

	return (kstat_seq_data_addr(ksp, n));
}

static void *
kstat_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	kstat_t *ksp = (kstat_t *)f->private;
	ASSERT(ksp->ks_magic == KS_MAGIC);

	++*pos;
	if (*pos >= ksp->ks_ndata)
		return (NULL);

	return (kstat_seq_data_addr(ksp, *pos));
}

static void
kstat_seq_stop(struct seq_file *f, void *v)
{
	kstat_t *ksp = (kstat_t *)f->private;
	ASSERT(ksp->ks_magic == KS_MAGIC);

	if (ksp->ks_type == KSTAT_TYPE_RAW)
		vmem_free(ksp->ks_raw_buf, ksp->ks_raw_bufsize);

	mutex_exit(ksp->ks_lock);
}

static struct seq_operations kstat_seq_ops = {
	.show  = kstat_seq_show,
	.start = kstat_seq_start,
	.next  = kstat_seq_next,
	.stop  = kstat_seq_stop,
};

static kstat_module_t *
kstat_find_module(char *name)
{
	kstat_module_t *module = NULL;

	list_for_each_entry(module, &kstat_module_list, ksm_module_list) {
		if (strncmp(name, module->ksm_name, KSTAT_STRLEN) == 0)
			return (module);
	}

	return (NULL);
}

static kstat_module_t *
kstat_create_module(char *name)
{
	kstat_module_t *module;
	struct proc_dir_entry *pde;

	pde = proc_mkdir(name, proc_spl_kstat);
	if (pde == NULL)
		return (NULL);

	module = kmem_alloc(sizeof (kstat_module_t), KM_SLEEP);
	module->ksm_proc = pde;
	strlcpy(module->ksm_name, name, KSTAT_STRLEN+1);
	INIT_LIST_HEAD(&module->ksm_kstat_list);
	list_add_tail(&module->ksm_module_list, &kstat_module_list);

	return (module);

}

static void
kstat_delete_module(kstat_module_t *module)
{
	ASSERT(list_empty(&module->ksm_kstat_list));
	remove_proc_entry(module->ksm_name, proc_spl_kstat);
	list_del(&module->ksm_module_list);
	kmem_free(module, sizeof (kstat_module_t));
}

static int
proc_kstat_open(struct inode *inode, struct file *filp)
{
	struct seq_file *f;
	int rc;

	rc = seq_open(filp, &kstat_seq_ops);
	if (rc)
		return (rc);

	f = filp->private_data;
	f->private = SPL_PDE_DATA(inode);

	return (0);
}

static ssize_t
proc_kstat_write(struct file *filp, const char __user *buf, size_t len,
    loff_t *ppos)
{
	struct seq_file *f = filp->private_data;
	kstat_t *ksp = f->private;
	int rc;

	ASSERT(ksp->ks_magic == KS_MAGIC);

	mutex_enter(ksp->ks_lock);
	rc = ksp->ks_update(ksp, KSTAT_WRITE);
	mutex_exit(ksp->ks_lock);

	if (rc)
		return (-rc);

	*ppos += len;
	return (len);
}

static const kstat_proc_op_t proc_kstat_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= proc_kstat_open,
	.proc_write	= proc_kstat_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release,
#else
	.open		= proc_kstat_open,
	.write		= proc_kstat_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
#endif
};

void
__kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void *(*addr)(kstat_t *ksp, loff_t index))
{
	ksp->ks_raw_ops.headers = headers;
	ksp->ks_raw_ops.data    = data;
	ksp->ks_raw_ops.addr    = addr;
}
EXPORT_SYMBOL(__kstat_set_raw_ops);

void
kstat_proc_entry_init(kstat_proc_entry_t *kpep, const char *module,
    const char *name)
{
	kpep->kpe_owner = NULL;
	kpep->kpe_proc = NULL;
	INIT_LIST_HEAD(&kpep->kpe_list);
	strncpy(kpep->kpe_module, module, KSTAT_STRLEN);
	strncpy(kpep->kpe_name, name, KSTAT_STRLEN);
}
EXPORT_SYMBOL(kstat_proc_entry_init);

kstat_t *
__kstat_create(const char *ks_module, int ks_instance, const char *ks_name,
    const char *ks_class, uchar_t ks_type, uint_t ks_ndata,
    uchar_t ks_flags)
{
	kstat_t *ksp;

	ASSERT(ks_module);
	ASSERT(ks_instance == 0);
	ASSERT(ks_name);

	if ((ks_type == KSTAT_TYPE_INTR) || (ks_type == KSTAT_TYPE_IO))
		ASSERT(ks_ndata == 1);

	ksp = kmem_zalloc(sizeof (*ksp), KM_SLEEP);
	if (ksp == NULL)
		return (ksp);

	mutex_enter(&kstat_module_lock);
	ksp->ks_kid = kstat_id;
	kstat_id++;
	mutex_exit(&kstat_module_lock);

	ksp->ks_magic = KS_MAGIC;
	mutex_init(&ksp->ks_private_lock, NULL, MUTEX_DEFAULT, NULL);
	ksp->ks_lock = &ksp->ks_private_lock;

	ksp->ks_crtime = gethrtime();
	ksp->ks_snaptime = ksp->ks_crtime;
	ksp->ks_instance = ks_instance;
	strncpy(ksp->ks_class, ks_class, KSTAT_STRLEN);
	ksp->ks_type = ks_type;
	ksp->ks_flags = ks_flags;
	ksp->ks_update = kstat_default_update;
	ksp->ks_private = NULL;
	ksp->ks_raw_ops.headers = NULL;
	ksp->ks_raw_ops.data = NULL;
	ksp->ks_raw_ops.addr = NULL;
	ksp->ks_raw_buf = NULL;
	ksp->ks_raw_bufsize = 0;
	kstat_proc_entry_init(&ksp->ks_proc, ks_module, ks_name);

	switch (ksp->ks_type) {
		case KSTAT_TYPE_RAW:
			ksp->ks_ndata = 1;
			ksp->ks_data_size = ks_ndata;
			break;
		case KSTAT_TYPE_NAMED:
			ksp->ks_ndata = ks_ndata;
			ksp->ks_data_size = ks_ndata * sizeof (kstat_named_t);
			break;
		case KSTAT_TYPE_INTR:
			ksp->ks_ndata = ks_ndata;
			ksp->ks_data_size = ks_ndata * sizeof (kstat_intr_t);
			break;
		case KSTAT_TYPE_IO:
			ksp->ks_ndata = ks_ndata;
			ksp->ks_data_size = ks_ndata * sizeof (kstat_io_t);
			break;
		case KSTAT_TYPE_TIMER:
			ksp->ks_ndata = ks_ndata;
			ksp->ks_data_size = ks_ndata * sizeof (kstat_timer_t);
			break;
		default:
			PANIC("Undefined kstat type %d\n", ksp->ks_type);
	}

	if (ksp->ks_flags & KSTAT_FLAG_VIRTUAL) {
		ksp->ks_data = NULL;
	} else {
		ksp->ks_data = kmem_zalloc(ksp->ks_data_size, KM_SLEEP);
		if (ksp->ks_data == NULL) {
			kmem_free(ksp, sizeof (*ksp));
			ksp = NULL;
		}
	}

	return (ksp);
}
EXPORT_SYMBOL(__kstat_create);

static int
kstat_detect_collision(kstat_proc_entry_t *kpep)
{
	kstat_module_t *module;
	kstat_proc_entry_t *tmp = NULL;
	char *parent;
	char *cp;

	parent = kmem_asprintf("%s", kpep->kpe_module);

	if ((cp = strrchr(parent, '/')) == NULL) {
		kmem_strfree(parent);
		return (0);
	}

	cp[0] = '\0';
	if ((module = kstat_find_module(parent)) != NULL) {
		list_for_each_entry(tmp, &module->ksm_kstat_list, kpe_list) {
			if (strncmp(tmp->kpe_name, cp+1, KSTAT_STRLEN) == 0) {
				kmem_strfree(parent);
				return (EEXIST);
			}
		}
	}

	kmem_strfree(parent);
	return (0);
}

/*
 * Add a file to the proc filesystem under the kstat namespace (i.e.
 * /proc/spl/kstat/). The file need not necessarily be implemented as a
 * kstat.
 */
void
kstat_proc_entry_install(kstat_proc_entry_t *kpep, mode_t mode,
    const kstat_proc_op_t *proc_ops, void *data)
{
	kstat_module_t *module;
	kstat_proc_entry_t *tmp = NULL;

	ASSERT(kpep);

	mutex_enter(&kstat_module_lock);

	module = kstat_find_module(kpep->kpe_module);
	if (module == NULL) {
		if (kstat_detect_collision(kpep) != 0) {
			cmn_err(CE_WARN, "kstat_create('%s', '%s'): namespace" \
			    " collision", kpep->kpe_module, kpep->kpe_name);
			goto out;
		}
		module = kstat_create_module(kpep->kpe_module);
		if (module == NULL)
			goto out;
	}

	/*
	 * Only one entry by this name per-module, on failure the module
	 * shouldn't be deleted because we know it has at least one entry.
	 */
	list_for_each_entry(tmp, &module->ksm_kstat_list, kpe_list) {
		if (strncmp(tmp->kpe_name, kpep->kpe_name, KSTAT_STRLEN) == 0)
			goto out;
	}

	list_add_tail(&kpep->kpe_list, &module->ksm_kstat_list);

	kpep->kpe_owner = module;
	kpep->kpe_proc = proc_create_data(kpep->kpe_name, mode,
	    module->ksm_proc, proc_ops, data);
	if (kpep->kpe_proc == NULL) {
		list_del_init(&kpep->kpe_list);
		if (list_empty(&module->ksm_kstat_list))
			kstat_delete_module(module);
	}
out:
	mutex_exit(&kstat_module_lock);

}
EXPORT_SYMBOL(kstat_proc_entry_install);

void
__kstat_install(kstat_t *ksp)
{
	ASSERT(ksp);
	mode_t mode;
	/* Specify permission modes for different kstats */
	if (strncmp(ksp->ks_proc.kpe_name, "dbufs", KSTAT_STRLEN) == 0) {
		mode = 0600;
	} else {
		mode = 0644;
	}
	kstat_proc_entry_install(
	    &ksp->ks_proc, mode, &proc_kstat_operations, ksp);
}
EXPORT_SYMBOL(__kstat_install);

void
kstat_proc_entry_delete(kstat_proc_entry_t *kpep)
{
	kstat_module_t *module = kpep->kpe_owner;
	if (kpep->kpe_proc)
		remove_proc_entry(kpep->kpe_name, module->ksm_proc);

	mutex_enter(&kstat_module_lock);
	list_del_init(&kpep->kpe_list);

	/*
	 * Remove top level module directory if it wasn't empty before, but now
	 * is.
	 */
	if (kpep->kpe_proc && list_empty(&module->ksm_kstat_list))
		kstat_delete_module(module);
	mutex_exit(&kstat_module_lock);

}
EXPORT_SYMBOL(kstat_proc_entry_delete);

void
__kstat_delete(kstat_t *ksp)
{
	kstat_proc_entry_delete(&ksp->ks_proc);

	if (!(ksp->ks_flags & KSTAT_FLAG_VIRTUAL))
		kmem_free(ksp->ks_data, ksp->ks_data_size);

	ksp->ks_lock = NULL;
	mutex_destroy(&ksp->ks_private_lock);
	kmem_free(ksp, sizeof (*ksp));
}
EXPORT_SYMBOL(__kstat_delete);

int
spl_kstat_init(void)
{
	mutex_init(&kstat_module_lock, NULL, MUTEX_DEFAULT, NULL);
	INIT_LIST_HEAD(&kstat_module_list);
	kstat_id = 0;
	return (0);
}

void
spl_kstat_fini(void)
{
	ASSERT(list_empty(&kstat_module_list));
	mutex_destroy(&kstat_module_lock);
}
