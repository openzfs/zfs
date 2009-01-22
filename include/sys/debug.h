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

#ifndef _SPL_DEBUG_H
#define _SPL_DEBUG_H

#ifdef  __cplusplus
extern "C" {
#endif

#include <linux/sched.h> /* THREAD_SIZE */
#include <linux/proc_fs.h>

extern unsigned long spl_debug_mask;
extern unsigned long spl_debug_subsys;

#define S_UNDEFINED   0x00000001
#define S_ATOMIC      0x00000002
#define S_KOBJ        0x00000004
#define S_VNODE       0x00000008
#define S_TIME        0x00000010
#define S_RWLOCK      0x00000020
#define S_THREAD      0x00000040
#define S_CONDVAR     0x00000080
#define S_MUTEX       0x00000100
#define S_RNG         0x00000200
#define S_TASKQ       0x00000400
#define S_KMEM        0x00000800
#define S_DEBUG       0x00001000
#define S_GENERIC     0x00002000
#define S_PROC        0x00004000
#define S_MODULE      0x00008000

#define D_TRACE       0x00000001
#define D_INFO        0x00000002
#define D_WARNING     0x00000004
#define D_ERROR       0x00000008
#define D_EMERG       0x00000010
#define D_CONSOLE     0x00000020
#define D_IOCTL       0x00000040
#define D_DPRINTF     0x00000080
#define D_OTHER       0x00000100

#define D_CANTMASK    (D_ERROR | D_EMERG | D_WARNING | D_CONSOLE)
#define DEBUG_SUBSYSTEM S_UNDEFINED

int debug_init(void);
void debug_fini(void);
int spl_debug_mask2str(char *str, int size, unsigned long mask, int is_subsys);
int spl_debug_str2mask(unsigned long *mask, const char *str, int is_subsys);

extern unsigned long spl_debug_subsys;
extern unsigned long spl_debug_mask;
extern unsigned long spl_debug_printk;
extern int spl_debug_mb;
extern unsigned int spl_debug_binary;
extern unsigned int spl_debug_catastrophe;
extern unsigned int spl_debug_panic_on_bug;
extern char spl_debug_file_path[PATH_MAX];
extern unsigned int spl_console_ratelimit;
extern long spl_console_max_delay;
extern long spl_console_min_delay;
extern unsigned int spl_console_backoff;
extern unsigned int spl_debug_stack;

#define TCD_MAX_PAGES                   (5 << (20 - PAGE_SHIFT))
#define TCD_STOCK_PAGES                 (TCD_MAX_PAGES)
#define TRACE_CONSOLE_BUFFER_SIZE       1024

#define SPL_DEFAULT_MAX_DELAY           (600 * HZ)
#define SPL_DEFAULT_MIN_DELAY           ((HZ + 1) / 2)
#define SPL_DEFAULT_BACKOFF             2

#define DL_NOTHREAD                     0x0001 /* Do not create a new thread */
#define DL_SINGLE_CPU                   0x0002 /* Collect pages from this CPU */

typedef struct dumplog_priv {
        wait_queue_head_t dp_waitq;
        pid_t dp_pid;
        int dp_flags;
        atomic_t dp_done;
} dumplog_priv_t;

typedef struct {
        unsigned long cdls_next;
        int           cdls_count;
        long          cdls_delay;
} spl_debug_limit_state_t;

/* Three trace data types */
typedef enum {
        TCD_TYPE_PROC,
        TCD_TYPE_SOFTIRQ,
        TCD_TYPE_IRQ,
        TCD_TYPE_MAX
} tcd_type_t;

union trace_data_union {
	struct trace_cpu_data {
		/* pages with trace records not yet processed by tracefiled */
		struct list_head        tcd_pages;
		/* number of pages on ->tcd_pages */
		unsigned long           tcd_cur_pages;
		/* Max number of pages allowed on ->tcd_pages */
		unsigned long           tcd_max_pages;

		/*
		 * preallocated pages to write trace records into. Pages from
		 * ->tcd_stock_pages are moved to ->tcd_pages by spl_debug_msg().
		 *
		 * This list is necessary, because on some platforms it's
		 * impossible to perform efficient atomic page allocation in a
		 * non-blockable context.
		 *
		 * Such platforms fill ->tcd_stock_pages "on occasion", when
		 * tracing code is entered in blockable context.
		 *
		 * trace_get_tage_try() tries to get a page from
		 * ->tcd_stock_pages first and resorts to atomic page
		 * allocation only if this queue is empty. ->tcd_stock_pages
		 * is replenished when tracing code is entered in blocking
		 * context (darwin-tracefile.c:trace_get_tcd()). We try to
		 * maintain TCD_STOCK_PAGES (40 by default) pages in this
		 * queue. Atomic allocation is only required if more than
		 * TCD_STOCK_PAGES pagesful are consumed by trace records all
		 * emitted in non-blocking contexts. Which is quite unlikely.
		 */
		struct list_head        tcd_stock_pages;
		/* number of pages on ->tcd_stock_pages */
		unsigned long           tcd_cur_stock_pages;

		unsigned short          tcd_shutting_down;
		unsigned short          tcd_cpu;
		unsigned short          tcd_type;
		/* The factors to share debug memory. */
		unsigned short          tcd_pages_factor;

		/*
		 * This spinlock is needed to workaround the problem of
		 * set_cpus_allowed() being GPL-only. Since we cannot
		 * schedule a thread on a specific CPU when dumping the
		 * pages, we must use the spinlock for mutual exclusion.
		 */
		spinlock_t              tcd_lock;
		unsigned long           tcd_lock_flags;
	} tcd;
	char __pad[L1_CACHE_ALIGN(sizeof(struct trace_cpu_data))];
};

extern union trace_data_union (*trace_data[TCD_TYPE_MAX])[NR_CPUS];

#define tcd_for_each(tcd, i, j)                                       \
    for (i = 0; trace_data[i] != NULL; i++)                           \
        for (j = 0, ((tcd) = &(*trace_data[i])[j].tcd);               \
             j < num_possible_cpus(); j++, (tcd) = &(*trace_data[i])[j].tcd)

#define tcd_for_each_type_lock(tcd, i, cpu)                           \
    for (i = 0; trace_data[i] &&                                      \
         (tcd = &(*trace_data[i])[cpu].tcd) &&                        \
         trace_lock_tcd(tcd); trace_unlock_tcd(tcd), i++)

struct trace_page {
	struct page *    page;    /* page itself */
	struct list_head linkage;  /* Used by lists in trace_data_union */
	unsigned int     used;     /* number of bytes used within this page */
	unsigned short   cpu;      /* cpu that owns this page */
	unsigned short   type;     /* type(context) of this page */
};

struct page_collection {
	struct list_head  pc_pages;
	spinlock_t        pc_lock;
	int               pc_want_daemon_pages;
};

#define SBUG()            spl_debug_bug(__FILE__, __FUNCTION__, __LINE__, 0);

#ifdef NDEBUG

#define CDEBUG_STACK()			(0)
#define CDEBUG_LIMIT(x, y, z, a...)	((void)0)
#define __CDEBUG_LIMIT(x, y, z, a...)	((void)0)
#define CDEBUG(mask, format, a...)	((void)0)
#define CWARN(fmt, a...)		((void)0)
#define CERROR(fmt, a...)		((void)0)
#define CEMERG(fmt, a...)		((void)0)
#define CONSOLE(mask, fmt, a...)	((void)0)

#define ENTRY				((void)0)
#define EXIT				((void)0)
#define RETURN(x)			return (x)
#define GOTO(x, y)			{ ((void)(y)); goto x; }

#define __ASSERT(x)			((void)0)
#define __ASSERT_TAGE_INVARIANT(x)	((void)0)
#define ASSERT(x)			((void)0)
#define ASSERTF(x, y, z...)		((void)0)
#define VERIFY(cond)                                                    \
do {                                                                    \
        if (unlikely(!(cond))) {                                        \
                printk(KERN_ERR "VERIFY(" #cond ") failed\n");          \
                SBUG();                                                 \
        }                                                               \
} while (0)

#define VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE, FMT, CAST)                  \
do {                                                                    \
        if (!((TYPE)(LEFT) OP (TYPE)(RIGHT))) {                         \
                printk(KERN_ERR                                         \
                       "VERIFY3(" #LEFT " " #OP " " #RIGHT ") "         \
                       "failed (" FMT " " #OP " " FMT ")\n",            \
                       CAST (LEFT), CAST (RIGHT));                      \
                SBUG();                                                 \
        }                                                               \
} while (0)

#define VERIFY3S(x,y,z) VERIFY3_IMPL(x, y, z, int64_t, "%lld", (long long))
#define VERIFY3U(x,y,z) VERIFY3_IMPL(x, y, z, uint64_t, "%llu",         \
                                     (unsigned long long))
#define VERIFY3P(x,y,z) VERIFY3_IMPL(x, y, z, uintptr_t, "%p", (void *))

#define ASSERT3S(x,y,z) ((void)0)
#define ASSERT3U(x,y,z) ((void)0)
#define ASSERT3P(x,y,z) ((void)0)

#else /* NDEBUG */

#ifdef  __ia64__
#define CDEBUG_STACK() (THREAD_SIZE -                                   \
                       ((unsigned long)__builtin_dwarf_cfa() &          \
                       (THREAD_SIZE - 1)))
