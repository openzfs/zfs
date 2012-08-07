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
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Proc Implementation.
\*****************************************************************************/

#include <sys/systeminfo.h>
#include <sys/kstat.h>
#include <linux/kmod.h>
#include <linux/seq_file.h>
#include <linux/proc_compat.h>
#include <spl-debug.h>

#ifdef SS_DEBUG_SUBSYS
#undef SS_DEBUG_SUBSYS
#endif

#define SS_DEBUG_SUBSYS SS_PROC

#ifdef DEBUG_KMEM
static unsigned long table_min = 0;
static unsigned long table_max = ~0;
#endif

#ifdef CONFIG_SYSCTL
static struct ctl_table_header *spl_header = NULL;
#endif /* CONFIG_SYSCTL */

static struct proc_dir_entry *proc_spl = NULL;
#ifdef DEBUG_KMEM
static struct proc_dir_entry *proc_spl_kmem = NULL;
static struct proc_dir_entry *proc_spl_kmem_slab = NULL;
#endif /* DEBUG_KMEM */
struct proc_dir_entry *proc_spl_kstat = NULL;

#ifdef HAVE_CTL_NAME
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
#define CTL_KMEM_SLAB_KMEMTOTAL	CTL_UNNUMBERED /* Total kmem slab size */
#define CTL_KMEM_SLAB_KMEMALLOC	CTL_UNNUMBERED /* Alloc'd kmem slab size */
#define CTL_KMEM_SLAB_KMEMMAX	CTL_UNNUMBERED /* Max kmem slab size */
#define CTL_KMEM_SLAB_VMEMTOTAL	CTL_UNNUMBERED /* Total vmem slab size */
#define CTL_KMEM_SLAB_VMEMALLOC	CTL_UNNUMBERED /* Alloc'd vmem slab size */
#define CTL_KMEM_SLAB_VMEMMAX	CTL_UNNUMBERED /* Max vmem slab size */
#endif

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

#ifdef DEBUG_LOG
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
#endif

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
	CTL_KMEM_SLAB_KMEMTOTAL,	/* Total kmem slab size */
	CTL_KMEM_SLAB_KMEMALLOC,	/* Alloc'd kmem slab size */
	CTL_KMEM_SLAB_KMEMMAX,		/* Max kmem slab size */
	CTL_KMEM_SLAB_VMEMTOTAL,	/* Total vmem slab size */
	CTL_KMEM_SLAB_VMEMALLOC,	/* Alloc'd vmem slab size */
	CTL_KMEM_SLAB_VMEMMAX,		/* Max vmem slab size */
#endif
};
#endif /* HAVE_CTL_UNNUMBERED */
#endif /* HAVE_CTL_NAME */

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

