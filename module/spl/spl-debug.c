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
 *  Solaris Porting Layer (SPL) Debug Implementation.
\*****************************************************************************/

#include <linux/kmod.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>
#include <linux/pagemap.h>
#include <linux/slab.h>
#include <linux/ctype.h>
#include <linux/kthread.h>
#include <linux/hardirq.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/proc_compat.h>
#include <linux/file_compat.h>
#include <sys/sysmacros.h>
#include <spl-debug.h>
#include <spl-trace.h>
#include <spl-ctl.h>

#ifdef SS_DEBUG_SUBSYS
#undef SS_DEBUG_SUBSYS
#endif

#define SS_DEBUG_SUBSYS SS_DEBUG

/* Debug log support enabled */
#ifdef DEBUG_LOG

unsigned long spl_debug_subsys = ~0;
EXPORT_SYMBOL(spl_debug_subsys);
module_param(spl_debug_subsys, ulong, 0644);
MODULE_PARM_DESC(spl_debug_subsys, "Subsystem debugging level mask.");

unsigned long spl_debug_mask = SD_CANTMASK;
EXPORT_SYMBOL(spl_debug_mask);
module_param(spl_debug_mask, ulong, 0644);
MODULE_PARM_DESC(spl_debug_mask, "Debugging level mask.");

unsigned long spl_debug_printk = SD_CANTMASK;
EXPORT_SYMBOL(spl_debug_printk);
module_param(spl_debug_printk, ulong, 0644);
MODULE_PARM_DESC(spl_debug_printk, "Console printk level mask.");

int spl_debug_mb = -1;
EXPORT_SYMBOL(spl_debug_mb);
module_param(spl_debug_mb, int, 0644);
MODULE_PARM_DESC(spl_debug_mb, "Total debug buffer size.");

unsigned int spl_debug_binary = 1;
EXPORT_SYMBOL(spl_debug_binary);

unsigned int spl_debug_catastrophe;
EXPORT_SYMBOL(spl_debug_catastrophe);

unsigned int spl_debug_panic_on_bug = 0;
EXPORT_SYMBOL(spl_debug_panic_on_bug);
module_param(spl_debug_panic_on_bug, uint, 0644);
MODULE_PARM_DESC(spl_debug_panic_on_bug, "Panic on BUG");

static char spl_debug_file_name[PATH_MAX];
char spl_debug_file_path[PATH_MAX] = "/tmp/spl-log";

unsigned int spl_console_ratelimit = 1;
EXPORT_SYMBOL(spl_console_ratelimit);

long spl_console_max_delay;
EXPORT_SYMBOL(spl_console_max_delay);

long spl_console_min_delay;
EXPORT_SYMBOL(spl_console_min_delay);

unsigned int spl_console_backoff = SPL_DEFAULT_BACKOFF;
EXPORT_SYMBOL(spl_console_backoff);

unsigned int spl_debug_stack;
EXPORT_SYMBOL(spl_debug_stack);

static int spl_panic_in_progress;

union trace_data_union (*trace_data[TCD_TYPE_MAX])[NR_CPUS] __cacheline_aligned;
char *trace_console_buffers[NR_CPUS][3];
struct rw_semaphore trace_sem;
atomic_t trace_tage_allocated = ATOMIC_INIT(0);

static int spl_debug_dump_all_pages(dumplog_priv_t *dp, char *);
static void trace_fini(void);


/* Memory percentage breakdown by type */
static unsigned int pages_factor[TCD_TYPE_MAX] = {
       80,  /* 80% pages for TCD_TYPE_PROC */
       10,  /* 10% pages for TCD_TYPE_SOFTIRQ */
       10   /* 10% pages for TCD_TYPE_IRQ */
};