#else
#define CDEBUG_STACK() (THREAD_SIZE -                                   \
                       ((unsigned long)__builtin_frame_address(0) &     \
                        (THREAD_SIZE - 1)))
# endif /* __ia64__ */

/* DL_SINGLE_CPU flag is passed to spl_debug_bug() because we are about
 * to over run our stack and likely damage at least one other unknown
 * thread stack.  We must finish generating the needed debug info within
 * this thread context because once we yeild the CPU its very likely
 * the system will crash.
 */
#define __CHECK_STACK(file, func, line)                                 \
do {                                                                    \
        if (unlikely(CDEBUG_STACK() > spl_debug_stack)) {               \
                spl_debug_stack = CDEBUG_STACK();                       \
                                                                        \
                if (unlikely(CDEBUG_STACK() > (4 * THREAD_SIZE) / 5)) { \
                        spl_debug_msg(NULL, D_TRACE, D_WARNING,         \
                                      file, func, line, "Error "        \
                                      "exceeded maximum safe stack "    \
                                      "size (%lu/%lu)\n",               \
                                      CDEBUG_STACK(), THREAD_SIZE);     \
                        spl_debug_bug(file, func, line, DL_SINGLE_CPU); \
                }                                                       \
        }                                                               \
} while (0)

#define CHECK_STACK()   __CHECK_STACK(__FILE__, __func__, __LINE__)

