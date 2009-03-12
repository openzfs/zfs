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

#include <sys/proc.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_PROC

#ifdef DEBUG_KMEM
static unsigned long table_min = 0;
static unsigned long table_max = ~0;
#endif

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *spl_header = NULL;
#endif /* CONFIG_SYSCTL */

#if defined(DEBUG_MUTEX) || defined(DEBUG_KMEM) || defined(DEBUG_KSTAT)
static struct proc_dir_entry *proc_spl = NULL;
#ifdef DEBUG_MUTEX
static struct proc_dir_entry *proc_spl_mutex = NULL;
static struct proc_dir_entry *proc_spl_mutex_stats = NULL;
#endif /* DEBUG_MUTEX */
#ifdef DEBUG_KMEM
static struct proc_dir_entry *proc_spl_kmem = NULL;
static struct proc_dir_entry *proc_spl_kmem_slab = NULL;
#endif /* DEBUG_KMEM */
#ifdef DEBUG_KSTAT
struct proc_dir_entry *proc_spl_kstat = NULL;
#endif /* DEBUG_KSTAT */
#endif /* DEBUG_MUTEX || DEBUG_KMEM || DEBUG_KSTAT */

#ifdef HAVE_CTL_UNNUMBERED

#define CTL_SPL			CTL_UNNUMBERED
#define CTL_SPL_DEBUG		CTL_UNNUMBERED
#define CTL_SPL_VM		CTL_UNNUMBERED
#define CTL_SPL_MUTEX		CTL_UNNUMBERED
#define CTL_SPL_KMEM		CTL_UNNUMBERED
#define CTL_SPL_KSTAT		CTL_UNNUMBERED

#define CTL_VERSION		CTL_UNNUMBERED /* Version */
#define CTL_HOSTID		CTL_UNNUMBERED /* Host id by /usr/bin/hostid */
#define CTL_HW_SERIAL		CTL_UNNUMBERED /* HW serial number by hostid */
#define CTL_KALLSYMS		CTL_UNNUMBERED /* kallsyms_lookup_name addr */

#define CTL_DEBUG_SUBSYS	CTL_UNNUMBERED /* Debug subsystem */
#define CTL_DEBUG_MASK		CTL_UNNUMBERED /* Debug mask */
#define CTL_DEBUG_PRINTK	CTL_UNNUMBERED /* All messages to console */
#define CTL_DEBUG_MB		CTL_UNNUMBERED /* Debug buffer size */
#define CTL_DEBUG_BINARY	CTL_UNNUMBERED /* Binary data in buffer */
#define CTL_DEBUG_CATASTROPHE	CTL_UNNUMBERED /* Set if BUG'd or panic'd */
#define CTL_DEBUG_PANIC_ON_BUG	CTL_UNNUMBERED /* Should panic on BUG */
#define CTL_DEBUG_PATH		CTL_UNNUMBERED /* Dump log location */
#define CTL_DEBUG_DUMP		CTL_UNNUMBERED /* Dump debug buffer to file */
#define CTL_DEBUG_FORCE_BUG	CTL_UNNUMBERED /* Hook to force a BUG */
#define CTL_DEBUG_STACK_SIZE	CTL_UNNUMBERED /* Max observed stack size */

#define CTL_CONSOLE_RATELIMIT	CTL_UNNUMBERED /* Ratelimit console messages */
#define CTL_CONSOLE_MAX_DELAY_CS CTL_UNNUMBERED /* Max delay skip messages */
#define CTL_CONSOLE_MIN_DELAY_CS CTL_UNNUMBERED /* Init delay skip messages */
#define CTL_CONSOLE_BACKOFF	CTL_UNNUMBERED /* Delay increase factor */

#define CTL_VM_MINFREE		CTL_UNNUMBERED /* Minimum free memory */
#define CTL_VM_DESFREE		CTL_UNNUMBERED /* Desired free memory */
#define CTL_VM_LOTSFREE		CTL_UNNUMBERED /* Lots of free memory */
#define CTL_VM_NEEDFREE		CTL_UNNUMBERED /* Need free memory */
#define CTL_VM_SWAPFS_MINFREE	CTL_UNNUMBERED /* Minimum swapfs memory */
#define CTL_VM_SWAPFS_RESERVE	CTL_UNNUMBERED /* Reserved swapfs memory */
#define CTL_VM_AVAILRMEM	CTL_UNNUMBERED /* Easily available memory */
#define CTL_VM_FREEMEM		CTL_UNNUMBERED /* Free memory */
#define CTL_VM_PHYSMEM		CTL_UNNUMBERED /* Total physical memory */

