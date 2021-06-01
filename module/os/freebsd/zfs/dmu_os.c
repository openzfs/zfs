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

#include <sys/types.h>
#include <sys/param.h>
#include <sys/dmu.h>
#include <sys/dmu_impl.h>
#include <sys/dmu_tx.h>
#include <sys/dbuf.h>
#include <sys/dnode.h>
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_traverse.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_synctask.h>
#include <sys/dsl_prop.h>
#include <sys/dmu_zfetch.h>
#include <sys/zfs_ioctl.h>
#include <sys/zap.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/sa.h>
#include <sys/zfeature.h>
#include <sys/abd.h>
#include <sys/zfs_rlock.h>
#include <sys/racct.h>
#include <sys/vm.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>

#include <sys/ccompat.h>


#ifdef ZFS_DEBUG
#undef AT_UID
#undef AT_GID
#include <sys/linker.h>
#include <sys/sbuf.h>

static int context_dump_enable;

static int
sysctl_vfs_zfs_dump_dmu_thread_contexts(SYSCTL_HANDLER_ARGS)
{
	int val, err, count;
	dmu_cb_state_t *dcs;
	dmu_buf_ctx_node_t *dbsn;
	struct sbuf *sb;
	taskq_t *tqlast;
	char namebuf[64];
	long offset;


	if (context_dump_enable == 0)
		return (0);
	val = 0;
	err = sysctl_wire_old_buffer(req, 0);
	if (err != 0)
		return (err);
	sb = sbuf_new_for_sysctl(NULL, NULL, 80, req);
	if (sb == NULL)
		return (ENOMEM);

	tqlast = NULL;
	count = 0;
	mutex_enter(&dmu_contexts_lock);
	for (dcs = list_head(dmu_contexts_list); dcs != NULL;
	    dcs = list_next(dmu_contexts_list, dcs)) {
		/*
		 * Ensure that none of the threads are running
		 */
		if (dcs->dcs_tq != tqlast) {
			taskqueue_block(dcs->dcs_tq->tq_queue);
			if (tqlast != NULL)
				taskqueue_unblock(tqlast->tq_queue);
			tqlast = dcs->dcs_tq;
		}
		count++;
		for (dbsn = list_head(&dcs->dcs_io_list); dbsn != NULL;
		    dbsn = list_next(&dcs->dcs_io_list, dbsn)) {
			if (linker_ddb_search_symbol_name(
			    (caddr_t)dbsn->dbsn_cb, namebuf, sizeof (namebuf),
			    &offset) == 0) {
				sbuf_printf(sb, "\ntid: %d func: %s type: %d",
				    dcs->dcs_thread->td_tid, namebuf,
				    dbsn->dbsn_type);
			} else {
				sbuf_printf(sb, "\ntid: %d func: %p type: %d",
				    dcs->dcs_thread->td_tid, dbsn->dbsn_cb,
				    dbsn->dbsn_type);
			}
		}
	}
	mutex_exit(&dmu_contexts_lock);
	if (tqlast != NULL)
		taskqueue_unblock(tqlast->tq_queue);

	sbuf_printf(sb, "\n\tTotal contexts: %d", count);
	err = sbuf_finish(sb);
	sbuf_delete(sb);
	return (err);
}
/* BEGIN CSTYLED */
SYSCTL_DECL(_vfs_zfs_dmu);
SYSCTL_INT(_vfs_zfs_dmu, OID_AUTO, context_dump_enable, CTLFLAG_RW,
    &context_dump_enable, 0, "enable context dump");
SYSCTL_PROC(_vfs_zfs_dmu, OID_AUTO, dump_dmu_thread_contexts,
    CTLTYPE_STRING | CTLFLAG_MPSAFE | CTLFLAG_RD, 0, 0,
    sysctl_vfs_zfs_dump_dmu_thread_contexts, "A",
    "Dump dmu thread contexts");
/* END CSTYLED */
#endif

#ifndef IDX_TO_OFF
#define	IDX_TO_OFF(idx) (((vm_ooffset_t)(idx)) << PAGE_SHIFT)
#endif

#if  __FreeBSD_version < 1300051
#define	VM_ALLOC_BUSY_FLAGS VM_ALLOC_NOBUSY
#else
#define	VM_ALLOC_BUSY_FLAGS  VM_ALLOC_SBUSY | VM_ALLOC_IGN_SBUSY
#endif


#if __FreeBSD_version < 1300072
#define	dmu_page_lock(m)	vm_page_lock(m)
#define	dmu_page_unlock(m)	vm_page_unlock(m)
#else
#define	dmu_page_lock(m)
#define	dmu_page_unlock(m)
#endif