const char *
spl_debug_subsys2str(int subsys)
{
        switch (subsys) {
        default:
                return NULL;
        case SS_UNDEFINED:
                return "undefined";
        case SS_ATOMIC:
                return "atomic";
        case SS_KOBJ:
                return "kobj";
        case SS_VNODE:
                return "vnode";
        case SS_TIME:
                return "time";
        case SS_RWLOCK:
                return "rwlock";
        case SS_THREAD:
                return "thread";
        case SS_CONDVAR:
                return "condvar";
        case SS_MUTEX:
                return "mutex";
        case SS_RNG:
                return "rng";
        case SS_TASKQ:
                return "taskq";
        case SS_KMEM:
                return "kmem";
        case SS_DEBUG:
                return "debug";
        case SS_GENERIC:
                return "generic";
        case SS_PROC:
                return "proc";
        case SS_MODULE:
                return "module";
        case SS_CRED:
                return "cred";
        case SS_KSTAT:
                return "kstat";
        case SS_XDR:
                return "xdr";
        case SS_TSD:
                return "tsd";
	case SS_ZLIB:
		return "zlib";
        case SS_USER1:
                return "user1";
        case SS_USER2:
                return "user2";
        case SS_USER3:
                return "user3";
        case SS_USER4:
                return "user4";
        case SS_USER5:
                return "user5";
        case SS_USER6:
                return "user6";
        case SS_USER7:
                return "user7";
        case SS_USER8:
                return "user8";
        }
}

const char *
spl_debug_dbg2str(int debug)
{
        switch (debug) {
        default:
                return NULL;
        case SD_TRACE:
                return "trace";
        case SD_INFO:
                return "info";
        case SD_WARNING:
                return "warning";
        case SD_ERROR:
                return "error";
        case SD_EMERG:
                return "emerg";
        case SD_CONSOLE:
                return "console";
        case SD_IOCTL:
                return "ioctl";
        case SD_DPRINTF:
                return "dprintf";
        case SD_OTHER:
                return "other";
        }
}

int
spl_debug_mask2str(char *str, int size, unsigned long mask, int is_subsys)
{
        const char *(*fn)(int bit) = is_subsys ? spl_debug_subsys2str :
                                                 spl_debug_dbg2str;
        const char *token;
        int i, bit, len = 0;

        if (mask == 0) {                        /* "0" */
                if (size > 0)
                        str[0] = '0';
                len = 1;
        } else {                                /* space-separated tokens */
                for (i = 0; i < 32; i++) {
                        bit = 1 << i;

                        if ((mask & bit) == 0)
                                continue;

                        token = fn(bit);
                        if (token == NULL)              /* unused bit */
                                continue;

                        if (len > 0) {                  /* separator? */
                                if (len < size)
                                        str[len] = ' ';
                                len++;
                        }

                        while (*token != 0) {
                                if (len < size)
                                        str[len] = *token;
                                token++;
                                len++;
                        }
                }
        }

        /* terminate 'str' */
        if (len < size)
                str[len] = 0;
        else
                str[size - 1] = 0;

        return len;
}

static int
spl_debug_token2mask(int *mask, const char *str, int len, int is_subsys)
{
        const char *(*fn)(int bit) = is_subsys ? spl_debug_subsys2str :
                                                 spl_debug_dbg2str;
        const char   *token;
        int i, j, bit;

        /* match against known tokens */
        for (i = 0; i < 32; i++) {
                bit = 1 << i;

                token = fn(bit);
                if (token == NULL)              /* unused? */
                        continue;

                /* strcasecmp */
                for (j = 0; ; j++) {
                        if (j == len) {         /* end of token */
                                if (token[j] == 0) {
                                        *mask = bit;
                                        return 0;
                                }
                                break;
                        }

                        if (token[j] == 0)
                                break;

                        if (str[j] == token[j])
                                continue;

                        if (str[j] < 'A' || 'Z' < str[j])
                                break;

                        if (str[j] - 'A' + 'a' != token[j])
                                break;
                }
        }

        return -EINVAL;                         /* no match */
}

int
spl_debug_str2mask(unsigned long *mask, const char *str, int is_subsys)
{
        char op = 0;
        int m = 0, matched, n, t;

        /* Allow a number for backwards compatibility */
        for (n = strlen(str); n > 0; n--)
                if (!isspace(str[n-1]))
                        break;
        matched = n;

        if ((t = sscanf(str, "%i%n", &m, &matched)) >= 1 && matched == n) {
                *mask = m;
                return 0;
        }

        /* <str> must be a list of debug tokens or numbers separated by
         * whitespace and optionally an operator ('+' or '-').  If an operator
         * appears first in <str>, '*mask' is used as the starting point
         * (relative), otherwise 0 is used (absolute).  An operator applies to
         * all following tokens up to the next operator. */
        matched = 0;
        while (*str != 0) {
                while (isspace(*str)) /* skip whitespace */
                        str++;

                if (*str == 0)
                        break;

                if (*str == '+' || *str == '-') {
                        op = *str++;

                        /* op on first token == relative */
                        if (!matched)
                                m = *mask;

                        while (isspace(*str)) /* skip whitespace */
                                str++;

                        if (*str == 0)          /* trailing op */
                                return -EINVAL;
                }

                /* find token length */
                for (n = 0; str[n] != 0 && !isspace(str[n]); n++);

                /* match token */
                if (spl_debug_token2mask(&t, str, n, is_subsys) != 0)
                        return -EINVAL;

                matched = 1;
                if (op == '-')
                        m &= ~t;
                else
                        m |= t;

                str += n;
        }

        if (!matched)
                return -EINVAL;

        *mask = m;
        return 0;
}