#ifdef DEBUG_KMEM
#define CTL_KMEM_KMEMUSED	CTL_UNNUMBERED /* Alloc'd kmem bytes */
#define CTL_KMEM_KMEMMAX	CTL_UNNUMBERED /* Max alloc'd by kmem bytes */
#define CTL_KMEM_VMEMUSED	CTL_UNNUMBERED /* Alloc'd vmem bytes */
#define CTL_KMEM_VMEMMAX	CTL_UNNUMBERED /* Max alloc'd by vmem bytes */
#define CTL_KMEM_ALLOC_FAILED	CTL_UNNUMBERED /* Cache allocations failed */
#endif

#define CTL_MUTEX_STATS		CTL_UNNUMBERED /* Global mutex statistics */
#define CTL_MUTEX_STATS_PER	CTL_UNNUMBERED /* Per mutex statistics */
#define CTL_MUTEX_SPIN_MAX	CTL_UNNUMBERED /* Max mutex spin iterations */

#else /* HAVE_CTL_UNNUMBERED */

enum {
	CTL_SPL = 0x87,
	CTL_SPL_DEBUG = 0x88,
	CTL_SPL_VM = 0x89,
	CTL_SPL_MUTEX = 0x90,
	CTL_SPL_KMEM = 0x91,
	CTL_SPL_KSTAT = 0x92,
};

enum {
	CTL_VERSION = 1,		/* Version */
	CTL_HOSTID,			/* Host id reported by /usr/bin/hostid */
	CTL_HW_SERIAL,			/* Hardware serial number from hostid */
	CTL_KALLSYMS,			/* Address of kallsyms_lookup_name */

	CTL_DEBUG_SUBSYS,		/* Debug subsystem */
	CTL_DEBUG_MASK,			/* Debug mask */
	CTL_DEBUG_PRINTK,		/* Force all messages to console */
	CTL_DEBUG_MB,			/* Debug buffer size */
	CTL_DEBUG_BINARY,		/* Include binary data in buffer */
	CTL_DEBUG_CATASTROPHE,		/* Set if we have BUG'd or panic'd */
	CTL_DEBUG_PANIC_ON_BUG,		/* Set if we should panic on BUG */
	CTL_DEBUG_PATH,			/* Dump log location */
	CTL_DEBUG_DUMP,			/* Dump debug buffer to file */
	CTL_DEBUG_FORCE_BUG,		/* Hook to force a BUG */
	CTL_DEBUG_STACK_SIZE,		/* Max observed stack size */

	CTL_CONSOLE_RATELIMIT,		/* Ratelimit console messages */
	CTL_CONSOLE_MAX_DELAY_CS,	/* Max delay which we skip messages */
	CTL_CONSOLE_MIN_DELAY_CS,	/* Init delay which we skip messages */
	CTL_CONSOLE_BACKOFF,		/* Delay increase factor */

	CTL_VM_MINFREE,			/* Minimum free memory threshold */
	CTL_VM_DESFREE,			/* Desired free memory threshold */
	CTL_VM_LOTSFREE,		/* Lots of free memory threshold */
	CTL_VM_NEEDFREE,		/* Need free memory deficit */
	CTL_VM_SWAPFS_MINFREE,		/* Minimum swapfs memory */
	CTL_VM_SWAPFS_RESERVE,		/* Reserved swapfs memory */
	CTL_VM_AVAILRMEM,		/* Easily available memory */
	CTL_VM_FREEMEM,			/* Free memory */
	CTL_VM_PHYSMEM,			/* Total physical memory */

#ifdef DEBUG_KMEM
	CTL_KMEM_KMEMUSED,		/* Alloc'd kmem bytes */
	CTL_KMEM_KMEMMAX,		/* Max alloc'd by kmem bytes */
	CTL_KMEM_VMEMUSED,		/* Alloc'd vmem bytes */
	CTL_KMEM_VMEMMAX,		/* Max alloc'd by vmem bytes */
#endif

	CTL_MUTEX_STATS,		/* Global mutex statistics */
	CTL_MUTEX_STATS_PER,		/* Per mutex statistics */
	CTL_MUTEX_SPIN_MAX,		/* Maximum mutex spin iterations */
};
#endif /* HAVE_CTL_UNNUMBERED */