uint64_t
dmu_buf_write_pages(dmu_buf_set_t *dbs, dmu_buf_t *db, uint64_t off,
    uint64_t sz)
{
	vm_page_t *pp = dbs->dbs_dc->dc_data_buf;
	struct sf_buf *sf;
	int copied;

	/*
	 * Seek to the page that starts this transfer.
	 */
	pp += (db->db_offset	+ off - dbs->dbs_dc->dc_dn_start) / PAGESIZE;
	for (copied = 0; copied < sz; copied += PAGESIZE) {
		caddr_t va;
		int thiscpy;

		ASSERT3U(ptoa((*pp)->pindex), ==, db->db_offset + off);
		thiscpy = MIN(PAGESIZE, sz - copied);
		va = zfs_map_page(*pp, &sf);
		bcopy(va, (char *)db->db_data + off, thiscpy);
		zfs_unmap_page(sf);
		pp += 1;
		off += PAGESIZE;
	}
	return (sz);
}

typedef struct dmu_read_pages_ctx {
	dmu_ctx_t dc;
	int *rahead;
	int *rbehind;
	int count;
} dmu_read_pages_ctx_t;

int
dmu_write_pages(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    vm_page_t *ma, dmu_tx_t *tx)
{
	dmu_ctx_t dc;
	int err;

	if (size == 0)
		return (0);

	err = dmu_ctx_init(&dc, /* dnode */ NULL, os, object, offset,
	    size, ma, FTAG, DMU_CTX_FLAG_SUN_PAGES);
	if (err)
		return (err);

	dmu_ctx_set_dmu_tx(&dc, tx);
	err = dmu_issue(&dc);
	dmu_ctx_rele(&dc);
	return (err);
}