/* ASSERTION that is safe to use within the debug system */
#define __ASSERT(cond)                                                  \
do {                                                                    \
        if (unlikely(!(cond))) {                                        \
                printk(KERN_ERR "ASSERTION(" #cond ") failed\n");       \
                BUG();                                                  \
        }                                                               \
} while (0)

#define __ASSERT_TAGE_INVARIANT(tage)                                   \
do {                                                                    \
        __ASSERT(tage != NULL);                                         \
        __ASSERT(tage->page != NULL);                                   \
        __ASSERT(tage->used <= PAGE_SIZE);                              \
        __ASSERT(page_count(tage->page) > 0);                           \
} while(0)

/* ASSERTION that will debug log used outside the debug sysytem */
#define ASSERT(cond)                                                    \
do {                                                                    \
        CHECK_STACK();                                                  \
                                                                        \
        if (unlikely(!(cond))) {                                        \
                spl_debug_msg(NULL, DEBUG_SUBSYSTEM, D_EMERG,           \
                              __FILE__, __FUNCTION__, __LINE__,         \
                              "ASSERTION(" #cond ") failed\n");         \
                SBUG();                                                 \
        }                                                               \
} while (0)

#define ASSERTF(cond, fmt, a...)                                        \
do {                                                                    \
        CHECK_STACK();                                                  \
                                                                        \
        if (unlikely(!(cond))) {                                        \
                spl_debug_msg(NULL, DEBUG_SUBSYSTEM, D_EMERG,           \
                              __FILE__, __FUNCTION__, __LINE__,         \
                              "ASSERTION(" #cond ") failed: " fmt,      \
                                 ## a);                                 \
                SBUG();                                                 \
        }                                                               \
} while (0)

#define VERIFY3_IMPL(LEFT, OP, RIGHT, TYPE, FMT, CAST)                  \
do {                                                                    \
        CHECK_STACK();                                                  \
                                                                        \
        if (!((TYPE)(LEFT) OP (TYPE)(RIGHT))) {                         \
                spl_debug_msg(NULL, DEBUG_SUBSYSTEM, D_EMERG,           \
                              __FILE__, __FUNCTION__, __LINE__,         \
                              "VERIFY3(" #LEFT " " #OP " " #RIGHT ") "  \
                              "failed (" FMT " " #OP " " FMT ")\n",     \
                              CAST (LEFT), CAST (RIGHT));               \
                SBUG();                                                 \
        }                                                               \
} while (0)

#define VERIFY3S(x,y,z) VERIFY3_IMPL(x, y, z, int64_t, "%lld", (long long))
#define VERIFY3U(x,y,z) VERIFY3_IMPL(x, y, z, uint64_t, "%llu`",        \
                                     (unsigned long long))
#define VERIFY3P(x,y,z) VERIFY3_IMPL(x, y, z, uintptr_t, "%p", (void *))

#define ASSERT3S(x,y,z) VERIFY3S(x, y, z)
#define ASSERT3U(x,y,z) VERIFY3U(x, y, z)
#define ASSERT3P(x,y,z) VERIFY3P(x, y, z)

#define VERIFY(x)       ASSERT(x)

#define __CDEBUG(cdls, subsys, mask, format, a...)                      \
do {                                                                    \
        CHECK_STACK();                                                  \
                                                                        \
        if (((mask) & D_CANTMASK) != 0 ||                               \
            ((spl_debug_mask & (mask)) != 0 &&                          \
             (spl_debug_subsys & (subsys)) != 0))                       \
                spl_debug_msg(cdls, subsys, mask,                       \
                              __FILE__, __FUNCTION__, __LINE__,         \
                              format, ## a);                            \
} while (0)

#define CDEBUG(mask, format, a...)                                      \
        __CDEBUG(NULL, DEBUG_SUBSYSTEM, mask, format, ## a)

#define __CDEBUG_LIMIT(subsys, mask, format, a...)                      \
do {                                                                    \
        static spl_debug_limit_state_t cdls;                            \
                                                                        \
        __CDEBUG(&cdls, subsys, mask, format, ## a);                    \
} while (0)

#define CDEBUG_LIMIT(mask, format, a...)                                \
        __CDEBUG_LIMIT(DEBUG_SUBSYSTEM, mask, format, ## a)

#define CWARN(fmt, a...)               CDEBUG_LIMIT(D_WARNING, fmt, ## a)
#define CERROR(fmt, a...)              CDEBUG_LIMIT(D_ERROR, fmt, ## a)
#define CEMERG(fmt, a...)              CDEBUG_LIMIT(D_EMERG, fmt, ## a)
#define CONSOLE(mask, fmt, a...)       CDEBUG(D_CONSOLE | (mask), fmt, ## a)

#define GOTO(label, rc)                                                 \
do {                                                                    \
        long GOTO__ret = (long)(rc);                                    \
        CDEBUG(D_TRACE,"Process leaving via %s (rc=%lu : %ld : %lx)\n", \
               #label, (unsigned long)GOTO__ret, (signed long)GOTO__ret,\
               (signed long)GOTO__ret);                                 \
        goto label;                                                     \
} while (0)

#define RETURN(rc)                                                      \
do {                                                                    \
        typeof(rc) RETURN__ret = (rc);                                  \
        CDEBUG(D_TRACE, "Process leaving (rc=%lu : %ld : %lx)\n",       \
               (long)RETURN__ret, (long)RETURN__ret, (long)RETURN__ret);\
        return RETURN__ret;                                             \
} while (0)

#define __ENTRY(subsys)                                                 \
do {                                                                    \
        __CDEBUG(NULL, subsys, D_TRACE, "Process entered\n");           \
} while (0)

#define __EXIT(subsys)                                                  \
do {                                                                    \
        __CDEBUG(NULL, subsys, D_TRACE, "Process leaving\n");           \
} while(0)

#define ENTRY                           __ENTRY(DEBUG_SUBSYSTEM)
#define EXIT                            __EXIT(DEBUG_SUBSYSTEM)
#endif /* NDEBUG */

#define spl_debug_msg(cdls, subsys, mask, file, fn, line, format, a...) \
        spl_debug_vmsg(cdls, subsys, mask, file, fn,                    \
                       line, NULL, NULL, format, ##a)

extern int spl_debug_vmsg(spl_debug_limit_state_t *cdls, int subsys, int mask,
                          const char *file, const char *fn, const int line,
                          const char *format1, va_list args, const char *format2, ...);

extern unsigned long spl_debug_set_mask(unsigned long mask);
extern unsigned long spl_debug_get_mask(void);
extern unsigned long spl_debug_set_subsys(unsigned long mask);
extern unsigned long spl_debug_get_subsys(void);
extern int spl_debug_set_mb(int mb);
extern int spl_debug_get_mb(void);

extern int spl_debug_dumplog(int flags);
extern void spl_debug_dumpstack(struct task_struct *tsk);
extern void spl_debug_bug(char *file, const char *func, const int line, int flags);

extern int spl_debug_clear_buffer(void);
extern int spl_debug_mark_buffer(char *text);

#ifdef  __cplusplus
}
#endif

#endif /* SPL_DEBUG_H */