#ifdef DEBUG_LOG
SPL_PROC_HANDLER(proc_dobitmasks)
{
        unsigned long *mask = table->data;
        int is_subsys = (mask == &spl_debug_subsys) ? 1 : 0;
        int is_printk = (mask == &spl_debug_printk) ? 1 : 0;
        int size = 512, rc;
        char *str;
        SENTRY;

        str = kmem_alloc(size, KM_SLEEP);
        if (str == NULL)
                SRETURN(-ENOMEM);

        if (write) {
                rc = proc_copyin_string(str, size, buffer, *lenp);
                if (rc < 0)
                        SRETURN(rc);

                rc = spl_debug_str2mask(mask, str, is_subsys);
                /* Always print BUG/ASSERT to console, so keep this mask */
                if (is_printk)
                        *mask |= SD_EMERG;

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
        SRETURN(rc);
}

SPL_PROC_HANDLER(proc_debug_mb)
{
        char str[32];
        int rc, len;
        SENTRY;

        if (write) {
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        SRETURN(rc);

                rc = spl_debug_set_mb(simple_strtoul(str, NULL, 0));
                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%d", spl_debug_get_mb());
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer,*lenp,str+*ppos,"\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        SRETURN(rc);
}

SPL_PROC_HANDLER(proc_dump_kernel)
{
	SENTRY;

        if (write) {
               spl_debug_dumplog(0);
                *ppos += *lenp;
        } else {
                *lenp = 0;
        }

        SRETURN(0);
}

SPL_PROC_HANDLER(proc_force_bug)
{
	SENTRY;

        if (write)
		PANIC("Crashing due to forced panic\n");
        else
                *lenp = 0;

	SRETURN(0);
}

SPL_PROC_HANDLER(proc_console_max_delay_cs)
{
        int rc, max_delay_cs;
        struct ctl_table dummy = *table;
        long d;
	SENTRY;

        dummy.data = &max_delay_cs;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                max_delay_cs = 0;
                rc = spl_proc_dointvec(&dummy,write,filp,buffer,lenp,ppos);
                if (rc < 0)
                        SRETURN(rc);

                if (max_delay_cs <= 0)
                        SRETURN(-EINVAL);

                d = (max_delay_cs * HZ) / 100;
                if (d == 0 || d < spl_console_min_delay)
                        SRETURN(-EINVAL);

                spl_console_max_delay = d;
        } else {
                max_delay_cs = (spl_console_max_delay * 100) / HZ;
                rc = spl_proc_dointvec(&dummy,write,filp,buffer,lenp,ppos);
        }

        SRETURN(rc);
}

SPL_PROC_HANDLER(proc_console_min_delay_cs)
{
        int rc, min_delay_cs;
        struct ctl_table dummy = *table;
        long d;
	SENTRY;

        dummy.data = &min_delay_cs;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                min_delay_cs = 0;
                rc = spl_proc_dointvec(&dummy,write,filp,buffer,lenp,ppos);
                if (rc < 0)
                        SRETURN(rc);

                if (min_delay_cs <= 0)
                        SRETURN(-EINVAL);

                d = (min_delay_cs * HZ) / 100;
                if (d == 0 || d > spl_console_max_delay)
                        SRETURN(-EINVAL);

                spl_console_min_delay = d;
        } else {
                min_delay_cs = (spl_console_min_delay * 100) / HZ;
                rc = spl_proc_dointvec(&dummy,write,filp,buffer,lenp,ppos);
        }

        SRETURN(rc);
}

SPL_PROC_HANDLER(proc_console_backoff)
{
        int rc, backoff;
        struct ctl_table dummy = *table;
	SENTRY;

        dummy.data = &backoff;
        dummy.proc_handler = &proc_dointvec;

        if (write) {
                backoff = 0;
                rc = spl_proc_dointvec(&dummy,write,filp,buffer,lenp,ppos);
                if (rc < 0)
                        SRETURN(rc);

                if (backoff <= 0)
                        SRETURN(-EINVAL);

                spl_console_backoff = backoff;
        } else {
                backoff = spl_console_backoff;
                rc = spl_proc_dointvec(&dummy,write,filp,buffer,lenp,ppos);
        }

        SRETURN(rc);
}
#endif /* DEBUG_LOG */

#ifdef DEBUG_KMEM
SPL_PROC_HANDLER(proc_domemused)
{
        int rc = 0;
        unsigned long min = 0, max = ~0, val;
        struct ctl_table dummy = *table;
	SENTRY;

        dummy.data = &val;
        dummy.proc_handler = &proc_dointvec;
        dummy.extra1 = &min;
        dummy.extra2 = &max;

        if (write) {
                *ppos += *lenp;
        } else {
# ifdef HAVE_ATOMIC64_T
                val = atomic64_read((atomic64_t *)table->data);
# else
                val = atomic_read((atomic_t *)table->data);
# endif /* HAVE_ATOMIC64_T */
                rc = spl_proc_doulongvec_minmax(&dummy, write, filp,
                                                buffer, lenp, ppos);
        }

        SRETURN(rc);
}

SPL_PROC_HANDLER(proc_doslab)
{
        int rc = 0;
        unsigned long min = 0, max = ~0, val = 0, mask;
        struct ctl_table dummy = *table;
        spl_kmem_cache_t *skc;
        SENTRY;

        dummy.data = &val;
        dummy.proc_handler = &proc_dointvec;
        dummy.extra1 = &min;
        dummy.extra2 = &max;

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
                rc = spl_proc_doulongvec_minmax(&dummy, write, filp,
                                                buffer, lenp, ppos);
        }

        SRETURN(rc);
}
#endif /* DEBUG_KMEM */

