/* SPDX-License-Identifier: CDDL-1.0 */
/*
 * Shared with spl_uio.c so the pin array can be sized and indexed.
 * UVM calls stay in spl_uvm_pin.c.  Pointers only: this header does
 * not include UVM, and struct krwlock is NetBSD's krwlock_t.
 */
#ifndef _SPL_UVM_PIN_H_
#define	_SPL_UVM_PIN_H_

struct vm_anon;
struct vm_page;
struct vm_map;
struct vmspace;
struct krwlock;

struct zfs_nb_pin {
	struct vm_anon	*np_anon;
	struct vm_page	*np_pg;
	struct krwlock	*np_lock;	/* == np_anon->an_lock, held */
};

int zfs_nb_vspin(struct vmspace *, vaddr_t, size_t, struct zfs_nb_pin *);
void zfs_nb_vsunpin(struct zfs_nb_pin *, size_t);
void zfs_nb_dio_read_fallback_add(void);

#endif /* _SPL_UVM_PIN_H_ */