static int
proc_copyin_string(char *kbuffer, int kbuffer_size,
                   const char *ubuffer, int ubuffer_size)
{
        int size;

        if (ubuffer_size > kbuffer_size)
                return -EOVERFLOW;

        if (copy_from_user((void *)kbuffer, (void *)ubuffer, ubuffer_size))
                return -EFAULT;

        /* strip trailing whitespace */
        size = strnlen(kbuffer, ubuffer_size);
        while (size-- >= 0)
                if (!isspace(kbuffer[size]))
                        break;

        /* empty string */
        if (size < 0)
                return -EINVAL;

        /* no space to terminate */
        if (size == kbuffer_size)
                return -EOVERFLOW;

        kbuffer[size + 1] = 0;
        return 0;
}

static int
proc_copyout_string(char *ubuffer, int ubuffer_size,
                    const char *kbuffer, char *append)
{
        /* NB if 'append' != NULL, it's a single character to append to the
         * copied out string - usually "\n", for /proc entries and
         * (i.e. a terminating zero byte) for sysctl entries
         */
        int size = MIN(strlen(kbuffer), ubuffer_size);

        if (copy_to_user(ubuffer, kbuffer, size))
                return -EFAULT;

        if (append != NULL && size < ubuffer_size) {
                if (copy_to_user(ubuffer + size, append, 1))
                        return -EFAULT;

                size++;
        }

        return size;
}

static int
proc_dobitmasks(struct ctl_table *table, int write, struct file *filp,
                void __user *buffer, size_t *lenp, loff_t *ppos)
{
        unsigned long *mask = table->data;
        int is_subsys = (mask == &spl_debug_subsys) ? 1 : 0;
        int is_printk = (mask == &spl_debug_printk) ? 1 : 0;
        int size = 512, rc;
        char *str;
        ENTRY;

        str = kmem_alloc(size, KM_SLEEP);
        if (str == NULL)
                RETURN(-ENOMEM);

        if (write) {
                rc = proc_copyin_string(str, size, buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                rc = spl_debug_str2mask(mask, str, is_subsys);
                /* Always print BUG/ASSERT to console, so keep this mask */
                if (is_printk)
                        *mask |= D_EMERG;

                *ppos += *lenp;
        } else {
                rc = spl_debug_mask2str(str, size, *mask, is_subsys);
                if (*ppos >= rc)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer, *lenp,
                                                 str + *ppos, "\n");
                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        kmem_free(str, size);
        RETURN(rc);
}

static int
proc_debug_mb(struct ctl_table *table, int write, struct file *filp,
              void __user *buffer, size_t *lenp, loff_t *ppos)
{
        char str[32];
        int rc, len;
        ENTRY;

        if (write) {
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                rc = spl_debug_set_mb(simple_strtoul(str, NULL, 0));
                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%d", spl_debug_get_mb());
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer, *lenp, str + *ppos, "\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        RETURN(rc);
}

static int
proc_dump_kernel(struct ctl_table *table, int write, struct file *filp,
                 void __user *buffer, size_t *lenp, loff_t *ppos)
{
	ENTRY;

        if (write) {
               spl_debug_dumplog(0);
                *ppos += *lenp;
        } else {
                *lenp = 0;
        }

        RETURN(0);
}

static int
proc_force_bug(struct ctl_table *table, int write, struct file *filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
{
	ENTRY;

        if (write) {
               CERROR("Crashing due to forced SBUG\n");
               SBUG();
	       /* Unreachable */
        } else {
                *lenp = 0;
	}

	RETURN(0);
}

static int
proc_console_max_delay_cs(struct ctl_table *table, int write, struct file *filp,
                          void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc, max_delay_cs;
        struct ctl_table dummy = *table;
        long d;
	ENTRY;

        dummy.data = &max_delay_cs;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                max_delay_cs = 0;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
                if (rc < 0)
                        RETURN(rc);

                if (max_delay_cs <= 0)
                        RETURN(-EINVAL);

                d = (max_delay_cs * HZ) / 100;
                if (d == 0 || d < spl_console_min_delay)
                        RETURN(-EINVAL);

                spl_console_max_delay = d;
        } else {
                max_delay_cs = (spl_console_max_delay * 100) / HZ;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
        }

        RETURN(rc);
}

static int
proc_console_min_delay_cs(struct ctl_table *table, int write, struct file *filp,
                          void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc, min_delay_cs;
        struct ctl_table dummy = *table;
        long d;
	ENTRY;

        dummy.data = &min_delay_cs;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                min_delay_cs = 0;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
                if (rc < 0)
                        RETURN(rc);

                if (min_delay_cs <= 0)
                        RETURN(-EINVAL);

                d = (min_delay_cs * HZ) / 100;
                if (d == 0 || d > spl_console_max_delay)
                        RETURN(-EINVAL);

                spl_console_min_delay = d;
        } else {
                min_delay_cs = (spl_console_min_delay * 100) / HZ;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
        }

        RETURN(rc);
}

static int
proc_console_backoff(struct ctl_table *table, int write, struct file *filp,
                     void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc, backoff;
        struct ctl_table dummy = *table;
	ENTRY;

        dummy.data = &backoff;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                backoff = 0;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
                if (rc < 0)
                        RETURN(rc);

                if (backoff <= 0)
                        RETURN(-EINVAL);

                spl_console_backoff = backoff;
        } else {
                backoff = spl_console_backoff;
                rc = proc_dointvec(&dummy, write, filp, buffer, lenp, ppos);
        }

        RETURN(rc);
}