SPL_PROC_HANDLER(proc_dohostid)
{
        int len, rc = 0;
        char *end, str[32];
        SENTRY;

        if (write) {
                /* We can't use spl_proc_doulongvec_minmax() in the write
                 * case here because hostid while a hex value has no
                 * leading 0x which confuses the helper function. */
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        SRETURN(rc);

                spl_hostid = simple_strtoul(str, &end, 16);
                if (str == end)
                        SRETURN(-EINVAL);

                (void) snprintf(hw_serial, HW_HOSTID_LEN, "%lu", spl_hostid);
                hw_serial[HW_HOSTID_LEN - 1] = '\0';
                *ppos += *lenp;
        } else {
                len = snprintf(str, sizeof(str), "%lx", spl_hostid);
                if (*ppos >= len)
                        rc = 0;
                else
                        rc = proc_copyout_string(buffer,*lenp,str+*ppos,"\n");

                if (rc >= 0) {
                        *lenp = rc;
                        *ppos += rc;
                }
        }

        SRETURN(rc);
}

#ifndef HAVE_KALLSYMS_LOOKUP_NAME
SPL_PROC_HANDLER(proc_dokallsyms_lookup_name)
{
        int len, rc = 0;
        char *end, str[32];
	SENTRY;

        if (write) {
		/* This may only be set once at module load time */
		if (spl_kallsyms_lookup_name_fn != SYMBOL_POISON)
			SRETURN(-EEXIST);

		/* We can't use spl_proc_doulongvec_minmax() in the write
		 * case here because the address while a hex value has no
		 * leading 0x which confuses the helper function. */
                rc = proc_copyin_string(str, sizeof(str), buffer, *lenp);
                if (rc < 0)
                        SRETURN(rc);

                spl_kallsyms_lookup_name_fn =
			(kallsyms_lookup_name_t)simple_strtoul(str, &end, 16);
		if (str == end)
			SRETURN(-EINVAL);

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

        SRETURN(rc);
}
#endif /* HAVE_KALLSYMS_LOOKUP_NAME */

SPL_PROC_HANDLER(proc_doavailrmem)
{
        int len, rc = 0;
	char str[32];
	SENTRY;

        if (write) {
                *ppos += *lenp;
        } else {
		len = snprintf(str, sizeof(str), "%lu",
			       (unsigned long)availrmem);
		if (*ppos >= len)
			rc = 0;
		else
			rc = proc_copyout_string(buffer,*lenp,str+*ppos,"\n");

		if (rc >= 0) {
			*lenp = rc;
			*ppos += rc;
		}
        }

        SRETURN(rc);
}

SPL_PROC_HANDLER(proc_dofreemem)
{
        int len, rc = 0;
	char str[32];
	SENTRY;

        if (write) {
                *ppos += *lenp;
        } else {
		len = snprintf(str, sizeof(str), "%lu", (unsigned long)freemem);
		if (*ppos >= len)
			rc = 0;
		else
			rc = proc_copyout_string(buffer,*lenp,str+*ppos,"\n");

		if (rc >= 0) {
			*lenp = rc;
			*ppos += rc;
		}
        }

        SRETURN(rc);
}

#ifdef DEBUG_KMEM
static void
slab_seq_show_headers(struct seq_file *f)
{
        seq_printf(f,
            "--------------------- cache ----------"
            "---------------------------------------------  "
            "----- slab ------  "
            "---- object -----------------\n");
        seq_printf(f,
            "name                                  "
            "  flags      size     alloc slabsize  objsize  "
            "total alloc   max  "
            "total alloc   max emerg   max\n");
}

static int
slab_seq_show(struct seq_file *f, void *p)
{
        spl_kmem_cache_t *skc = p;

        ASSERT(skc->skc_magic == SKC_MAGIC);

        spin_lock(&skc->skc_lock);
        seq_printf(f, "%-36s  ", skc->skc_name);
        seq_printf(f, "0x%05lx %9lu %9lu %8u %8u  "
            "%5lu %5lu %5lu  %5lu %5lu %5lu %5lu %5lu\n",
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
            (long unsigned)skc->skc_obj_emergency,
            (long unsigned)skc->skc_obj_emergency_max);

        spin_unlock(&skc->skc_lock);

        return 0;
}

static void *
slab_seq_start(struct seq_file *f, loff_t *pos)
{
        struct list_head *p;
        loff_t n = *pos;
        SENTRY;

	down_read(&spl_kmem_cache_sem);
        if (!n)
                slab_seq_show_headers(f);

        p = spl_kmem_cache_list.next;
        while (n--) {
                p = p->next;
                if (p == &spl_kmem_cache_list)
                        SRETURN(NULL);
        }

        SRETURN(list_entry(p, spl_kmem_cache_t, skc_list));
}

static void *
slab_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	spl_kmem_cache_t *skc = p;
        SENTRY;

        ++*pos;
        SRETURN((skc->skc_list.next == &spl_kmem_cache_list) ?
	       NULL : list_entry(skc->skc_list.next,spl_kmem_cache_t,skc_list));
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

#ifdef DEBUG_LOG
static struct ctl_table spl_debug_table[] = {
        {
                CTL_NAME    (CTL_DEBUG_SUBSYS)
                .procname = "subsystem",
                .data     = &spl_debug_subsys,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                CTL_NAME    (CTL_DEBUG_MASK)
                .procname = "mask",
                .data     = &spl_debug_mask,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                CTL_NAME    (CTL_DEBUG_PRINTK)
                .procname = "printk",
                .data     = &spl_debug_printk,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dobitmasks
        },
        {
                CTL_NAME    (CTL_DEBUG_MB)
                .procname = "mb",
                .mode     = 0644,
                .proc_handler = &proc_debug_mb,
        },
        {
                CTL_NAME    (CTL_DEBUG_BINARY)
                .procname = "binary",
                .data     = &spl_debug_binary,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_DEBUG_CATASTROPHE)
                .procname = "catastrophe",
                .data     = &spl_debug_catastrophe,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_DEBUG_PANIC_ON_BUG)
                .procname = "panic_on_bug",
                .data     = &spl_debug_panic_on_bug,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec
        },
        {
                CTL_NAME    (CTL_DEBUG_PATH)
                .procname = "path",
                .data     = spl_debug_file_path,
                .maxlen   = sizeof(spl_debug_file_path),
                .mode     = 0644,
                .proc_handler = &proc_dostring,
        },
        {
                CTL_NAME    (CTL_DEBUG_DUMP)
                .procname = "dump",
                .mode     = 0200,
                .proc_handler = &proc_dump_kernel,
        },
        {       CTL_NAME    (CTL_DEBUG_FORCE_BUG)
                .procname = "force_bug",
                .mode     = 0200,
                .proc_handler = &proc_force_bug,
        },
        {
                CTL_NAME    (CTL_CONSOLE_RATELIMIT)
                .procname = "console_ratelimit",
                .data     = &spl_console_ratelimit,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_CONSOLE_MAX_DELAY_CS)
                .procname = "console_max_delay_centisecs",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_max_delay_cs,
        },
        {
                CTL_NAME    (CTL_CONSOLE_MIN_DELAY_CS)
                .procname = "console_min_delay_centisecs",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_min_delay_cs,
        },
        {
                CTL_NAME    (CTL_CONSOLE_BACKOFF)
                .procname = "console_backoff",
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_console_backoff,
        },
        {
                CTL_NAME    (CTL_DEBUG_STACK_SIZE)
                .procname = "stack_max",
                .data     = &spl_debug_stack,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
	{0},
};
#endif /* DEBUG_LOG */

