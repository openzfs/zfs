// SPDX-License-Identifier: CDDL-1.0
/*
 * UVM page pin for Direct I/O reads.
 *
 * Direct I/O reads must not uvm_loan().  A loan can hand back the
 * shared zero page, another file's cache page, or an anon still
 * shared with a forked child.  Writing the read into that page
 * corrupts memory that is not the caller's.
 *
 * zfs_nb_vspin() is the in-module stand-in for a UVM pin: write-fault
 * the range, then under the map read lock and the amap write lock
 * (which is the anon lock) require a private resident anon and
 * uvm_pagewire() it.  An extra anon reference plus rw_obj_hold() on
 * its lock keep the page alive across munmap.  MAP_SHARED and aobj
 * mappings return EOPNOTSUPP so the caller can fall back to uncached
 * ARC I/O.  Ordinary heap buffers are copy-on-write anons; that is
 * what the ZTS direct tests use.
 *
 * Field sizes are asserted so a UVM rename fails the module build
 * instead of the data.  uvm_pagemarkdirty(DIRTY) is enough to keep
 * a pinned anon coherent with swap: the pagedaemon writes a dirty
 * anon (reusing its old slot) before freeing the page.  The module
 * does not look up uvm_anon_dropswap and does not need ksyms.
 */

#include <sys/param.h>
#include <sys/types.h>
#include <sys/systm.h>
#include <sys/proc.h>
#include <sys/kmem.h>
#include <sys/atomic.h>
#include <sys/kstat.h>
#include <sys/cpu.h>
#include <uvm/uvm.h>
#include <uvm/uvm_amap.h>
#include <uvm/uvm_anon.h>
#include <uvm/uvm_extern.h>
#include <uvm/uvm_pmap.h>
#include "spl_uvm_pin.h"

CTASSERT(sizeof (((struct vm_anon *)0)->an_ref) == sizeof (uintptr_t));
CTASSERT(sizeof (((struct vm_anon *)0)->an_lock) == sizeof (krwlock_t *));
CTASSERT(sizeof (((struct vm_anon *)0)->an_page) == sizeof (struct vm_page *));
CTASSERT(sizeof (((struct vm_amap *)0)->am_lock) == sizeof (krwlock_t *));
CTASSERT(sizeof (((struct vm_page *)0)->wire_count) > 0);
CTASSERT(sizeof (((struct vm_page *)0)->loan_count) > 0);

/* struct zfs_nb_pin is in spl_uvm_pin.h */

uint64_t zfs_nb_dio_read_fallback;

static kstat_t *zfs_nb_pin_ksp;
static kstat_named_t zfs_nb_pin_named;

void zfs_nb_vsunpin(struct zfs_nb_pin *, size_t);

/*
 * Map read-locked.  0, EAGAIN (caller refaults), or EOPNOTSUPP
 * (this mapping cannot be pinned; fall back).
 */
static int
zfs_nb_pin_one(struct vm_map *map, vaddr_t va, struct zfs_nb_pin *p)
{
	struct vm_map_entry *e;
	struct vm_amap *amap;
	struct vm_anon *anon;
	struct vm_page *pg;

	if (!uvm_map_lookup_entry(map, va, &e) || UVM_ET_ISSUBMAP(e) ||
	    (e->protection & VM_PROT_WRITE) == 0 ||
	    !UVM_ET_ISCOPYONWRITE(e))
		return (EOPNOTSUPP);
	if ((amap = e->aref.ar_amap) == NULL || UVM_ET_ISNEEDSCOPY(e))
		return (EAGAIN);

	amap_lock(amap, RW_WRITER);
	anon = amap_lookup(&e->aref, va - e->start);
	if (anon == NULL || (pg = anon->an_page) == NULL ||
	    anon->an_ref != 1 || pg->loan_count != 0 || pg->uobject != NULL ||
	    (pg->flags & (PG_BUSY | PG_RELEASED | PG_FAKE)) != 0) {
		amap_unlock(amap);
		return (EAGAIN);
	}
	uvm_pagemarkdirty(pg, UVM_PAGE_STATUS_DIRTY);
	uvm_pagelock(pg);
	uvm_pagewire(pg);
	uvm_pageunlock(pg);
	anon->an_ref++;
	rw_obj_hold(anon->an_lock);
	p->np_anon = anon;
	p->np_pg = pg;
	p->np_lock = anon->an_lock;
	amap_unlock(amap);
	return (0);
}