#ifdef DEBUG_KMEM
static int
proc_doatomic64(struct ctl_table *table, int write, struct file *filp,
                void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int rc = 0;
        unsigned long min = 0, max = ~0, val;
        struct ctl_table dummy = *table;
	ENTRY;

        dummy.data = &val;
        dummy.proc_handler = &proc_dointvec;
        dummy.extra1 = &min;
        dummy.extra2 = &max;

        if (write) {
                *ppos += *lenp;
        } else {
                val = atomic64_read((atomic64_t *)table->data);
                rc = proc_doulongvec_minmax(&dummy, write, filp,
                                            buffer, lenp, ppos);
        }

        RETURN(rc);
}
#endif /* DEBUG_KMEM */

static int
proc_dohostid(struct ctl_table *table, int write, struct file *filp,
              void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int len, rc = 0;
        int32_t val;
        char *end, str[32];
        ENTRY;

        if (write) {
                /* We can't use proc_doulongvec_minmax() in the write
                 * case hear because hostid while a hex value has no
                 * leading 0x which confuses the helper function. */
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                val = simple_strtol(str, &end, 16);
                if (str == end)
                        RETURN(-EINVAL);

                spl_hostid = (long) val;
                (void) snprintf(hw_serial, HW_HOSTID_LEN, "%u",
                               (val >= 0) ? val : -val);
                hw_serial[HW_HOSTID_LEN - 1] = '\0';
                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%lx", spl_hostid);
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer, *lenp, str + *ppos, "\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        RETURN(rc);
}

#ifndef HAVE_KALLSYMS_LOOKUP_NAME
static int
proc_dokallsyms_lookup_name(struct ctl_table *table, int write,
			    struct file *filp, void __user *buffer,
			    size_t *lenp, loff_t *ppos) {
        int len, rc = 0;
        char *end, str[32];
	ENTRY;

        if (write) {
		/* This may only be set once at module load time */
		if (spl_kallsyms_lookup_name_fn)
			RETURN(-EEXIST);

		/* We can't use proc_doulongvec_minmax() in the write
		 * case hear because the address while a hex value has no
		 * leading 0x which confuses the helper function. */
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        RETURN(rc);

                spl_kallsyms_lookup_name_fn =
			(kallsyms_lookup_name_t)simple_strtoul(str, &end, 16);
		if (str == end)
			RETURN(-EINVAL);

                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%lx",
			       (unsigned long)spl_kallsyms_lookup_name_fn);
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer,*lenp,str+*ppos,"\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        RETURN(rc);
}
#endif /* HAVE_KALLSYMS_LOOKUP_NAME */

static int
proc_doavailrmem(struct ctl_table *table, int write, struct file *filp,
                 void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int len, rc = 0;
	char str[32];
	ENTRY;

        if (write) {
                *ppos += *lenp;
        } else {
		len = snprintf(str, sizeof(str), "%lu", (unsigned long)availrmem);
		if (*ppos >= len)
			rc = 0;
		else
			rc = proc_copyout_string(buffer, *lenp, str + *ppos, "\n");

		if (rc >= 0) {
			*lenp = rc;
			*ppos += rc;
		}
        }

        RETURN(rc);
}

static int
proc_dofreemem(struct ctl_table *table, int write, struct file *filp,
               void __user *buffer, size_t *lenp, loff_t *ppos)
{
        int len, rc = 0;
	char str[32];
	ENTRY;

        if (write) {
                *ppos += *lenp;
        } else {
		len = snprintf(str, sizeof(str), "%lu", (unsigned long)freemem);
		if (*ppos >= len)
			rc = 0;
		else
			rc = proc_copyout_string(buffer, *lenp, str + *ppos, "\n");

		if (rc >= 0) {
			*lenp = rc;
			*ppos += rc;
		}
        }

        RETURN(rc);
}

#ifdef DEBUG_MUTEX
static void
mutex_seq_show_headers(struct seq_file *f)
{
        seq_printf(f, "%-36s %-4s %-16s\t"
                   "e_tot\te_nh\te_sp\te_sl\tte_tot\tte_nh\n",
		   "name", "type", "owner");
}

static int
mutex_seq_show(struct seq_file *f, void *p)
{
        kmutex_t *mp = p;
	char t = 'X';
        int i;

	ASSERT(mp->km_magic == KM_MAGIC);

	switch (mp->km_type) {
		case MUTEX_DEFAULT:	t = 'D';	break;
		case MUTEX_SPIN:	t = 'S';	break;
		case MUTEX_ADAPTIVE:	t = 'A';	break;
		default:
			SBUG();
	}
        seq_printf(f, "%-36s %c    ", mp->km_name, t);
	if (mp->km_owner)
                seq_printf(f, "%p\t", mp->km_owner);
	else
                seq_printf(f, "%-16s\t", "<not held>");

        for (i = 0; i < MUTEX_STATS_SIZE; i++)
                seq_printf(f, "%d%c", mp->km_stats[i],
                           (i + 1 == MUTEX_STATS_SIZE) ? '\n' : '\t');

        return 0;
}

static void *
mutex_seq_start(struct seq_file *f, loff_t *pos)
{
        struct list_head *p;
        loff_t n = *pos;
        ENTRY;

	spin_lock(&mutex_stats_lock);
        if (!n)
                mutex_seq_show_headers(f);

        p = mutex_stats_list.next;
        while (n--) {
                p = p->next;
                if (p == &mutex_stats_list)
                        RETURN(NULL);
        }

        RETURN(list_entry(p, kmutex_t, km_list));
}

static void *
mutex_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	kmutex_t *mp = p;
        ENTRY;

        ++*pos;
        RETURN((mp->km_list.next == &mutex_stats_list) ?
	       NULL : list_entry(mp->km_list.next, kmutex_t, km_list));
}

