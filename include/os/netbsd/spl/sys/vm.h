// SPDX-License-Identifier: CDDL-1.0
#ifndef _OPENSOLARIS_SYS_VM_H_
#define	_OPENSOLARIS_SYS_VM_H_

#ifdef _KERNEL

#include <uvm/uvm_extern.h>
#include <uvm/uvm_object.h>
#include <uvm/uvm_page.h>
#include <uvm/uvm_pmap.h>
#include <vm/vm_page.h>
#include <sys/cpu.h>
#include <sys/kmem.h>

typedef struct vm_page *vm_page_t;
typedef struct uvm_object *vm_object_t;

struct sf_buf {
	vaddr_t	sf_va;
};

extern const int zfs_vm_pagerret_bad;
extern const int zfs_vm_pagerret_error;
extern const int zfs_vm_pagerret_ok;
extern const int zfs_vm_pagerret_pend;
extern const int zfs_vm_pagerput_sync;
extern const int zfs_vm_pagerput_inval;

void	zfs_vmobject_assert_wlocked(vm_object_t object);
void	zfs_vmobject_wlock(vm_object_t object);
void	zfs_vmobject_wunlock(vm_object_t object);

/*
 * Map a page into KVA.  Direct-map arches do not sleep.
 * Without PMAP_DIRECT_MAP this allocates VA (KM_SLEEP /
 * UVM_KMF_WAITVA) and may sleep -- illegal after fpu_kern_enter
 * (IPL_VM).  amd64 kmods get __HAVE_DIRECT_MAP from ccompile.h.
 *
 * Call sites: abd_alloc_from_pages and abd_iter_map (thread
 * context).  The native pager uses uvm_pagermapin, not this
 * helper.  Do not call from a spin mutex or interrupt.
 */
static inline caddr_t
zfs_map_page(vm_page_t pp, struct sf_buf **sfp)
{
#ifdef PMAP_DIRECT_MAP
	/*
	 * Swap on zvols is only reliable here.  Architectures
	 * without PMAP_DIRECT_MAP must sleep to get KVA, and
	 * pager I/O can run where sleeping is forbidden.  Do
	 * not claim swap-on-zvol on those arches until we have
	 * a non-sleeping map API.
	 */
	*sfp = NULL;
	return ((caddr_t)(uintptr_t)PMAP_DIRECT_MAP(VM_PAGE_TO_PHYS(pp)));
#else
	/*
	 * No PMAP_DIRECT_MAP: cannot reliably support swap on
	 * zvols.  This path sleeps.
	 */
	struct sf_buf *sf;
	vaddr_t va;

	KASSERT(!cpu_intr_p());
	KASSERT(!cpu_softintr_p());
	sf = kmem_alloc(sizeof (*sf), KM_SLEEP);
	va = uvm_km_alloc(kernel_map, PAGE_SIZE, 0,
	    UVM_KMF_VAONLY | UVM_KMF_WAITVA);
	if (va == 0)
		panic("zfs_map_page: uvm_km_alloc failed");
	pmap_kenter_pa(va, VM_PAGE_TO_PHYS(pp),
	    VM_PROT_READ | VM_PROT_WRITE, 0);
	pmap_update(pmap_kernel());
	sf->sf_va = va;
	*sfp = sf;
	return ((caddr_t)va);
#endif
}

static inline void
zfs_unmap_page(struct sf_buf *sf)
{
#ifdef PMAP_DIRECT_MAP
	(void) sf;
#else
	if (sf == NULL)
		return;
	pmap_kremove(sf->sf_va, PAGE_SIZE);
	pmap_update(pmap_kernel());
	uvm_km_free(kernel_map, sf->sf_va, PAGE_SIZE, UVM_KMF_VAONLY);
	kmem_free(sf, sizeof (*sf));
#endif
}

#endif /* _KERNEL */

#endif	/* _OPENSOLARIS_SYS_VM_H_ */