static void
spl_debug_dumplog_internal(dumplog_priv_t *dp)
{
        void *journal_info;

        journal_info = current->journal_info;
        current->journal_info = NULL;

        snprintf(spl_debug_file_name, sizeof(spl_debug_file_path) - 1,
                 "%s.%ld.%ld", spl_debug_file_path,
		 get_seconds(), (long)dp->dp_pid);
        printk("SPL: Dumping log to %s\n", spl_debug_file_name);
        spl_debug_dump_all_pages(dp, spl_debug_file_name);

        current->journal_info = journal_info;
}

static int
spl_debug_dumplog_thread(void *arg)
{
	dumplog_priv_t *dp = (dumplog_priv_t *)arg;

        spl_debug_dumplog_internal(dp);
	atomic_set(&dp->dp_done, 1);
        wake_up(&dp->dp_waitq);
	complete_and_exit(NULL, 0);

        return 0; /* Unreachable */
}

/* When flag is set do not use a new thread for the debug dump */
int
spl_debug_dumplog(int flags)
{
	struct task_struct *tsk;
	dumplog_priv_t dp;

        init_waitqueue_head(&dp.dp_waitq);
        dp.dp_pid = current->pid;
        dp.dp_flags = flags;
        atomic_set(&dp.dp_done, 0);

        if (dp.dp_flags & DL_NOTHREAD) {
                spl_debug_dumplog_internal(&dp);
        } else {

                tsk = kthread_create(spl_debug_dumplog_thread,(void *)&dp,"spl_debug");
                if (tsk == NULL)
                        return -ENOMEM;

                wake_up_process(tsk);
                wait_event(dp.dp_waitq, atomic_read(&dp.dp_done));
        }

	return 0;
}
EXPORT_SYMBOL(spl_debug_dumplog);

static char *
trace_get_console_buffer(void)
{
        int  cpu = get_cpu();
        int  idx;

        if (in_irq()) {
                idx = 0;
        } else if (in_softirq()) {
                idx = 1;
        } else {
                idx = 2;
        }

        return trace_console_buffers[cpu][idx];
}

static void
trace_put_console_buffer(char *buffer)
{
        put_cpu();
}

static int
trace_lock_tcd(struct trace_cpu_data *tcd)
{
        __ASSERT(tcd->tcd_type < TCD_TYPE_MAX);

        spin_lock_irqsave(&tcd->tcd_lock, tcd->tcd_lock_flags);

        return 1;
}

static void
trace_unlock_tcd(struct trace_cpu_data *tcd)
{
        __ASSERT(tcd->tcd_type < TCD_TYPE_MAX);

        spin_unlock_irqrestore(&tcd->tcd_lock, tcd->tcd_lock_flags);
}

static struct trace_cpu_data *
trace_get_tcd(void)
{
        int cpu;
        struct trace_cpu_data *tcd;

        cpu = get_cpu();
        if (in_irq())
                tcd = &(*trace_data[TCD_TYPE_IRQ])[cpu].tcd;
        else if (in_softirq())
                tcd = &(*trace_data[TCD_TYPE_SOFTIRQ])[cpu].tcd;
        else
                tcd = &(*trace_data[TCD_TYPE_PROC])[cpu].tcd;

        trace_lock_tcd(tcd);

        return tcd;
}

static void
trace_put_tcd (struct trace_cpu_data *tcd)
{
        trace_unlock_tcd(tcd);

        put_cpu();
}

static void
trace_set_debug_header(struct spl_debug_header *header, int subsys,
                       int mask, const int line, unsigned long stack)
{
        struct timeval tv;

        do_gettimeofday(&tv);

        header->ph_subsys = subsys;
        header->ph_mask = mask;
        header->ph_cpu_id = smp_processor_id();
        header->ph_sec = (__u32)tv.tv_sec;
        header->ph_usec = tv.tv_usec;
        header->ph_stack = stack;
        header->ph_pid = current->pid;
        header->ph_line_num = line;

        return;
}