static void
mutex_seq_stop(struct seq_file *f, void *v)
{
	spin_unlock(&mutex_stats_lock);
}

static struct seq_operations mutex_seq_ops = {
        .show  = mutex_seq_show,
        .start = mutex_seq_start,
        .next  = mutex_seq_next,
        .stop  = mutex_seq_stop,
};

static int
proc_mutex_open(struct inode *inode, struct file *filp)
{
        return seq_open(filp, &mutex_seq_ops);
}

static struct file_operations proc_mutex_operations = {
        .open           = proc_mutex_open,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = seq_release,
};
#endif /* DEBUG_MUTEX */

#ifdef DEBUG_KMEM
static void
slab_seq_show_headers(struct seq_file *f)
{
        seq_printf(f, "%-36s\n", "name");
}

static int
slab_seq_show(struct seq_file *f, void *p)
{
	spl_kmem_cache_t *skc = p;

	ASSERT(skc->skc_magic == SKC_MAGIC);

	spin_lock(&skc->skc_lock);
        seq_printf(f, "%-36s      ", skc->skc_name);
        seq_printf(f, "%u %u %u - %lu %lu %lu - %lu %lu %lu - %lu %lu %lu\n",
		   (unsigned)skc->skc_obj_size,
		   (unsigned)skc->skc_slab_objs,
		   (unsigned)skc->skc_slab_size,
		   (long unsigned)skc->skc_slab_fail,
		   (long unsigned)skc->skc_slab_create,
		   (long unsigned)skc->skc_slab_destroy,
		   (long unsigned)skc->skc_slab_total,
		   (long unsigned)skc->skc_slab_alloc,
		   (long unsigned)skc->skc_slab_max,
		   (long unsigned)skc->skc_obj_total,
		   (long unsigned)skc->skc_obj_alloc,
		   (long unsigned)skc->skc_obj_max);

	spin_unlock(&skc->skc_lock);

        return 0;
}

