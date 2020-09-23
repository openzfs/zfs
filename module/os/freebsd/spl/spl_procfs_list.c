/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <sys/cdefs.h>
__FBSDID("$FreeBSD$");

#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/procfs_list.h>

typedef struct procfs_list_iter {
	procfs_list_t *pli_pl;
	void *pli_elt;
} pli_t;

void
seq_printf(struct seq_file *f, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	(void) vsnprintf(f->sf_buf, f->sf_size, fmt, adx);
	va_end(adx);
}

static int
procfs_list_update(kstat_t *ksp, int rw)
{
	procfs_list_t *pl = ksp->ks_private;

	if (rw == KSTAT_WRITE)
		pl->pl_clear(pl);

	return (0);
}

static int
procfs_list_data(char *buf, size_t size, void *data)
{
	pli_t *p;
	void *elt;
	procfs_list_t *pl;
	struct seq_file f;

	p = data;
	pl = p->pli_pl;
	elt = p->pli_elt;
	free(p, M_TEMP);
	f.sf_buf = buf;
	f.sf_size = size;
	return (pl->pl_show(&f, elt));
}

static void *
procfs_list_addr(kstat_t *ksp, loff_t n)
{
	procfs_list_t *pl = ksp->ks_private;
	void *elt = ksp->ks_private1;
	pli_t *p = NULL;


	if (n == 0)
		ksp->ks_private1 = list_head(&pl->pl_list);
	else if (elt)
		ksp->ks_private1 = list_next(&pl->pl_list, elt);

	if (ksp->ks_private1) {
		p = malloc(sizeof (*p), M_TEMP, M_WAITOK);
		p->pli_pl = pl;
		p->pli_elt = ksp->ks_private1;
	}

	return (p);
}

void
procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off)
{
	kstat_t *procfs_kstat;

	mutex_init(&procfs_list->pl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&procfs_list->pl_list,
	    procfs_list_node_off + sizeof (procfs_list_node_t),
	    procfs_list_node_off + offsetof(procfs_list_node_t, pln_link));
	procfs_list->pl_show = show;
	procfs_list->pl_show_header = show_header;
	procfs_list->pl_clear = clear;
	procfs_list->pl_next_id = 1;
	procfs_list->pl_node_offset = procfs_list_node_off;

	procfs_kstat =  kstat_create(module, 0, name, submodule,
	    KSTAT_TYPE_RAW, 0, KSTAT_FLAG_VIRTUAL);

	if (procfs_kstat) {
		procfs_kstat->ks_lock = &procfs_list->pl_lock;
		procfs_kstat->ks_ndata = UINT32_MAX;
		procfs_kstat->ks_private = procfs_list;
		procfs_kstat->ks_update = procfs_list_update;
		kstat_set_seq_raw_ops(procfs_kstat, show_header,
		    procfs_list_data, procfs_list_addr);
		kstat_install(procfs_kstat);
		procfs_list->pl_private = procfs_kstat;
	}
}

void
procfs_list_uninstall(procfs_list_t *procfs_list)
{}

void
procfs_list_destroy(procfs_list_t *procfs_list)
{
	ASSERT(list_is_empty(&procfs_list->pl_list));
	kstat_delete(procfs_list->pl_private);
	list_destroy(&procfs_list->pl_list);
	mutex_destroy(&procfs_list->pl_lock);
}

#define	NODE_ID(procfs_list, obj) \
		(((procfs_list_node_t *)(((char *)obj) + \
		(procfs_list)->pl_node_offset))->pln_id)

void
procfs_list_add(procfs_list_t *procfs_list, void *p)
{
	ASSERT(MUTEX_HELD(&procfs_list->pl_lock));
	NODE_ID(procfs_list, p) = procfs_list->pl_next_id++;
	list_insert_tail(&procfs_list->pl_list, p);
}
