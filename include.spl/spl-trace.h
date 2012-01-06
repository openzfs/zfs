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

#ifndef _SPL_TRACE_H
#define _SPL_TRACE_H

#define TCD_MAX_PAGES			(5 << (20 - PAGE_SHIFT))
#define TCD_STOCK_PAGES			(TCD_MAX_PAGES)
#define TRACE_CONSOLE_BUFFER_SIZE	1024

#define SPL_DEFAULT_MAX_DELAY		(600 * HZ)
#define SPL_DEFAULT_MIN_DELAY		((HZ + 1) / 2)
#define SPL_DEFAULT_BACKOFF		2

#define DL_NOTHREAD			0x0001 /* Do not create a new thread */
#define DL_SINGLE_CPU			0x0002 /* Collect pages from this CPU*/

typedef struct dumplog_priv {
	wait_queue_head_t	dp_waitq;
	pid_t			dp_pid;
	int			dp_flags;
	atomic_t		dp_done;
} dumplog_priv_t;

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
		struct list_head	tcd_pages;
		/* number of pages on ->tcd_pages */
		unsigned long	tcd_cur_pages;
		/* Max number of pages allowed on ->tcd_pages */
		unsigned long	tcd_max_pages;

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
		struct list_head	tcd_stock_pages;
		/* number of pages on ->tcd_stock_pages */
		unsigned long	tcd_cur_stock_pages;

		unsigned short	tcd_shutting_down;
		unsigned short	tcd_cpu;
		unsigned short	tcd_type;
		/* The factors to share debug memory. */
		unsigned short	tcd_pages_factor;

		/*
		 * This spinlock is needed to workaround the problem of
		 * set_cpus_allowed() being GPL-only. Since we cannot
		 * schedule a thread on a specific CPU when dumping the
		 * pages, we must use the spinlock for mutual exclusion.
		 */
		spinlock_t	tcd_lock;
		unsigned long	tcd_lock_flags;
	} tcd;
	char __pad[L1_CACHE_ALIGN(sizeof(struct trace_cpu_data))];
};

extern union trace_data_union (*trace_data[TCD_TYPE_MAX])[NR_CPUS];

#define tcd_for_each(tcd, i, j)						\
    for (i = 0; i < TCD_TYPE_MAX && trace_data[i]; i++)			\
	for (j = 0, ((tcd) = &(*trace_data[i])[j].tcd);			\
	     j < num_possible_cpus(); j++, (tcd) = &(*trace_data[i])[j].tcd)

#define tcd_for_each_type_lock(tcd, i, cpu)				\
    for (i = 0; i < TCD_TYPE_MAX && trace_data[i] &&			\
	 (tcd = &(*trace_data[i])[cpu].tcd) &&				\
	 trace_lock_tcd(tcd); trace_unlock_tcd(tcd), i++)

struct trace_page {
	struct page	*page;    /* page itself */
	struct list_head linkage;  /* Used by trace_data_union */
	unsigned int	used;     /* number of bytes used within this page */
	unsigned short	cpu;      /* cpu that owns this page */
	unsigned short	type;     /* type(context) of this page */
};

struct page_collection {
	struct list_head pc_pages;
	spinlock_t	pc_lock;
	int		pc_want_daemon_pages;
};

#endif /* SPL_TRACE_H */