static void
trace_print_to_console(struct spl_debug_header *hdr, int mask, const char *buf,
                       int len, const char *file, const char *fn)
{
        char *prefix = "SPL", *ptype = NULL;

        if ((mask & SD_EMERG) != 0) {
                prefix = "SPLError";
                ptype = KERN_EMERG;
        } else if ((mask & SD_ERROR) != 0) {
                prefix = "SPLError";
                ptype = KERN_ERR;
        } else if ((mask & SD_WARNING) != 0) {
                prefix = "SPL";
                ptype = KERN_WARNING;
        } else if ((mask & (SD_CONSOLE | spl_debug_printk)) != 0) {
                prefix = "SPL";
                ptype = KERN_INFO;
        }

        if ((mask & SD_CONSOLE) != 0) {
                printk("%s%s: %.*s", ptype, prefix, len, buf);
        } else {
                printk("%s%s: %d:%d:(%s:%d:%s()) %.*s", ptype, prefix,
                       hdr->ph_pid, hdr->ph_stack, file,
                       hdr->ph_line_num, fn, len, buf);
        }

        return;
}

static int
trace_max_debug_mb(void)
{
        return MAX(512, ((num_physpages >> (20 - PAGE_SHIFT)) * 80) / 100);
}

static struct trace_page *
tage_alloc(int gfp)
{
        struct page *page;
        struct trace_page *tage;

        page = alloc_pages(gfp | __GFP_NOWARN, 0);
        if (page == NULL)
                return NULL;

        tage = kmalloc(sizeof(*tage), gfp);
        if (tage == NULL) {
                __free_pages(page, 0);
                return NULL;
        }

        tage->page = page;
        atomic_inc(&trace_tage_allocated);

        return tage;
}

static void
tage_free(struct trace_page *tage)
{
        __ASSERT(tage != NULL);
        __ASSERT(tage->page != NULL);

        __free_pages(tage->page, 0);
        kfree(tage);
        atomic_dec(&trace_tage_allocated);
}

static struct trace_page *
tage_from_list(struct list_head *list)
{
        return list_entry(list, struct trace_page, linkage);
}

static void
tage_to_tail(struct trace_page *tage, struct list_head *queue)
{
        __ASSERT(tage != NULL);
        __ASSERT(queue != NULL);

        list_move_tail(&tage->linkage, queue);
}

/* try to return a page that has 'len' bytes left at the end */
static struct trace_page *
trace_get_tage_try(struct trace_cpu_data *tcd, unsigned long len)
{
        struct trace_page *tage;

        if (tcd->tcd_cur_pages > 0) {
                __ASSERT(!list_empty(&tcd->tcd_pages));
                tage = tage_from_list(tcd->tcd_pages.prev);
                if (tage->used + len <= PAGE_SIZE)
                        return tage;
        }

        if (tcd->tcd_cur_pages < tcd->tcd_max_pages) {
                if (tcd->tcd_cur_stock_pages > 0) {
                        tage = tage_from_list(tcd->tcd_stock_pages.prev);
                        tcd->tcd_cur_stock_pages--;
                        list_del_init(&tage->linkage);
                } else {
                        tage = tage_alloc(GFP_ATOMIC);
                        if (tage == NULL) {
                                printk(KERN_WARNING
                                       "failure to allocate a tage (%ld)\n",
                                       tcd->tcd_cur_pages);
                                return NULL;
                        }
                }

                tage->used = 0;
                tage->cpu = smp_processor_id();
                tage->type = tcd->tcd_type;
                list_add_tail(&tage->linkage, &tcd->tcd_pages);
                tcd->tcd_cur_pages++;

                return tage;
        }

        return NULL;
}

/* return a page that has 'len' bytes left at the end */
static struct trace_page *
trace_get_tage(struct trace_cpu_data *tcd, unsigned long len)
{
        struct trace_page *tage;

        __ASSERT(len <= PAGE_SIZE);

        tage = trace_get_tage_try(tcd, len);
        if (tage)
                return tage;

        if (tcd->tcd_cur_pages > 0) {
                tage = tage_from_list(tcd->tcd_pages.next);
                tage->used = 0;
                tage_to_tail(tage, &tcd->tcd_pages);
        }

        return tage;
}