static struct ctl_table spl_vm_table[] = {
        {
                CTL_NAME    (CTL_VM_MINFREE)
                .procname = "minfree",
                .data     = &minfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_VM_DESFREE)
                .procname = "desfree",
                .data     = &desfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_VM_LOTSFREE)
                .procname = "lotsfree",
                .data     = &lotsfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_VM_NEEDFREE)
                .procname = "needfree",
                .data     = &needfree,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_VM_SWAPFS_MINFREE)
                .procname = "swapfs_minfree",
                .data     = &swapfs_minfree,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_VM_SWAPFS_RESERVE)
                .procname = "swapfs_reserve",
                .data     = &swapfs_reserve,
                .maxlen   = sizeof(int),
                .mode     = 0644,
                .proc_handler = &proc_dointvec,
        },
        {
                CTL_NAME    (CTL_VM_AVAILRMEM)
                .procname = "availrmem",
                .mode     = 0444,
                .proc_handler = &proc_doavailrmem,
        },
        {
                CTL_NAME    (CTL_VM_FREEMEM)
                .procname = "freemem",
                .data     = (void *)2,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dofreemem,
        },
        {
                CTL_NAME    (CTL_VM_PHYSMEM)
                .procname = "physmem",
                .data     = &physmem,
                .maxlen   = sizeof(int),
                .mode     = 0444,
                .proc_handler = &proc_dointvec,
        },
	{0},
};