static void *
slab_seq_start(struct seq_file *f, loff_t *pos)
{
        struct list_head *p;
        loff_t n = *pos;
        ENTRY;

	down_read(&spl_kmem_cache_sem);
        if (!n)
                slab_seq_show_headers(f);

        p = spl_kmem_cache_list.next;
        while (n--) {
                p = p->next;
                if (p == &spl_kmem_cache_list)
                        RETURN(NULL);
        }

        RETURN(list_entry(p, spl_kmem_cache_t, skc_list));
}

static void *
slab_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	spl_kmem_cache_t *skc = p;
        ENTRY;

        ++*pos;
        RETURN((skc->skc_list.next == &spl_kmem_cache_list) ?
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
        return seq_open(filp, &slab_seq_ops);
}

static struct file_operations proc_slab_operations = {
        .open           = proc_slab_open,
        .read           = seq_read,
        .llseek         = seq_lseek,
        .release        = seq_release,
};
#endif /* DEBUG_KMEM */

static struct ctl_table spl_debug_table[] = {
        {
                .ctl_name = CTL_DEBUG_SUBSYS,
                .procname = "subsystem",
                .data     = &spl_debug_subsys,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                .ctl_name = CTL_DEBUG_MASK,
                .procname = "mask",
                .data     = &spl_debug_mask,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                .ctl_name = CTL_DEBUG_PRINTK,
                .procname = "printk",
                .data     = &spl_debug_printk,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                .ctl_name = CTL_DEBUG_MB,
                .procname = "mb",
                .mode     = 0644,
                .proc_handler = &proc_debug_mb,
        },
        {
                .ctl_name = CTL_DEBUG_BINARY,
                .procname = "binary",
                .data     = &spl_debug_binary,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_DEBUG_CATASTROPHE,
                .procname = "catastrophe",
                .data     = &spl_debug_catastrophe,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_DEBUG_PANIC_ON_BUG,
                .procname = "panic_on_bug",
                .data     = &spl_debug_panic_on_bug,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                .ctl_name = CTL_DEBUG_PATH,
                .procname = "path",
                .data     = spl_debug_file_path,
                .maxlen   = sizeof(spl_debug_file_path),
                .mode     = 0644,
                .proc_handler = &proc_dostring,
        },
        {
                .ctl_name = CTL_DEBUG_DUMP,
                .procname = "dump",
                .mode     = 0200,
                .proc_handler = &proc_dump_kernel,
        },
        {       .ctl_name = CTL_DEBUG_FORCE_BUG,
                .procname = "force_bug",
                .mode     = 0200,
                .proc_handler = &proc_force_bug,
        },
        {
                .ctl_name = CTL_CONSOLE_RATELIMIT,
                .procname = "console_ratelimit",
                .data     = &spl_console_ratelimit,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_CONSOLE_MAX_DELAY_CS,
                .procname = "console_max_delay_centisecs",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_max_delay_cs,
        },
        {
                .ctl_name = CTL_CONSOLE_MIN_DELAY_CS,
                .procname = "console_min_delay_centisecs",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_min_delay_cs,
        },
        {
                .ctl_name = CTL_CONSOLE_BACKOFF,
                .procname = "console_backoff",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_backoff,
        },
        {
                .ctl_name = CTL_DEBUG_STACK_SIZE,
                .procname = "stack_max",
                .data     = &spl_debug_stack,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
	{0},
};

static struct ctl_table spl_vm_table[] = {
        {
                .ctl_name = CTL_VM_MINFREE,
                .procname = "minfree",
                .data     = &minfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_VM_DESFREE,
                .procname = "desfree",
                .data     = &desfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_VM_LOTSFREE,
                .procname = "lotsfree",
                .data     = &lotsfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_VM_NEEDFREE,
                .procname = "needfree",
                .data     = &needfree,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_VM_SWAPFS_MINFREE,
                .procname = "swapfs_minfree",
                .data     = &swapfs_minfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_VM_SWAPFS_RESERVE,
                .procname = "swapfs_reserve",
                .data     = &swapfs_reserve,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_VM_AVAILRMEM,
                .procname = "availrmem",
                .mode     = 0444,
                .proc_handler = &proc_doavailrmem,
        },
        {
                .ctl_name = CTL_VM_FREEMEM,
                .procname = "freemem",
                .data     = (void *)2,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dofreemem,
        },
        {
                .ctl_name = CTL_VM_PHYSMEM,
                .procname = "physmem",
                .data     = &physmem,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
	{0},
};

#ifdef DEBUG_MUTEX
static struct ctl_table spl_mutex_table[] = {
        {
                .ctl_name = CTL_MUTEX_STATS,
                .procname = "stats",
                .data     = &mutex_stats,
                .maxlen   = sizeof(int) * MUTEX_STATS_SIZE,
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
        {
                .ctl_name = CTL_MUTEX_SPIN_MAX,
                .procname = "spin_max",
                .data     = &mutex_spin_max,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
	{0},
};
#endif /* DEBUG_MUTEX */

#ifdef DEBUG_KMEM
static struct ctl_table spl_kmem_table[] = {
        {
                .ctl_name = CTL_KMEM_KMEMUSED,
                .procname = "kmem_used",
                .data     = &kmem_alloc_used,
                .maxlen   = sizeof(atomic64_t),
                .mode     = 0444,
                .proc_handler = &proc_doatomic64,
        },
        {
                .ctl_name = CTL_KMEM_KMEMMAX,
                .procname = "kmem_max",
                .data     = &kmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
        {
                .ctl_name = CTL_KMEM_VMEMUSED,
                .procname = "vmem_used",
                .data     = &vmem_alloc_used,
                .maxlen   = sizeof(atomic64_t),
                .mode     = 0444,
                .proc_handler = &proc_doatomic64,
        },
        {
                .ctl_name = CTL_KMEM_VMEMMAX,
                .procname = "vmem_max",
                .data     = &vmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
	{0},
};
#endif /* DEBUG_KMEM */

#ifdef DEBUG_KSTAT
static struct ctl_table spl_kstat_table[] = {
	{0},
};
#endif /* DEBUG_KSTAT */

static struct ctl_table spl_table[] = {
        /* NB No .strategy entries have been provided since
         * sysctl(8) prefers to go via /proc for portability.
         */
        {
                .ctl_name = CTL_VERSION,
                .procname = "version",
                .data     = spl_version,
                .maxlen   = sizeof(spl_version),
                .mode     = 0444,
                .proc_handler = &proc_dostring,
        },
        {
                .ctl_name = CTL_HOSTID,
                .procname = "hostid",
                .data     = &spl_hostid,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dohostid,
        },
        {
                .ctl_name = CTL_HW_SERIAL,
                .procname = "hw_serial",
                .data     = hw_serial,
                .maxlen   = sizeof(hw_serial),
                .mode     = 0444,
                .proc_handler = &proc_dostring,
        },
#ifndef HAVE_KALLSYMS_LOOKUP_NAME
        {
                .ctl_name = CTL_KALLSYMS,
                .procname = "kallsyms_lookup_name",
                .data     = &spl_kallsyms_lookup_name_fn,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dokallsyms_lookup_name,
        },
#endif
	{
		.ctl_name = CTL_SPL_DEBUG,
		.procname = "debug",
		.mode     = 0555,
		.child    = spl_debug_table,
	},
	{
		.ctl_name = CTL_SPL_VM,
		.procname = "vm",
		.mode     = 0555,
		.child    = spl_vm_table,
	},
#ifdef DEBUG_MUTEX
	{
		.ctl_name = CTL_SPL_MUTEX,
		.procname = "mutex",
		.mode     = 0555,
		.child    = spl_mutex_table,
	},
#endif
#ifdef DEBUG_KMEM
	{
		.ctl_name = CTL_SPL_KMEM,
		.procname = "kmem",
		.mode     = 0555,
		.child    = spl_kmem_table,
	},
#endif
#ifdef DEBUG_KSTAT
	{
		.ctl_name = CTL_SPL_KSTAT,
		.procname = "kstat",
		.mode     = 0555,
		.child    = spl_kstat_table,
	},
#endif
        { 0 },
};

static struct ctl_table spl_dir[] = {
        {
                .ctl_name = CTL_SPL,
                .procname = "spl",
                .mode     = 0555,
                .child    = spl_table,
        },
        { 0 }
};

static struct ctl_table spl_root[] = {
	{
	.ctl_name = CTL_KERN,
	.procname = "kernel",
	.mode = 0555,
	.child = spl_dir,
	},
	{ 0 }
};

static int
proc_dir_entry_match(int len, const char *name, struct proc_dir_entry *de)
{
        if (de->namelen != len)
                return 0;

        return !memcmp(name, de->name, len);
}

struct proc_dir_entry *
proc_dir_entry_find(struct proc_dir_entry *root, const char *str)
{
	struct proc_dir_entry *de;

	for (de = root->subdir; de; de = de->next)
		if (proc_dir_entry_match(strlen(str), str, de))
			return de;

	return NULL;
}

int
proc_dir_entries(struct proc_dir_entry *root)
{
	struct proc_dir_entry *de;
	int i = 0;

	for (de = root->subdir; de; de = de->next)
		i++;

	return i;
}

int
proc_init(void)
{
	int rc = 0;
        ENTRY;

#ifdef CONFIG_SYSCTL
        spl_header = spl_register_sysctl_table(spl_root, 0);
	if (spl_header == NULL)
		RETURN(-EUNATCH);
#endif /* CONFIG_SYSCTL */

#if defined(DEBUG_MUTEX) || defined(DEBUG_KMEM) || defined(DEBUG_KSTAT)
	proc_spl = proc_mkdir("spl", NULL);
	if (proc_spl == NULL)
		GOTO(out, rc = -EUNATCH);

#ifdef DEBUG_MUTEX
	proc_spl_mutex = proc_mkdir("mutex", proc_spl);
	if (proc_spl_mutex == NULL)
		GOTO(out, rc = -EUNATCH);

	proc_spl_mutex_stats = create_proc_entry("stats_per", 0444,
						 proc_spl_mutex);
        if (proc_spl_mutex_stats == NULL)
		GOTO(out, rc = -EUNATCH);

        proc_spl_mutex_stats->proc_fops = &proc_mutex_operations;
#endif /* DEBUG_MUTEX */

#ifdef DEBUG_KMEM
        proc_spl_kmem = proc_mkdir("kmem", proc_spl);
        if (proc_spl_kmem == NULL)
                GOTO(out, rc = -EUNATCH);

	proc_spl_kmem_slab = create_proc_entry("slab", 0444, proc_spl_kmem);
        if (proc_spl_kmem_slab == NULL)
		GOTO(out, rc = -EUNATCH);

        proc_spl_kmem_slab->proc_fops = &proc_slab_operations;
#endif /* DEBUG_KMEM */

#ifdef DEBUG_KSTAT
        proc_spl_kstat = proc_mkdir("kstat", proc_spl);
        if (proc_spl_kstat == NULL)
                GOTO(out, rc = -EUNATCH);
#endif /* DEBUG_KSTAT */

out:
	if (rc) {
		remove_proc_entry("kstat", proc_spl);
#ifdef DEBUG_KMEM
	        remove_proc_entry("slab", proc_spl_kmem);
#endif
		remove_proc_entry("kmem", proc_spl);
#ifdef DEBUG_MUTEX
	        remove_proc_entry("stats_per", proc_spl_mutex);
#endif
		remove_proc_entry("mutex", proc_spl);
		remove_proc_entry("spl", NULL);
#ifdef CONFIG_SYSCTL
	        spl_unregister_sysctl_table(spl_header);
#endif /* CONFIG_SYSCTL */
	}
#endif /* DEBUG_MUTEX || DEBUG_KMEM || DEBUG_KSTAT */

        RETURN(rc);
}

void
proc_fini(void)
{
        ENTRY;

#if defined(DEBUG_MUTEX) || defined(DEBUG_KMEM) || defined(DEBUG_KSTAT)
	remove_proc_entry("kstat", proc_spl);
#ifdef DEBUG_KMEM
        remove_proc_entry("slab", proc_spl_kmem);
#endif
	remove_proc_entry("kmem", proc_spl);
#ifdef DEBUG_MUTEX
        remove_proc_entry("stats_per", proc_spl_mutex);
#endif
	remove_proc_entry("mutex", proc_spl);
	remove_proc_entry("spl", NULL);
#endif /* DEBUG_MUTEX || DEBUG_KMEM || DEBUG_KSTAT */

#ifdef CONFIG_SYSCTL
        ASSERT(spl_header != NULL);
        spl_unregister_sysctl_table(spl_header);
#endif /* CONFIG_SYSCTL */

        EXIT;
}