int
spl_debug_msg(void *arg, int subsys, int mask, const char *file,
    const char *fn, const int line, const char *format, ...)
{
	spl_debug_limit_state_t *cdls = arg;
        struct trace_cpu_data   *tcd = NULL;
        struct spl_debug_header header = { 0, };
        struct trace_page       *tage;
        /* string_buf is used only if tcd != NULL, and is always set then */
        char                    *string_buf = NULL;
        char                    *debug_buf;
        int                      known_size;
        int                      needed = 85; /* average message length */
        int                      max_nob;
        va_list                  ap;
        int                      i;

	if (subsys == 0)
		subsys = SS_DEBUG_SUBSYS;

	if (mask == 0)
		mask = SD_EMERG;

        if (strchr(file, '/'))
                file = strrchr(file, '/') + 1;

        tcd = trace_get_tcd();
        trace_set_debug_header(&header, subsys, mask, line, 0);
        if (tcd == NULL)
                goto console;

        if (tcd->tcd_shutting_down) {
                trace_put_tcd(tcd);
                tcd = NULL;
                goto console;
        }

        known_size = strlen(file) + 1;
        if (fn)
                known_size += strlen(fn) + 1;

        if (spl_debug_binary)
                known_size += sizeof(header);

        /* '2' used because vsnprintf returns real size required for output
         * _without_ terminating NULL. */
        for (i = 0; i < 2; i++) {
                tage = trace_get_tage(tcd, needed + known_size + 1);
                if (tage == NULL) {
                        if (needed + known_size > PAGE_SIZE)
                                mask |= SD_ERROR;

                        trace_put_tcd(tcd);
                        tcd = NULL;
                        goto console;
                }

                string_buf = (char *)page_address(tage->page) +
                             tage->used + known_size;

                max_nob = PAGE_SIZE - tage->used - known_size;
                if (max_nob <= 0) {
                        printk(KERN_EMERG "negative max_nob: %i\n", max_nob);
                        mask |= SD_ERROR;
                        trace_put_tcd(tcd);
                        tcd = NULL;
                        goto console;
                }

                needed = 0;
                if (format) {
                        va_start(ap, format);
                        needed += vsnprintf(string_buf, max_nob, format, ap);
                        va_end(ap);
                }

                if (needed < max_nob)
                        break;
        }

        header.ph_len = known_size + needed;
        debug_buf = (char *)page_address(tage->page) + tage->used;

        if (spl_debug_binary) {
                memcpy(debug_buf, &header, sizeof(header));
                tage->used += sizeof(header);
                debug_buf += sizeof(header);
        }

        strcpy(debug_buf, file);
        tage->used += strlen(file) + 1;
        debug_buf += strlen(file) + 1;

        if (fn) {
                strcpy(debug_buf, fn);
                tage->used += strlen(fn) + 1;
                debug_buf += strlen(fn) + 1;
        }

        __ASSERT(debug_buf == string_buf);

        tage->used += needed;
        __ASSERT (tage->used <= PAGE_SIZE);

console:
        if ((mask & spl_debug_printk) == 0) {
                /* no console output requested */
                if (tcd != NULL)
                        trace_put_tcd(tcd);
                return 1;
        }

        if (cdls != NULL) {
                if (spl_console_ratelimit && cdls->cdls_next != 0 &&
                    !time_before(cdls->cdls_next, jiffies)) {
                        /* skipping a console message */
                        cdls->cdls_count++;
                        if (tcd != NULL)
                                trace_put_tcd(tcd);
                        return 1;
                }

                if (time_before(cdls->cdls_next + spl_console_max_delay +
                                (10 * HZ), jiffies)) {
                        /* last timeout was a long time ago */
                        cdls->cdls_delay /= spl_console_backoff * 4;
                } else {
                        cdls->cdls_delay *= spl_console_backoff;

                        if (cdls->cdls_delay < spl_console_min_delay)
                                cdls->cdls_delay = spl_console_min_delay;
                        else if (cdls->cdls_delay > spl_console_max_delay)
                                cdls->cdls_delay = spl_console_max_delay;
                }

                /* ensure cdls_next is never zero after it's been seen */
                cdls->cdls_next = (jiffies + cdls->cdls_delay) | 1;
        }

        if (tcd != NULL) {
                trace_print_to_console(&header, mask, string_buf, needed, file, fn);
                trace_put_tcd(tcd);
        } else {
                string_buf = trace_get_console_buffer();

                needed = 0;
                if (format != NULL) {
                        va_start(ap, format);
                        needed += vsnprintf(string_buf,
                            TRACE_CONSOLE_BUFFER_SIZE, format, ap);
                        va_end(ap);
                }
                trace_print_to_console(&header, mask,
                                 string_buf, needed, file, fn);

                trace_put_console_buffer(string_buf);
        }

        if (cdls != NULL && cdls->cdls_count != 0) {
                string_buf = trace_get_console_buffer();

                needed = snprintf(string_buf, TRACE_CONSOLE_BUFFER_SIZE,
                         "Skipped %d previous similar message%s\n",
                         cdls->cdls_count, (cdls->cdls_count > 1) ? "s" : "");

                trace_print_to_console(&header, mask,
                                 string_buf, needed, file, fn);

                trace_put_console_buffer(string_buf);
                cdls->cdls_count = 0;
        }

        return 0;
}
EXPORT_SYMBOL(spl_debug_msg);