#ifdef DEBUG_KMEM
static struct ctl_table spl_kmem_table[] = {
        {
                CTL_NAME    (CTL_KMEM_KMEMUSED)
                .procname = "kmem_used",
                .data     = &kmem_alloc_used,
# ifdef HAVE_ATOMIC64_T
                .maxlen   = sizeof(atomic64_t),
# else
                .maxlen   = sizeof(atomic_t),
# endif /* HAVE_ATOMIC64_T */
                .mode     = 0444,
                .proc_handler = &proc_domemused,
        },
        {
                CTL_NAME    (CTL_KMEM_KMEMMAX)
                .procname = "kmem_max",
                .data     = &kmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
        {
                CTL_NAME    (CTL_KMEM_VMEMUSED)
                .procname = "vmem_used",
                .data     = &vmem_alloc_used,
# ifdef HAVE_ATOMIC64_T
                .maxlen   = sizeof(atomic64_t),
# else
                .maxlen   = sizeof(atomic_t),
# endif /* HAVE_ATOMIC64_T */
                .mode     = 0444,
                .proc_handler = &proc_domemused,
        },
        {
                CTL_NAME    (CTL_KMEM_VMEMMAX)
                .procname = "vmem_max",
                .data     = &vmem_alloc_max,
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doulongvec_minmax,
        },
        {
                CTL_NAME    (CTL_KMEM_SLAB_KMEMTOTAL)
                .procname = "slab_kmem_total",
		.data     = (void *)(KMC_KMEM | KMC_TOTAL),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                CTL_NAME    (CTL_KMEM_SLAB_KMEMALLOC)
                .procname = "slab_kmem_alloc",
		.data     = (void *)(KMC_KMEM | KMC_ALLOC),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                CTL_NAME    (CTL_KMEM_SLAB_KMEMMAX)
                .procname = "slab_kmem_max",
		.data     = (void *)(KMC_KMEM | KMC_MAX),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                CTL_NAME    (CTL_KMEM_SLAB_VMEMTOTAL)
                .procname = "slab_vmem_total",
		.data     = (void *)(KMC_VMEM | KMC_TOTAL),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                CTL_NAME    (CTL_KMEM_SLAB_VMEMALLOC)
                .procname = "slab_vmem_alloc",
		.data     = (void *)(KMC_VMEM | KMC_ALLOC),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
        {
                CTL_NAME    (CTL_KMEM_SLAB_VMEMMAX)
                .procname = "slab_vmem_max",
		.data     = (void *)(KMC_VMEM | KMC_MAX),
                .maxlen   = sizeof(unsigned long),
                .extra1   = &table_min,
                .extra2   = &table_max,
                .mode     = 0444,
                .proc_handler = &proc_doslab,
        },
	{0},
};
#endif /* DEBUG_KMEM */

static struct ctl_table spl_kstat_table[] = {
	{0},
};

static struct ctl_table spl_table[] = {
        /* NB No .strategy entries have been provided since
         * sysctl(8) prefers to go via /proc for portability.
         */
        {
                CTL_NAME    (CTL_VERSION)
                .procname = "version",
                .data     = spl_version,
                .maxlen   = sizeof(spl_version),
                .mode     = 0444,
                .proc_handler = &proc_dostring,
        },
        {
                CTL_NAME    (CTL_HOSTID)
                .procname = "hostid",
                .data     = &spl_hostid,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dohostid,
        },
        {
                CTL_NAME    (CTL_HW_SERIAL)
                .procname = "hw_serial",
                .data     = hw_serial,
                .maxlen   = sizeof(hw_serial),
                .mode     = 0444,
                .proc_handler = &proc_dostring,
        },
#ifndef HAVE_KALLSYMS_LOOKUP_NAME
        {
                CTL_NAME    (CTL_KALLSYMS)
                .procname = "kallsyms_lookup_name",
                .data     = &spl_kallsyms_lookup_name_fn,
                .maxlen   = sizeof(unsigned long),
                .mode     = 0644,
                .proc_handler = &proc_dokallsyms_lookup_name,
        },
#endif
#ifdef DEBUG_LOG
	{
		CTL_NAME    (CTL_SPL_DEBUG)
		.procname = "debug",
		.mode     = 0555,
		.child    = spl_debug_table,
	},
#endif
	{
		CTL_NAME    (CTL_SPL_VM)
		.procname = "vm",
		.mode     = 0555,
		.child    = spl_vm_table,
	},
#ifdef DEBUG_KMEM
	{
		CTL_NAME    (CTL_SPL_KMEM)
		.procname = "kmem",
		.mode     = 0555,
		.child    = spl_kmem_table,
	},
#endif
	{
		CTL_NAME    (CTL_SPL_KSTAT)
		.procname = "kstat",
		.mode     = 0555,
		.child    = spl_kstat_table,
	},
        { 0 },
};

static struct ctl_table spl_dir[] = {
        {
                CTL_NAME    (CTL_SPL)
                .procname = "spl",
                .mode     = 0555,
                .child    = spl_table,
        },
        { 0 }
};

static struct ctl_table spl_root[] = {
	{
	CTL_NAME    (CTL_KERN)
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
spl_proc_init(void)
{
	int rc = 0;
        SENTRY;

#ifdef CONFIG_SYSCTL
        spl_header = spl_register_sysctl_table(spl_root, 0);
	if (spl_header == NULL)
		SRETURN(-EUNATCH);
#endif /* CONFIG_SYSCTL */

	proc_spl = proc_mkdir("spl", NULL);
	if (proc_spl == NULL)
		SGOTO(out, rc = -EUNATCH);

#ifdef DEBUG_KMEM
        proc_spl_kmem = proc_mkdir("kmem", proc_spl);
        if (proc_spl_kmem == NULL)
                SGOTO(out, rc = -EUNATCH);

	proc_spl_kmem_slab = create_proc_entry("slab", 0444, proc_spl_kmem);
        if (proc_spl_kmem_slab == NULL)
		SGOTO(out, rc = -EUNATCH);

        proc_spl_kmem_slab->proc_fops = &proc_slab_operations;
#endif /* DEBUG_KMEM */

        proc_spl_kstat = proc_mkdir("kstat", proc_spl);
        if (proc_spl_kstat == NULL)
                SGOTO(out, rc = -EUNATCH);
out:
	if (rc) {
		remove_proc_entry("kstat", proc_spl);
#ifdef DEBUG_KMEM
	        remove_proc_entry("slab", proc_spl_kmem);
		remove_proc_entry("kmem", proc_spl);
#endif
		remove_proc_entry("spl", NULL);
#ifdef CONFIG_SYSCTL
	        spl_unregister_sysctl_table(spl_header);
#endif /* CONFIG_SYSCTL */
	}

        SRETURN(rc);
}

void
spl_proc_fini(void)
{
        SENTRY;

	remove_proc_entry("kstat", proc_spl);
#ifdef DEBUG_KMEM
        remove_proc_entry("slab", proc_spl_kmem);
	remove_proc_entry("kmem", proc_spl);
#endif
	remove_proc_entry("spl", NULL);

#ifdef CONFIG_SYSCTL
        ASSERT(spl_header != NULL);
        spl_unregister_sysctl_table(spl_header);
#endif /* CONFIG_SYSCTL */

        SEXIT;
}