static void
dmu_read_pages_buf_set_transfer(dmu_buf_set_t *dbs)
{
	struct sf_buf *sf;
	vm_object_t vmobj;
	vm_page_t m, *ma;
	dmu_buf_t **dbp;
	dmu_buf_t *db;
	caddr_t va;
	int numbufs, i;
	int bufoff, pgoff, tocpy;
	int mi, di, count;
	int *rahead, *rbehind;
	dmu_read_pages_ctx_t *drpc;

	ma = (vm_page_t *)dbs->dbs_dc->dc_data_buf;
	drpc = (dmu_read_pages_ctx_t *)dbs->dbs_dc;
	rahead = drpc->rahead;
	rbehind = drpc->rbehind;
	count = drpc->count;
	numbufs = dbs->dbs_count;
	dbp = dbs->dbs_dbp;

#ifdef ZFS_DEBUG
	if (dbp[0]->db_offset != 0 || numbufs > 1) {
		for (i = 0; i < numbufs; i++) {
			ASSERT(ISP2(dbp[i]->db_size));
			ASSERT((dbp[i]->db_offset % dbp[i]->db_size) == 0);
			ASSERT3U(dbp[i]->db_size, ==, dbp[0]->db_size);
		}
	}
#endif

	vmobj = ma[0]->object;
	zfs_vmobject_wlock_12(vmobj);

	db = dbp[0];
	for (i = 0; i < *rbehind; i++) {
		m = vm_page_grab_unlocked(vmobj, ma[0]->pindex - 1 - i,
		    VM_ALLOC_NORMAL | VM_ALLOC_NOWAIT | VM_ALLOC_BUSY_FLAGS);
		if (m == NULL)
			break;
		if (!vm_page_none_valid(m)) {
			ASSERT3U(m->valid, ==, VM_PAGE_BITS_ALL);
			vm_page_do_sunbusy(m);
			break;
		}
		ASSERT(m->dirty == 0);
		ASSERT(!pmap_page_is_write_mapped(m));

		ASSERT(db->db_size > PAGE_SIZE);
		bufoff = IDX_TO_OFF(m->pindex) % db->db_size;
		va = zfs_map_page(m, &sf);
		bcopy((char *)db->db_data + bufoff, va, PAGESIZE);
		zfs_unmap_page(sf);
		vm_page_valid(m);
		dmu_page_lock(m);
		if ((m->busy_lock & VPB_BIT_WAITERS) != 0)
			vm_page_activate(m);
		else
			vm_page_deactivate(m);
		dmu_page_unlock(m);
		vm_page_do_sunbusy(m);
	}
	*rbehind = i;

	bufoff = IDX_TO_OFF(ma[0]->pindex) % db->db_size;
	pgoff = 0;
	for (mi = 0, di = 0; mi < count && di < numbufs; ) {
		if (pgoff == 0) {
			m = ma[mi];
			if (m != bogus_page) {
				vm_page_assert_xbusied(m);
				ASSERT(vm_page_none_valid(m));
				ASSERT(m->dirty == 0);
				ASSERT(!pmap_page_is_write_mapped(m));
				va = zfs_map_page(m, &sf);
			}
		}
		if (bufoff == 0)
			db = dbp[di];

		if (m != bogus_page) {
			ASSERT3U(IDX_TO_OFF(m->pindex) + pgoff, ==,
			    db->db_offset + bufoff);
		}

		/*
		 * We do not need to clamp the copy size by the file
		 * size as the last block is zero-filled beyond the
		 * end of file anyway.
		 */
		tocpy = MIN(db->db_size - bufoff, PAGESIZE - pgoff);
		if (m != bogus_page)
			bcopy((char *)db->db_data + bufoff, va + pgoff, tocpy);

		pgoff += tocpy;
		ASSERT(pgoff <= PAGESIZE);
		if (pgoff == PAGESIZE) {
			if (m != bogus_page) {
				zfs_unmap_page(sf);
				vm_page_valid(m);
			}
			ASSERT(mi < count);
			mi++;
			pgoff = 0;
		}

		bufoff += tocpy;
		ASSERT(bufoff <= db->db_size);
		if (bufoff == db->db_size) {
			ASSERT(di < numbufs);
			di++;
			bufoff = 0;
		}
	}

#ifdef ZFS_DEBUG
	/*
	 * Three possibilities:
	 * - last requested page ends at a buffer boundary and , thus,
	 *   all pages and buffers have been iterated;
	 * - all requested pages are filled, but the last buffer
	 *   has not been exhausted;
	 *   the read-ahead is possible only in this case;
	 * - all buffers have been read, but the last page has not been
	 *   fully filled;
	 *   this is only possible if the file has only a single buffer
	 *   with a size that is not a multiple of the page size.
	 */
	if (mi == count) {
		ASSERT(di >= numbufs - 1);
		IMPLY(*rahead != 0, di == numbufs - 1);
		IMPLY(*rahead != 0, bufoff != 0);
		ASSERT(pgoff == 0);
	}
	if (di == numbufs) {
		ASSERT(mi >= count - 1);
		ASSERT(*rahead == 0);
		IMPLY(pgoff == 0, mi == count);
		if (pgoff != 0) {
			ASSERT(mi == count - 1);
			ASSERT((dbp[0]->db_size & PAGE_MASK) != 0);
		}
	}
#endif
	if (pgoff != 0) {
		ASSERT(m != bogus_page);
		bzero(va + pgoff, PAGESIZE - pgoff);
		zfs_unmap_page(sf);
		vm_page_valid(m);
	}

	for (i = 0; i < *rahead; i++) {
		m = vm_page_grab_unlocked(vmobj, ma[count - 1]->pindex + 1 + i,
		    VM_ALLOC_NORMAL | VM_ALLOC_NOWAIT | VM_ALLOC_BUSY_FLAGS);
		if (m == NULL)
			break;
		if (!vm_page_none_valid(m)) {
			ASSERT3U(m->valid, ==, VM_PAGE_BITS_ALL);
			vm_page_do_sunbusy(m);
			break;
		}
		ASSERT(m->dirty == 0);
		ASSERT(!pmap_page_is_write_mapped(m));

		ASSERT(db->db_size > PAGE_SIZE);
		bufoff = IDX_TO_OFF(m->pindex) % db->db_size;
		tocpy = MIN(db->db_size - bufoff, PAGESIZE);
		va = zfs_map_page(m, &sf);
		bcopy((char *)db->db_data + bufoff, va, tocpy);
		if (tocpy < PAGESIZE) {
			ASSERT(i == *rahead - 1);
			ASSERT((db->db_size & PAGE_MASK) != 0);
			bzero(va + tocpy, PAGESIZE - tocpy);
		}
		zfs_unmap_page(sf);
		vm_page_valid(m);
		dmu_page_lock(m);
		if ((m->busy_lock & VPB_BIT_WAITERS) != 0)
			vm_page_activate(m);
		else
			vm_page_deactivate(m);
		dmu_page_unlock(m);
		vm_page_do_sunbusy(m);
	}
	*rahead = i;
	zfs_vmobject_wunlock_12(vmobj);
}

int
dmu_read_pages(objset_t *os, uint64_t object, vm_page_t *ma, int count,
    int *rbehind, int *rahead, int last_size)
{
	dmu_read_pages_ctx_t drpc;
	uint32_t dmu_flags = DMU_CTX_FLAG_READ|DMU_CTX_FLAG_PAGES;
	int err;

	ASSERT3U(ma[0]->pindex + count - 1, ==, ma[count - 1]->pindex);
	ASSERT(last_size <= PAGE_SIZE);
#ifdef ZFS_DEBUG
	IMPLY(last_size < PAGE_SIZE, *rahead == 0);
#endif
	drpc.rbehind = rbehind;
	drpc.rahead = rahead;
	drpc.count = count;
	err = dmu_ctx_init(&drpc.dc, /* dnode */ NULL, os,
	    object, IDX_TO_OFF(ma[0]->pindex), IDX_TO_OFF(count -1) + last_size,
	    ma, FTAG, dmu_flags);
	if (err != 0)
		return (err);
	dmu_ctx_set_buf_set_transfer_cb(&drpc.dc,
	    dmu_read_pages_buf_set_transfer);
	dmu_issue(&drpc.dc);
	dmu_ctx_rele(&drpc.dc);
	return (drpc.dc.dc_err);
}