/* Do the collect_pages job on a single CPU: assumes that all other
 * CPUs have been stopped during a panic.  If this isn't true for
 * some arch, this will have to be implemented separately in each arch.
 */
static void
collect_pages_from_single_cpu(struct page_collection *pc)
{
        struct trace_cpu_data *tcd;
        int i, j;

        tcd_for_each(tcd, i, j) {
                list_splice_init(&tcd->tcd_pages, &pc->pc_pages);
                tcd->tcd_cur_pages = 0;
        }
}

static void
collect_pages_on_all_cpus(struct page_collection *pc)
{
        struct trace_cpu_data *tcd;
        int i, cpu;

        spin_lock(&pc->pc_lock);
        for_each_possible_cpu(cpu) {
                tcd_for_each_type_lock(tcd, i, cpu) {
                        list_splice_init(&tcd->tcd_pages, &pc->pc_pages);
                        tcd->tcd_cur_pages = 0;
                }
        }
        spin_unlock(&pc->pc_lock);
}

static void
collect_pages(dumplog_priv_t *dp, struct page_collection *pc)
{
        INIT_LIST_HEAD(&pc->pc_pages);

        if (spl_panic_in_progress || dp->dp_flags & DL_SINGLE_CPU)
                collect_pages_from_single_cpu(pc);
        else
                collect_pages_on_all_cpus(pc);
}

static void
put_pages_back_on_all_cpus(struct page_collection *pc)
{
        struct trace_cpu_data *tcd;
        struct list_head *cur_head;
        struct trace_page *tage;
        struct trace_page *tmp;
        int i, cpu;

        spin_lock(&pc->pc_lock);

        for_each_possible_cpu(cpu) {
                tcd_for_each_type_lock(tcd, i, cpu) {
                        cur_head = tcd->tcd_pages.next;

                        list_for_each_entry_safe(tage, tmp, &pc->pc_pages,
                                                 linkage) {
                                if (tage->cpu != cpu || tage->type != i)
                                        continue;

                                tage_to_tail(tage, cur_head);
                                tcd->tcd_cur_pages++;
                        }
                }
        }

        spin_unlock(&pc->pc_lock);
}

static void
put_pages_back(struct page_collection *pc)
{
        if (!spl_panic_in_progress)
                put_pages_back_on_all_cpus(pc);
}

static int
spl_debug_dump_all_pages(dumplog_priv_t *dp, char *filename)
{
        struct page_collection pc;
        struct file *filp;
        struct trace_page *tage;
        struct trace_page *tmp;
        mm_segment_t oldfs;
        int rc = 0;

        down_write(&trace_sem);

        filp = spl_filp_open(filename, O_CREAT|O_EXCL|O_WRONLY|O_LARGEFILE,
                               0600, &rc);
        if (filp == NULL) {
                if (rc != -EEXIST)
                        printk(KERN_ERR "SPL: Can't open %s for dump: %d\n",
                               filename, rc);
                goto out;
        }

        spin_lock_init(&pc.pc_lock);
        collect_pages(dp, &pc);
        if (list_empty(&pc.pc_pages)) {
                rc = 0;
                goto close;
        }

        oldfs = get_fs();
        set_fs(get_ds());

        list_for_each_entry_safe(tage, tmp, &pc.pc_pages, linkage) {
                rc = spl_filp_write(filp, page_address(tage->page),
                                    tage->used, spl_filp_poff(filp));
                if (rc != (int)tage->used) {
                        printk(KERN_WARNING "SPL: Wanted to write %u "
                               "but wrote %d\n", tage->used, rc);
                        put_pages_back(&pc);
                        __ASSERT(list_empty(&pc.pc_pages));
                        break;
                }
                list_del(&tage->linkage);
                tage_free(tage);
        }

        set_fs(oldfs);

        rc = spl_filp_fsync(filp, 1);
        if (rc)
                printk(KERN_ERR "SPL: Unable to sync: %d\n", rc);
 close:
        spl_filp_close(filp);
 out:
        up_write(&trace_sem);

        return rc;
}