int
zfs_nb_vspin(struct vmspace *vm, vaddr_t va, size_t npg, struct zfs_nb_pin *p)
{
	struct vm_map *map = &vm->vm_map;
	size_t i, done = 0;
	int error = 0, tries = 0;

	if (npg == 0)
		return (0);
	/*
	 * wiredmax == 0 has been seen on kernels that do not publish
	 * a limit through this field.  Do not treat that as "already
	 * over the limit" or every direct read falls back.
	 */
	if (uvmexp.wiredmax > 0 &&
	    (uvmexp.wired >= uvmexp.wiredmax ||
	    npg > (size_t)(uvmexp.wiredmax - uvmexp.wired)))
		return (EOPNOTSUPP);
again:
	for (i = done; i < npg; i++) {
		if (uvm_fault(map, va + ptoa(i), VM_PROT_WRITE) != 0) {
			error = EFAULT;
			goto fail;
		}
	}
	vm_map_lock_read(map);
	for (i = done; i < npg; i++) {
		error = zfs_nb_pin_one(map, va + ptoa(i), &p[i]);
		if (error != 0)
			break;
	}
	vm_map_unlock_read(map);
	done = i;
	if (error == EAGAIN && ++tries < 4)
		goto again;
	if (error == 0)
		return (0);
fail:
	zfs_nb_vsunpin(p, done);
	return (error == EAGAIN ? EOPNOTSUPP : error);
}

void
zfs_nb_vsunpin(struct zfs_nb_pin *p, size_t n)
{
	size_t i;

	for (i = 0; i < n; i++) {
		krwlock_t *lk = p[i].np_lock;

		if (p[i].np_anon == NULL)
			continue;
		rw_enter(lk, RW_WRITER);
		uvm_pagemarkdirty(p[i].np_pg, UVM_PAGE_STATUS_DIRTY);
		uvm_pagelock(p[i].np_pg);
		uvm_pageunwire(p[i].np_pg);
		uvm_pageunlock(p[i].np_pg);
		if (--p[i].np_anon->an_ref == 0)
			uvm_anfree(p[i].np_anon);
		rw_exit(lk);
		rw_obj_free(lk);
		p[i].np_anon = NULL;
		p[i].np_pg = NULL;
		p[i].np_lock = NULL;
	}
}

int
spl_uvm_pin_init(void)
{
	(void) strlcpy(zfs_nb_pin_named.name, "dio_read_fallback",
	    sizeof (zfs_nb_pin_named.name));
	zfs_nb_pin_named.data_type = KSTAT_DATA_UINT64;
	zfs_nb_pin_ksp = kstat_create("zfs", 0, "nb_pin", "misc",
	    KSTAT_TYPE_NAMED, 1, 0);
	if (zfs_nb_pin_ksp != NULL) {
		zfs_nb_pin_ksp->ks_data = &zfs_nb_pin_named;
		kstat_install(zfs_nb_pin_ksp);
	}
	return (0);
}

void
spl_uvm_pin_fini(void)
{
	if (zfs_nb_pin_ksp != NULL) {
		kstat_delete(zfs_nb_pin_ksp);
		zfs_nb_pin_ksp = NULL;
	}
}

void
zfs_nb_dio_read_fallback_add(void)
{
	atomic_inc_64(&zfs_nb_dio_read_fallback);
	atomic_inc_64(&zfs_nb_pin_named.value.ui64);
}
