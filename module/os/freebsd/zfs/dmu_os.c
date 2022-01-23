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

int
dmu_write_pages(objset_t *os, uint64_t object, uint64_t offset, uint64_t size,
    vm_page_t *ma, dmu_tx_t *tx)
{
	dmu_buf_t **dbp;
	struct sf_buf *sf;
	int numbufs, i;
	int err;

	if (size == 0)
		return (0);

	err = dmu_buf_hold_array(os, object, offset, size,
	    FALSE, FTAG, &numbufs, &dbp);
	if (err)
		return (err);

	for (i = 0; i < numbufs; i++) {
		int tocpy, copied, thiscpy;
		int bufoff;
		dmu_buf_t *db = dbp[i];
		caddr_t va;

		ASSERT3U(size, >, 0);
		ASSERT3U(db->db_size, >=, PAGESIZE);

		bufoff = offset - db->db_offset;
		tocpy = (int)MIN(db->db_size - bufoff, size);

		ASSERT(i == 0 || i == numbufs-1 || tocpy == db->db_size);

		if (tocpy == db->db_size)
			dmu_buf_will_fill(db, tx);
		else
			dmu_buf_will_dirty(db, tx);

		for (copied = 0; copied < tocpy; copied += PAGESIZE) {
			ASSERT3U(ptoa((*ma)->pindex), ==,
			    db->db_offset + bufoff);
			thiscpy = MIN(PAGESIZE, tocpy - copied);
			va = zfs_map_page(*ma, &sf);
			bcopy(va, (char *)db->db_data + bufoff, thiscpy);
			zfs_unmap_page(sf);
			ma += 1;
			bufoff += PAGESIZE;
		}

		if (tocpy == db->db_size)
			dmu_buf_fill_done(db, tx);

		offset += tocpy;
		size -= tocpy;
	}
	dmu_buf_rele_array(dbp, numbufs, FTAG);
	return (err);
}

int
dmu_read_pages(objset_t *os, uint64_t object, vm_page_t *ma, int count,
    int *rbehind, int *rahead, int last_size)
{
	struct sf_buf *sf;
	vm_object_t vmobj;
	vm_page_t m;
	dmu_buf_t **dbp;
	dmu_buf_t *db;
	caddr_t va;
	int numbufs, i;
	int bufoff, pgoff, tocpy;
	int mi, di;
	int err;

	ASSERT3U(ma[0]->pindex + count - 1, ==, ma[count - 1]->pindex);
	ASSERT3S(last_size, <=, PAGE_SIZE);

	err = dmu_buf_hold_array(os, object, IDX_TO_OFF(ma[0]->pindex),
	    IDX_TO_OFF(count - 1) + last_size, TRUE, FTAG, &numbufs, &dbp);
	if (err != 0)
		return (err);

#ifdef ZFS_DEBUG
	IMPLY(last_size < PAGE_SIZE, *rahead == 0);
	if (dbp[0]->db_offset != 0 || numbufs > 1) {
		for (i = 0; i < numbufs; i++) {
			ASSERT(ISP2(dbp[i]->db_size));
			ASSERT3U((dbp[i]->db_offset % dbp[i]->db_size), ==, 0);
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
		ASSERT3U(m->dirty, ==, 0);
		ASSERT(!pmap_page_is_write_mapped(m));

		ASSERT3U(db->db_size, >, PAGE_SIZE);
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
				ASSERT3U(m->dirty, ==, 0);
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
		ASSERT3S(tocpy, >=, 0);
		if (m != bogus_page)
			bcopy((char *)db->db_data + bufoff, va + pgoff, tocpy);

		pgoff += tocpy;
		ASSERT3S(pgoff, >=, 0);
		ASSERT3S(pgoff, <=, PAGESIZE);
		if (pgoff == PAGESIZE) {
			if (m != bogus_page) {
				zfs_unmap_page(sf);
				vm_page_valid(m);
			}
			ASSERT3S(mi, <, count);
			mi++;
			pgoff = 0;
		}

		bufoff += tocpy;
		ASSERT3S(bufoff, >=, 0);
		ASSERT3S(bufoff, <=, db->db_size);
		if (bufoff == db->db_size) {
			ASSERT3S(di, <, numbufs);
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
		ASSERT3S(di, >=, numbufs - 1);
		IMPLY(*rahead != 0, di == numbufs - 1);
		IMPLY(*rahead != 0, bufoff != 0);
		ASSERT0(pgoff);
	}
	if (di == numbufs) {
		ASSERT3S(mi, >=, count - 1);
		ASSERT0(*rahead);
		IMPLY(pgoff == 0, mi == count);
		if (pgoff != 0) {
			ASSERT3S(mi, ==, count - 1);
			ASSERT3U((dbp[0]->db_size & PAGE_MASK), !=, 0);
		}
	}
#endif
	if (pgoff != 0) {
		ASSERT3P(m, !=, bogus_page);
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
		ASSERT3U(m->dirty, ==, 0);
		ASSERT(!pmap_page_is_write_mapped(m));

		ASSERT3U(db->db_size, >, PAGE_SIZE);
		bufoff = IDX_TO_OFF(m->pindex) % db->db_size;
		tocpy = MIN(db->db_size - bufoff, PAGESIZE);
		va = zfs_map_page(m, &sf);
		bcopy((char *)db->db_data + bufoff, va, tocpy);
		if (tocpy < PAGESIZE) {
			ASSERT3S(i, ==, *rahead - 1);
			ASSERT3U((db->db_size & PAGE_MASK), !=, 0);
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

	dmu_buf_rele_array(dbp, numbufs, FTAG);
	return (0);
}