static void
spl_debug_flush_pages(void)
{
        dumplog_priv_t dp;
        struct page_collection pc;
        struct trace_page *tage;
        struct trace_page *tmp;

        spin_lock_init(&pc.pc_lock);
        init_waitqueue_head(&dp.dp_waitq);
        dp.dp_pid = current->pid;
        dp.dp_flags = 0;
        atomic_set(&dp.dp_done, 0);

        collect_pages(&dp, &pc);
        list_for_each_entry_safe(tage, tmp, &pc.pc_pages, linkage) {
                list_del(&tage->linkage);
                tage_free(tage);
        }
}

unsigned long
spl_debug_set_mask(unsigned long mask) {
	spl_debug_mask = mask;
        return 0;
}
EXPORT_SYMBOL(spl_debug_set_mask);

unsigned long
spl_debug_get_mask(void) {
        return spl_debug_mask;
}
EXPORT_SYMBOL(spl_debug_get_mask);

unsigned long
spl_debug_set_subsys(unsigned long subsys) {
	spl_debug_subsys = subsys;
        return 0;
}
EXPORT_SYMBOL(spl_debug_set_subsys);

unsigned long
spl_debug_get_subsys(void) {
        return spl_debug_subsys;
}
EXPORT_SYMBOL(spl_debug_get_subsys);

int
spl_debug_set_mb(int mb)
{
        int i, j, pages;
        int limit = trace_max_debug_mb();
        struct trace_cpu_data *tcd;

        if (mb < num_possible_cpus()) {
                printk(KERN_ERR "SPL: Refusing to set debug buffer size to "
                       "%dMB - lower limit is %d\n", mb, num_possible_cpus());
                return -EINVAL;
        }

        if (mb > limit) {
                printk(KERN_ERR "SPL: Refusing to set debug buffer size to "
                       "%dMB - upper limit is %d\n", mb, limit);
                return -EINVAL;
        }

        mb /= num_possible_cpus();
        pages = mb << (20 - PAGE_SHIFT);

        down_write(&trace_sem);

        tcd_for_each(tcd, i, j)
                tcd->tcd_max_pages = (pages * tcd->tcd_pages_factor) / 100;

        up_write(&trace_sem);

        return 0;
}
EXPORT_SYMBOL(spl_debug_set_mb);

int
spl_debug_get_mb(void)
{
        int i, j;
        struct trace_cpu_data *tcd;
        int total_pages = 0;

        down_read(&trace_sem);

        tcd_for_each(tcd, i, j)
                total_pages += tcd->tcd_max_pages;

        up_read(&trace_sem);

        return (total_pages >> (20 - PAGE_SHIFT)) + 1;
}
EXPORT_SYMBOL(spl_debug_get_mb);

void spl_debug_dumpstack(struct task_struct *tsk)
{
        extern void show_task(struct task_struct *);

        if (tsk == NULL)
                tsk = current;

        printk("SPL: Showing stack for process %d\n", tsk->pid);
        dump_stack();
}
EXPORT_SYMBOL(spl_debug_dumpstack);

void spl_debug_bug(char *file, const char *func, const int line, int flags)
{
        spl_debug_catastrophe = 1;
        spl_debug_msg(NULL, 0, SD_EMERG, file, func, line, "SPL PANIC\n");

        if (in_interrupt())
                panic("SPL PANIC in interrupt.\n");

	if (in_atomic() || irqs_disabled())
		flags |= DL_NOTHREAD;

        /* Ensure all debug pages and dumped by current cpu */
         if (spl_debug_panic_on_bug)
                spl_panic_in_progress = 1;

        spl_debug_dumpstack(NULL);
        spl_debug_dumplog(flags);

        if (spl_debug_panic_on_bug)
                panic("SPL PANIC");

        set_task_state(current, TASK_UNINTERRUPTIBLE);
        while (1)
                schedule();
}
EXPORT_SYMBOL(spl_debug_bug);

int
spl_debug_clear_buffer(void)
{
        spl_debug_flush_pages();
        return 0;
}
EXPORT_SYMBOL(spl_debug_clear_buffer);

int
spl_debug_mark_buffer(char *text)
{
        SDEBUG(SD_WARNING, "*************************************\n");
        SDEBUG(SD_WARNING, "DEBUG MARKER: %s\n", text);
        SDEBUG(SD_WARNING, "*************************************\n");

        return 0;
}
EXPORT_SYMBOL(spl_debug_mark_buffer);

static int
trace_init(int max_pages)
{
        struct trace_cpu_data *tcd;
        int i, j;

        init_rwsem(&trace_sem);

        /* initialize trace_data */
        memset(trace_data, 0, sizeof(trace_data));
        for (i = 0; i < TCD_TYPE_MAX; i++) {
                trace_data[i] = kmalloc(sizeof(union trace_data_union) *
                                        NR_CPUS, GFP_KERNEL);
                if (trace_data[i] == NULL)
                        goto out;
        }

        tcd_for_each(tcd, i, j) {
                spin_lock_init(&tcd->tcd_lock);
                tcd->tcd_pages_factor = pages_factor[i];
                tcd->tcd_type = i;
                tcd->tcd_cpu = j;
                INIT_LIST_HEAD(&tcd->tcd_pages);
                INIT_LIST_HEAD(&tcd->tcd_stock_pages);
                tcd->tcd_cur_pages = 0;
                tcd->tcd_cur_stock_pages = 0;
                tcd->tcd_max_pages = (max_pages * pages_factor[i]) / 100;
                tcd->tcd_shutting_down = 0;
        }

        for (i = 0; i < num_possible_cpus(); i++) {
                for (j = 0; j < 3; j++) {
                        trace_console_buffers[i][j] =
                                kmalloc(TRACE_CONSOLE_BUFFER_SIZE,
                                        GFP_KERNEL);

                        if (trace_console_buffers[i][j] == NULL)
                                goto out;
                }
       }

        return 0;
out:
        trace_fini();
        printk(KERN_ERR "SPL: Insufficient memory for debug logs\n");
        return -ENOMEM;
}

int
spl_debug_init(void)
{
        int rc, max = spl_debug_mb;

        spl_console_max_delay = SPL_DEFAULT_MAX_DELAY;
        spl_console_min_delay = SPL_DEFAULT_MIN_DELAY;

        /* If spl_debug_mb is set to an invalid value or uninitialized
         * then just make the total buffers smp_num_cpus TCD_MAX_PAGES */
        if (max > (num_physpages >> (20 - 2 - PAGE_SHIFT)) / 5 ||
            max >= 512 || max < 0) {
                max = TCD_MAX_PAGES;
        } else {
                max = (max / num_online_cpus()) << (20 - PAGE_SHIFT);
        }

        rc = trace_init(max);
        if (rc)
                return rc;

        return rc;
}

static void
trace_cleanup_on_all_cpus(void)
{
        struct trace_cpu_data *tcd;
        struct trace_page *tage;
        struct trace_page *tmp;
        int i, cpu;

        for_each_possible_cpu(cpu) {
                tcd_for_each_type_lock(tcd, i, cpu) {
                        tcd->tcd_shutting_down = 1;

                        list_for_each_entry_safe(tage, tmp, &tcd->tcd_pages,
                                                 linkage) {
                                list_del(&tage->linkage);
                                tage_free(tage);
                        }
                        tcd->tcd_cur_pages = 0;
                }
        }
}

static void
trace_fini(void)
{
        int i, j;

        trace_cleanup_on_all_cpus();

        for (i = 0; i < num_possible_cpus(); i++) {
                for (j = 0; j < 3; j++) {
                        if (trace_console_buffers[i][j] != NULL) {
                                kfree(trace_console_buffers[i][j]);
                                trace_console_buffers[i][j] = NULL;
                        }
                }
        }

        for (i = 0; i < TCD_TYPE_MAX && trace_data[i] != NULL; i++) {
                kfree(trace_data[i]);
                trace_data[i] = NULL;
        }
}

void
spl_debug_fini(void)
{
        trace_fini();
}

#endif /* DEBUG_LOG */
