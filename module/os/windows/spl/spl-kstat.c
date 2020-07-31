/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

/*
 * Provides an implementation of kstat that is backed by whatever windows has ?
 */

#include <sys/kstat.h>
#include <spl-debug.h>
#include <sys/thread.h>
#include <sys/cmn_err.h>
#include <sys/time.h>

/* kstat_fr.c */

/*
* Copyright (c) 1992, 2010, Oracle and/or its affiliates. All rights reserved.
* Copyright 2014, Joyent, Inc. All rights reserved.
* Copyright 2015 Nexenta Systems, Inc. All rights reserved.
*/

/*
* Kernel statistics framework
*/

#include <sys/types.h>
#include <sys/time.h>
#include <sys/systm.h>
#include <sys/vmsystm.h>
#include <sys/t_lock.h>
#include <sys/param.h>
#include <sys/errno.h>
#include <sys/vmem.h>
#include <sys/sysmacros.h>
#include <sys/cmn_err.h>
#include <sys/kstat.h>
//#include <sys/sysinfo.h>
#include <sys/cpuvar.h>
#include <sys/fcntl.h>
//#include <sys/flock.h>
#include <sys/vnode.h>
#include <sys/vfs.h>
#include <sys/dnlc.h>
//#include <sys/var.h>
#include <sys/debug.h>
#include <sys/kobj.h>
#include <sys/avl.h>
//#include <sys/pool_pset.h>
#include <sys/cpupart.h>
#include <sys/zone.h>
//#include <sys/loadavg.h>
//#include <vm/page.h>
#include <vm/anon.h>
#include <vm/seg_kmem.h>

#include <Trace.h>

/*
* Global lock to protect the AVL trees and kstat_chain_id.
*/
static kmutex_t kstat_chain_lock;

/*
* Every install/delete kstat bumps kstat_chain_id.  This is used by:
*
* (1)	/dev/kstat, to detect changes in the kstat chain across ioctls;
*
* (2)	kstat_create(), to assign a KID (kstat ID) to each new kstat.
*	/dev/kstat uses the KID as a cookie for kstat lookups.
*
* We reserve the first two IDs because some kstats are created before
* the well-known ones (kstat_headers = 0, kstat_types = 1).
*
* We also bump the kstat_chain_id if a zone is gaining or losing visibility
* into a particular kstat, which is logically equivalent to a kstat being
* installed/deleted.
*/

kid_t kstat_chain_id = 2;

/*
* As far as zones are concerned, there are 3 types of kstat:
*
* 1) Those which have a well-known name, and which should return per-zone data
* depending on which zone is doing the kstat_read().  sockfs:0:sock_unix_list
* is an example of this type of kstat.
*
* 2) Those which should only be exported to a particular list of zones.
* For example, in the case of nfs:*:mntinfo, we don't want zone A to be
* able to see NFS mounts associated with zone B, while we want the
* global zone to be able to see all mounts on the system.
*
* 3) Those that can be exported to all zones.  Most system-related
* kstats fall within this category.
*
* An ekstat_t thus contains a list of kstats that the zone is to be
* exported to.  The lookup of a name:instance:module thus translates to a
* lookup of name:instance:module:myzone; if the kstat is not exported
* to all zones, and does not have the caller's zoneid explicitly
* enumerated in the list of zones to be exported to, it is the same as
* if the kstat didn't exist.
*
* Writing to kstats is currently disallowed from within a non-global
* zone, although this restriction could be removed in the future.
*/
typedef struct kstat_zone {
	zoneid_t zoneid;
	struct kstat_zone *next;
} kstat_zone_t;

/*
* Extended kstat structure -- for internal use only.
*/
typedef struct ekstat {
	kstat_t		e_ks;		/* the kstat itself */
	size_t		e_size;		/* total allocation size */
	kthread_t	*e_owner;	/* thread holding this kstat */
	kcondvar_t	e_cv;		/* wait for owner == NULL */
	avl_node_t	e_avl_bykid;	/* AVL tree to sort by KID */
	avl_node_t	e_avl_byname;	/* AVL tree to sort by name */
	kstat_zone_t	e_zone;		/* zone to export stats to */
} ekstat_t;

static uint64_t kstat_initial[8192];
static void *kstat_initial_ptr = kstat_initial;
static size_t kstat_initial_avail = sizeof(kstat_initial);
static vmem_t *kstat_arena;

#define	KSTAT_ALIGN	(sizeof (uint64_t))

static avl_tree_t kstat_avl_bykid;
static avl_tree_t kstat_avl_byname;

/*
* Various pointers we need to create kstats at boot time in kstat_init()
*/
extern	kstat_named_t	*segmapcnt_ptr;
extern	uint_t		segmapcnt_ndata;
extern	int		segmap_kstat_update(kstat_t *, int);
extern	kstat_named_t	*biostats_ptr;
extern	uint_t		biostats_ndata;
extern	kstat_named_t	*pollstats_ptr;
extern	uint_t		pollstats_ndata;

extern	int	vac;
extern	uint_t	nproc;
extern	time_t	boot_time;

static struct {
	char    name[KSTAT_STRLEN];
	size_t  size;
	uint_t  min_ndata;
	uint_t  max_ndata;
} kstat_data_type[KSTAT_NUM_TYPES] = {
	{ "raw",                1,                      0,      INT_MAX },
	{ "name=value",         sizeof(kstat_named_t), 0,      INT_MAX },
	{ "interrupt",          sizeof(kstat_intr_t),  1,      1 },
	{ "i/o",                sizeof(kstat_io_t),    1,      1 },
	{ "event_timer",        sizeof(kstat_timer_t), 0,      INT_MAX },
};

static int header_kstat_update(kstat_t *, int);
static int header_kstat_snapshot(kstat_t *, void *, int);

int
kstat_zone_find(kstat_t *k, zoneid_t zoneid)
{
	ekstat_t *e = (ekstat_t *)k;
	kstat_zone_t *kz;

	ASSERT(MUTEX_HELD(&kstat_chain_lock));
	for (kz = &e->e_zone; kz != NULL; kz = kz->next) {
		if (zoneid == ALL_ZONES || kz->zoneid == ALL_ZONES)
			return (1);
		if (zoneid == kz->zoneid)
			return (1);
	}
	return (0);
}

void
kstat_zone_remove(kstat_t *k, zoneid_t zoneid)
{
	ekstat_t *e = (ekstat_t *)k;
	kstat_zone_t *kz, *t = NULL;

	mutex_enter(&kstat_chain_lock);
	if (zoneid == e->e_zone.zoneid) {
		kz = e->e_zone.next;
		ASSERT(kz != NULL);
		e->e_zone.zoneid = kz->zoneid;
		e->e_zone.next = kz->next;
		goto out;
	}
	for (kz = &e->e_zone; kz->next != NULL; kz = kz->next) {
		if (kz->next->zoneid == zoneid) {
			t = kz->next;
			kz->next = t->next;
			break;
		}
	}
	ASSERT(t != NULL);	/* we removed something */
	kz = t;
out:
	kstat_chain_id++;
	mutex_exit(&kstat_chain_lock);
	kmem_free(kz, sizeof(*kz));
}

void
kstat_zone_add(kstat_t *k, zoneid_t zoneid)
{
	ekstat_t *e = (ekstat_t *)k;
	kstat_zone_t *kz;

	kz = kmem_alloc(sizeof(*kz), KM_NOSLEEP);
	if (kz == NULL)
		return;
	mutex_enter(&kstat_chain_lock);
	kz->zoneid = zoneid;
	kz->next = e->e_zone.next;
	e->e_zone.next = kz;
	kstat_chain_id++;
	mutex_exit(&kstat_chain_lock);
}

/*
* Compare the list of zones for the given kstats, returning 0 if they match
* (ie, one list contains ALL_ZONES or both lists contain the same zoneid).
* In practice, this is called indirectly by kstat_hold_byname(), so one of the
* two lists always has one element, and this is an O(n) operation rather than
* O(n^2).
*/
static int
kstat_zone_compare(ekstat_t *e1, ekstat_t *e2)
{
	kstat_zone_t *kz1, *kz2;

	ASSERT(MUTEX_HELD(&kstat_chain_lock));
	for (kz1 = &e1->e_zone; kz1 != NULL; kz1 = kz1->next) {
		for (kz2 = &e2->e_zone; kz2 != NULL; kz2 = kz2->next) {
			if (kz1->zoneid == ALL_ZONES ||
				kz2->zoneid == ALL_ZONES)
				return (0);
			if (kz1->zoneid == kz2->zoneid)
				return (0);
		}
	}
	return (e1->e_zone.zoneid < e2->e_zone.zoneid ? -1 : 1);
}

/*
* Support for keeping kstats sorted in AVL trees for fast lookups.
*/
static int
kstat_compare_bykid(const void *a1, const void *a2)
{
	const kstat_t *k1 = a1;
	const kstat_t *k2 = a2;

	if (k1->ks_kid < k2->ks_kid)
		return (-1);
	if (k1->ks_kid > k2->ks_kid)
		return (1);
	return (kstat_zone_compare((ekstat_t *)k1, (ekstat_t *)k2));
}

static int
kstat_compare_byname(const void *a1, const void *a2)
{
	const kstat_t *k1 = a1;
	const kstat_t *k2 = a2;
	int s;

	s = strcmp(k1->ks_module, k2->ks_module);
	if (s > 0)
		return (1);
	if (s < 0)
		return (-1);

	if (k1->ks_instance < k2->ks_instance)
		return (-1);
	if (k1->ks_instance > k2->ks_instance)
		return (1);

	s = strcmp(k1->ks_name, k2->ks_name);
	if (s > 0)
		return (1);
	if (s < 0)
		return (-1);

	return (kstat_zone_compare((ekstat_t *)k1, (ekstat_t *)k2));
}

static kstat_t *
kstat_hold(avl_tree_t *t, ekstat_t *template)
{
	kstat_t *ksp;
	ekstat_t *e;

	mutex_enter(&kstat_chain_lock);
	for (;;) {
		ksp = avl_find(t, template, NULL);
		if (ksp == NULL)
			break;
		e = (ekstat_t *)ksp;
		if (e->e_owner == NULL) {
			e->e_owner = (void *)curthread;
			break;
		}
		cv_wait(&e->e_cv, &kstat_chain_lock);
	}
	mutex_exit(&kstat_chain_lock);
	return (ksp);
}

void
kstat_rele(kstat_t *ksp)
{
	ekstat_t *e = (ekstat_t *)ksp;

	mutex_enter(&kstat_chain_lock);
	ASSERT(e->e_owner == (void *)curthread);
	e->e_owner = NULL;
	cv_broadcast(&e->e_cv);
	mutex_exit(&kstat_chain_lock);
}

kstat_t *
kstat_hold_bykid(kid_t kid, zoneid_t zoneid)
{
	ekstat_t e;

	e.e_ks.ks_kid = kid;
	e.e_zone.zoneid = zoneid;
	e.e_zone.next = NULL;

	return (kstat_hold(&kstat_avl_bykid, &e));
}

kstat_t *
kstat_hold_byname(const char *ks_module, int ks_instance, const char *ks_name,
	zoneid_t ks_zoneid)
{
	ekstat_t e;

	kstat_set_string(e.e_ks.ks_module, ks_module);
	e.e_ks.ks_instance = ks_instance;
	kstat_set_string(e.e_ks.ks_name, ks_name);
	e.e_zone.zoneid = ks_zoneid;
	e.e_zone.next = NULL;
	return (kstat_hold(&kstat_avl_byname, &e));
}

static ekstat_t *
kstat_alloc(size_t size)
{
	ekstat_t *e = NULL;

	size = P2ROUNDUP(sizeof(ekstat_t) + size, KSTAT_ALIGN);

	if (kstat_arena == NULL) {
		if (size <= kstat_initial_avail) {
			e = kstat_initial_ptr;
			kstat_initial_ptr = (char *)kstat_initial_ptr + size;
			kstat_initial_avail -= size;
		}
	} else {
		e = vmem_alloc(kstat_arena, size, VM_NOSLEEP);
	}

	if (e != NULL) {
		bzero(e, size);
		e->e_size = size;
		cv_init(&e->e_cv, NULL, CV_DEFAULT, NULL);
	}

	return (e);
}

static void
kstat_free(ekstat_t *e)
{
	cv_destroy(&e->e_cv);
	vmem_free(kstat_arena, e, e->e_size);
}

extern vmem_t		*heap_arena;
void *segkmem_alloc(vmem_t *vmp, uint32_t size, int vmflag);
void segkmem_free(vmem_t *vmp, void *inaddr, uint32_t size);

/*
* Create various system kstats.
*/
void
kstat_init(void)
{
	kstat_t *ksp;
	ekstat_t *e;
	avl_tree_t *t = &kstat_avl_bykid;

	/*
	* Set up the kstat vmem arena.
	*/
	kstat_arena = vmem_create("kstat",
		(void *)kstat_initial, sizeof(kstat_initial), KSTAT_ALIGN,
		segkmem_alloc, segkmem_free, heap_arena, 0, VM_SLEEP);

	/*
	* Make initial kstats appear as though they were allocated.
	*/
	for (e = avl_first(t); e != NULL; e = avl_walk(t, e, AVL_AFTER))
		(void) vmem_xalloc(kstat_arena, e->e_size, KSTAT_ALIGN,
			0, 0, e, (char *)e + e->e_size,
			VM_NOSLEEP | VM_BESTFIT | VM_PANIC);

	/*
	* The mother of all kstats.  The first kstat in the system, which
	* always has KID 0, has the headers for all kstats (including itself)
	* as its data.  Thus, the kstat driver does not need any special
	* interface to extract the kstat chain.
	*/
	kstat_chain_id = 0;
	ksp = kstat_create("unix", 0, "kstat_headers", "kstat", KSTAT_TYPE_RAW,
		0, KSTAT_FLAG_VIRTUAL | KSTAT_FLAG_VAR_SIZE);
	if (ksp) {
		ksp->ks_lock = &kstat_chain_lock;
		ksp->ks_update = header_kstat_update;
		ksp->ks_snapshot = header_kstat_snapshot;
		kstat_install(ksp);
	} else {
		panic("cannot create kstat 'kstat_headers'");
	}

	ksp = kstat_create("unix", 0, "kstat_types", "kstat",
		KSTAT_TYPE_NAMED, KSTAT_NUM_TYPES, 0);
	if (ksp) {
		int i;
		kstat_named_t *kn = KSTAT_NAMED_PTR(ksp);

		for (i = 0; i < KSTAT_NUM_TYPES; i++) {
			kstat_named_init(&kn[i], kstat_data_type[i].name,
				KSTAT_DATA_ULONG);
			kn[i].value.ul = i;
		}
		kstat_install(ksp);
	}

}

/*
* Caller of this should ensure that the string pointed by src
* doesn't change while kstat's lock is held. Not doing so defeats
* kstat's snapshot strategy as explained in <sys/kstat.h>
*/
void
kstat_named_setstr(kstat_named_t *knp, const char *src)
{
	if (knp->data_type != KSTAT_DATA_STRING)
		panic("kstat_named_setstr('%p', '%p'): "
			"named kstat is not of type KSTAT_DATA_STRING",
			(void *)knp, (void *)src);

	KSTAT_NAMED_STR_PTR(knp) = (char *)src;
	if (src != NULL)
		KSTAT_NAMED_STR_BUFLEN(knp) = strlen(src) + 1;
	else
		KSTAT_NAMED_STR_BUFLEN(knp) = 0;
}

void
kstat_set_string(char *dst, const char *src)
{
	bzero(dst, KSTAT_STRLEN);
	(void)strncpy(dst, src, KSTAT_STRLEN - 1);
}

void
kstat_named_init(kstat_named_t *knp, const char *name, uchar_t data_type)
{
	kstat_set_string(knp->name, name);
	knp->data_type = data_type;

	if (data_type == KSTAT_DATA_STRING)
		kstat_named_setstr(knp, NULL);
}

void
kstat_timer_init(kstat_timer_t *ktp, const char *name)
{
	kstat_set_string(ktp->name, name);
}

/* ARGSUSED */
static int
default_kstat_update(kstat_t *ksp, int rw)
{
	uint_t i;
	size_t len = 0;
	kstat_named_t *knp;

	/*
	* Named kstats with variable-length long strings have a standard
	* way of determining how much space is needed to hold the snapshot:
	*/
	if (ksp->ks_data != NULL && ksp->ks_type == KSTAT_TYPE_NAMED &&
		(ksp->ks_flags & (KSTAT_FLAG_VAR_SIZE | KSTAT_FLAG_LONGSTRINGS))) {

		/*
		* Add in the space required for the strings
		*/
		knp = KSTAT_NAMED_PTR(ksp);
		for (i = 0; i < ksp->ks_ndata; i++, knp++) {
			if (knp->data_type == KSTAT_DATA_STRING)
				len += KSTAT_NAMED_STR_BUFLEN(knp);
		}
		ksp->ks_data_size =
			ksp->ks_ndata * sizeof(kstat_named_t) + len;
	}
	return (0);
}

static int
default_kstat_snapshot(kstat_t *ksp, void *buf, int rw)
{
	kstat_io_t *kiop;
	hrtime_t cur_time;
	size_t	namedsz;

	ksp->ks_snaptime = cur_time = gethrtime();

	if (rw == KSTAT_WRITE) {
		if (!(ksp->ks_flags & KSTAT_FLAG_WRITABLE))
			return (EACCES);
		bcopy(buf, ksp->ks_data, ksp->ks_data_size);
		return (0);
	}

	/*
	* KSTAT_TYPE_NAMED kstats are defined to have ks_ndata
	* number of kstat_named_t structures, followed by an optional
	* string segment. The ks_data generally holds only the
	* kstat_named_t structures. So we copy it first. The strings,
	* if any, are copied below. For other kstat types, ks_data holds the
	* entire buffer.
	*/

	namedsz = sizeof(kstat_named_t) * ksp->ks_ndata;
	if (ksp->ks_type == KSTAT_TYPE_NAMED && ksp->ks_data_size > namedsz)
		bcopy(ksp->ks_data, buf, namedsz);
	else
		bcopy(ksp->ks_data, buf, ksp->ks_data_size);

	/*
	* Apply kstat type-specific data massaging
	*/
	switch (ksp->ks_type) {

	case KSTAT_TYPE_IO:
		/*
		* Normalize time units and deal with incomplete transactions
		*/
#if 0
		kiop = (kstat_io_t *)buf;

		scalehrtime(&kiop->wtime);
		scalehrtime(&kiop->wlentime);
		scalehrtime(&kiop->wlastupdate);
		scalehrtime(&kiop->rtime);
		scalehrtime(&kiop->rlentime);
		scalehrtime(&kiop->rlastupdate);

		if (kiop->wcnt != 0) {
			/* like kstat_waitq_exit */
			hrtime_t wfix = cur_time - kiop->wlastupdate;
			kiop->wlastupdate = cur_time;
			kiop->wlentime += kiop->wcnt * wfix;
			kiop->wtime += wfix;
		}

		if (kiop->rcnt != 0) {
			/* like kstat_runq_exit */
			hrtime_t rfix = cur_time - kiop->rlastupdate;
			kiop->rlastupdate = cur_time;
			kiop->rlentime += kiop->rcnt * rfix;
			kiop->rtime += rfix;
		}
#endif
		break;

	case KSTAT_TYPE_NAMED:
		/*
		* Massage any long strings in at the end of the buffer
		*/
		if (ksp->ks_data_size > namedsz) {
			uint_t i;
			kstat_named_t *knp = buf;
			char *dst = (char *)(knp + ksp->ks_ndata);
			/*
			* Copy strings and update pointers
			*/
			for (i = 0; i < ksp->ks_ndata; i++, knp++) {
				if (knp->data_type == KSTAT_DATA_STRING &&
					KSTAT_NAMED_STR_PTR(knp) != NULL) {
					bcopy(KSTAT_NAMED_STR_PTR(knp), dst,
						KSTAT_NAMED_STR_BUFLEN(knp));
					KSTAT_NAMED_STR_PTR(knp) = dst;
					dst += KSTAT_NAMED_STR_BUFLEN(knp);
				}
			}
			ASSERT(dst <= ((char *)buf + ksp->ks_data_size));
		}
		break;
	}
	return (0);
}

static int
header_kstat_update(kstat_t *header_ksp, int rw)
{
	int nkstats = 0;
	ekstat_t *e;
	avl_tree_t *t = &kstat_avl_bykid;
	zoneid_t zoneid;

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ASSERT(MUTEX_HELD(&kstat_chain_lock));

	zoneid = getzoneid();
	for (e = avl_first(t); e != NULL; e = avl_walk(t, e, AVL_AFTER)) {
		if (kstat_zone_find((kstat_t *)e, zoneid) &&
			(e->e_ks.ks_flags & KSTAT_FLAG_INVALID) == 0) {
			nkstats++;
		}
	}
	header_ksp->ks_ndata = nkstats;
	header_ksp->ks_data_size = nkstats * sizeof(kstat_t);
	return (0);
}

/*
* Copy out the data section of kstat 0, which consists of the list
* of all kstat headers.  By specification, these headers must be
* copied out in order of increasing KID.
*/
static int
header_kstat_snapshot(kstat_t *header_ksp, void *buf, int rw)
{
	ekstat_t *e;
	avl_tree_t *t = &kstat_avl_bykid;
	zoneid_t zoneid;

	header_ksp->ks_snaptime = gethrtime();

	if (rw == KSTAT_WRITE)
		return (EACCES);

	ASSERT(MUTEX_HELD(&kstat_chain_lock));

	zoneid = getzoneid();
	for (e = avl_first(t); e != NULL; e = avl_walk(t, e, AVL_AFTER)) {
		if (kstat_zone_find((kstat_t *)e, zoneid) &&
			(e->e_ks.ks_flags & KSTAT_FLAG_INVALID) == 0) {
			bcopy(&e->e_ks, buf, sizeof(kstat_t));
			buf = (char *)buf + sizeof(kstat_t);
		}
	}

	return (0);
}

kstat_t *
kstat_create(const char *ks_module, int ks_instance, const char *ks_name,
	const char *ks_class, uchar_t ks_type, uint_t ks_ndata, uchar_t ks_flags)
{
	return (kstat_create_zone(ks_module, ks_instance, ks_name, ks_class,
		ks_type, ks_ndata, ks_flags, ALL_ZONES));
}

/*
* Allocate and initialize a kstat structure.  Or, if a dormant kstat with
* the specified name exists, reactivate it.  Returns a pointer to the kstat
* on success, NULL on failure.  The kstat will not be visible to the
* kstat driver until kstat_install().
*/
kstat_t *
kstat_create_zone(const char *ks_module, int ks_instance, const char *ks_name,
	const char *ks_class, uchar_t ks_type, uint_t ks_ndata, uchar_t ks_flags,
	zoneid_t ks_zoneid)
{
	size_t ks_data_size;
	kstat_t *ksp;
	ekstat_t *e;
	avl_index_t where;
	char namebuf[KSTAT_STRLEN + 16];

	if (avl_numnodes(&kstat_avl_bykid) == 0) {
		avl_create(&kstat_avl_bykid, kstat_compare_bykid,
			sizeof(ekstat_t), offsetof(struct ekstat, e_avl_bykid));

		avl_create(&kstat_avl_byname, kstat_compare_byname,
			sizeof(ekstat_t), offsetof(struct ekstat, e_avl_byname));
	}

	/*
	* If ks_name == NULL, set the ks_name to <module><instance>.
	*/
	if (ks_name == NULL) {
		char buf[KSTAT_STRLEN];
		kstat_set_string(buf, ks_module);
		(void)sprintf(namebuf, "%s%d", buf, ks_instance);
		ks_name = namebuf;
	}

	/*
	* Make sure it's a valid kstat data type
	*/
	if (ks_type >= KSTAT_NUM_TYPES) {
		cmn_err(CE_WARN, "kstat_create('%s', %d, '%s'): "
			"invalid kstat type %d",
			ks_module, ks_instance, ks_name, ks_type);
		return (NULL);
	}

	/*
	* Don't allow persistent virtual kstats -- it makes no sense.
	* ks_data points to garbage when the client goes away.
	*/
	if ((ks_flags & KSTAT_FLAG_PERSISTENT) &&
		(ks_flags & KSTAT_FLAG_VIRTUAL)) {
		cmn_err(CE_WARN, "kstat_create('%s', %d, '%s'): "
			"cannot create persistent virtual kstat",
			ks_module, ks_instance, ks_name);
		return (NULL);
	}

	/*
	* Don't allow variable-size physical kstats, since the framework's
	* memory allocation for physical kstat data is fixed at creation time.
	*/
	if ((ks_flags & KSTAT_FLAG_VAR_SIZE) &&
		!(ks_flags & KSTAT_FLAG_VIRTUAL)) {
		cmn_err(CE_WARN, "kstat_create('%s', %d, '%s'): "
			"cannot create variable-size physical kstat",
			ks_module, ks_instance, ks_name);
		return (NULL);
	}

	/*
	* Make sure the number of data fields is within legal range
	*/
	if (ks_ndata < kstat_data_type[ks_type].min_ndata ||
		ks_ndata > kstat_data_type[ks_type].max_ndata) {
		cmn_err(CE_WARN, "kstat_create('%s', %d, '%s'): "
			"ks_ndata=%d out of range [%d, %d]",
			ks_module, ks_instance, ks_name, (int)ks_ndata,
			kstat_data_type[ks_type].min_ndata,
			kstat_data_type[ks_type].max_ndata);
		return (NULL);
	}

	ks_data_size = kstat_data_type[ks_type].size * ks_ndata;

	/*
	* If the named kstat already exists and is dormant, reactivate it.
	*/
	ksp = kstat_hold_byname(ks_module, ks_instance, ks_name, ks_zoneid);
	if (ksp != NULL) {
		if (!(ksp->ks_flags & KSTAT_FLAG_DORMANT)) {
			/*
			* The named kstat exists but is not dormant --
			* this is a kstat namespace collision.
			*/
			kstat_rele(ksp);
			cmn_err(CE_WARN,
				"kstat_create('%s', %d, '%s'): namespace collision",
				ks_module, ks_instance, ks_name);
			return (NULL);
		}
		if ((strcmp(ksp->ks_class, ks_class) != 0) ||
			(ksp->ks_type != ks_type) ||
			(ksp->ks_ndata != ks_ndata) ||
			(ks_flags & KSTAT_FLAG_VIRTUAL)) {
			/*
			* The name is the same, but the other key parameters
			* differ from those of the dormant kstat -- bogus.
			*/
			kstat_rele(ksp);
			cmn_err(CE_WARN, "kstat_create('%s', %d, '%s'): "
				"invalid reactivation of dormant kstat",
				ks_module, ks_instance, ks_name);
			return (NULL);
		}
		/*
		* Return dormant kstat pointer to caller.  As usual,
		* the kstat is marked invalid until kstat_install().
		*/
		ksp->ks_flags |= KSTAT_FLAG_INVALID;
		kstat_rele(ksp);
		return (ksp);
	}

	/*
	* Allocate memory for the new kstat header and, if this is a physical
	* kstat, the data section.
	*/
	e = kstat_alloc(ks_flags & KSTAT_FLAG_VIRTUAL ? 0 : ks_data_size);
	if (e == NULL) {
		cmn_err(CE_NOTE, "kstat_create('%s', %d, '%s'): "
			"insufficient kernel memory",
			ks_module, ks_instance, ks_name);
		return (NULL);
	}

	/*
	* Initialize as many fields as we can.  The caller may reset
	* ks_lock, ks_update, ks_private, and ks_snapshot as necessary.
	* Creators of virtual kstats may also reset ks_data.  It is
	* also up to the caller to initialize the kstat data section,
	* if necessary.  All initialization must be complete before
	* calling kstat_install().
	*/
	e->e_zone.zoneid = ks_zoneid;
	e->e_zone.next = NULL;

	ksp = &e->e_ks;
	ksp->ks_crtime = gethrtime();
	kstat_set_string(ksp->ks_module, ks_module);
	ksp->ks_instance = ks_instance;
	kstat_set_string(ksp->ks_name, ks_name);
	ksp->ks_type = ks_type;
	kstat_set_string(ksp->ks_class, ks_class);
	ksp->ks_flags = ks_flags | KSTAT_FLAG_INVALID;
	if (ks_flags & KSTAT_FLAG_VIRTUAL)
		ksp->ks_data = NULL;
	else
		ksp->ks_data = (void *)(e + 1);
	ksp->ks_ndata = ks_ndata;
	ksp->ks_data_size = ks_data_size;
	ksp->ks_snaptime = ksp->ks_crtime;
	ksp->ks_update = default_kstat_update;
	ksp->ks_private = NULL;
	ksp->ks_snapshot = default_kstat_snapshot;
	ksp->ks_lock = NULL;

	mutex_enter(&kstat_chain_lock);

	/*
	* Add our kstat to the AVL trees.
	*/
	if (avl_find(&kstat_avl_byname, e, &where) != NULL) {
		mutex_exit(&kstat_chain_lock);
		cmn_err(CE_WARN,
			"kstat_create('%s', %d, '%s'): namespace collision",
			ks_module, ks_instance, ks_name);
		kstat_free(e);
		return (NULL);
	}
	avl_insert(&kstat_avl_byname, e, where);

	/*
	* Loop around until we find an unused KID.
	*/
	do {
		ksp->ks_kid = kstat_chain_id++;
	} while (avl_find(&kstat_avl_bykid, e, &where) != NULL);
	avl_insert(&kstat_avl_bykid, e, where);

	mutex_exit(&kstat_chain_lock);

	return (ksp);
}

/*
* Activate a fully initialized kstat and make it visible to /dev/kstat.
*/
void
kstat_install(kstat_t *ksp)
{
	zoneid_t zoneid = ((ekstat_t *)ksp)->e_zone.zoneid;

	/*
	* If this is a variable-size kstat, it MUST provide kstat data locking
	* to prevent data-size races with kstat readers.
	*/
	if ((ksp->ks_flags & KSTAT_FLAG_VAR_SIZE) && ksp->ks_lock == NULL) {
		panic("kstat_install('%s', %d, '%s'): "
			"cannot create variable-size kstat without data lock",
			ksp->ks_module, ksp->ks_instance, ksp->ks_name);
	}

	if (kstat_hold_bykid(ksp->ks_kid, zoneid) != ksp) {
		cmn_err(CE_WARN, "kstat_install(%p): does not exist",
			(void *)ksp);
		return;
	}

	if (ksp->ks_type == KSTAT_TYPE_NAMED && ksp->ks_data != NULL) {
		uint_t i;
		kstat_named_t *knp = KSTAT_NAMED_PTR(ksp);

		for (i = 0; i < ksp->ks_ndata; i++, knp++) {
			if (knp->data_type == KSTAT_DATA_STRING) {
				ksp->ks_flags |= KSTAT_FLAG_LONGSTRINGS;
				break;
			}
		}
		/*
		* The default snapshot routine does not handle KSTAT_WRITE
		* for long strings.
		*/
		if ((ksp->ks_flags & KSTAT_FLAG_LONGSTRINGS) &&
			(ksp->ks_flags & KSTAT_FLAG_WRITABLE) &&
			(ksp->ks_snapshot == default_kstat_snapshot)) {
			panic("kstat_install('%s', %d, '%s'): "
				"named kstat containing KSTAT_DATA_STRING "
				"is writable but uses default snapshot routine",
				ksp->ks_module, ksp->ks_instance, ksp->ks_name);
		}
	}

	if (ksp->ks_flags & KSTAT_FLAG_DORMANT) {

		/*
		* We are reactivating a dormant kstat.  Initialize the
		* caller's underlying data to the value it had when the
		* kstat went dormant, and mark the kstat as active.
		* Grab the provider's kstat lock if it's not already held.
		*/
		kmutex_t *lp = ksp->ks_lock;
		if (lp != NULL && MUTEX_NOT_HELD(lp)) {
			mutex_enter(lp);
			(void)KSTAT_UPDATE(ksp, KSTAT_WRITE);
			mutex_exit(lp);
		} else {
			(void)KSTAT_UPDATE(ksp, KSTAT_WRITE);
		}
		ksp->ks_flags &= ~KSTAT_FLAG_DORMANT;
	}

	/*
	* Now that the kstat is active, make it visible to the kstat driver.
	* When copying out kstats the count is determined in
	* header_kstat_update() and actually copied into kbuf in
	* header_kstat_snapshot(). kstat_chain_lock is held across the two
	* calls to ensure that this list doesn't change. Thus, we need to
	* also take the lock to ensure that the we don't copy the new kstat
	* in the 2nd pass and overrun the buf.
	*/
	mutex_enter(&kstat_chain_lock);
	ksp->ks_flags &= ~KSTAT_FLAG_INVALID;
	mutex_exit(&kstat_chain_lock);
	kstat_rele(ksp);
}

/*
* Remove a kstat from the system.  Or, if it's a persistent kstat,
* just update the data and mark it as dormant.
*/
void
kstat_delete(kstat_t *ksp)
{
	kmutex_t *lp;
	ekstat_t *e = (ekstat_t *)ksp;
	zoneid_t zoneid;
	kstat_zone_t *kz;

	ASSERT(ksp != NULL);

	if (ksp == NULL)
		return;

	zoneid = e->e_zone.zoneid;

	lp = ksp->ks_lock;

	if (lp != NULL && MUTEX_HELD(lp)) {
		panic("kstat_delete(%p): caller holds data lock %p",
			(void *)ksp, (void *)lp);
	}

	if (kstat_hold_bykid(ksp->ks_kid, zoneid) != ksp) {
		cmn_err(CE_WARN, "kstat_delete(%p): does not exist",
			(void *)ksp);
		return;
	}

	if (ksp->ks_flags & KSTAT_FLAG_PERSISTENT) {
		/*
		* Update the data one last time, so that all activity
		* prior to going dormant has been accounted for.
		*/
		KSTAT_ENTER(ksp);
		(void)KSTAT_UPDATE(ksp, KSTAT_READ);
		KSTAT_EXIT(ksp);

		/*
		* Mark the kstat as dormant and restore caller-modifiable
		* fields to default values, so the kstat is readable during
		* the dormant phase.
		*/
		ksp->ks_flags |= KSTAT_FLAG_DORMANT;
		ksp->ks_lock = NULL;
		ksp->ks_update = default_kstat_update;
		ksp->ks_private = NULL;
		ksp->ks_snapshot = default_kstat_snapshot;
		kstat_rele(ksp);
		return;
	}

	/*
	* Remove the kstat from the framework's AVL trees,
	* free the allocated memory, and increment kstat_chain_id so
	* /dev/kstat clients can detect the event.
	*/
	mutex_enter(&kstat_chain_lock);
	avl_remove(&kstat_avl_bykid, e);
	avl_remove(&kstat_avl_byname, e);
	kstat_chain_id++;
	mutex_exit(&kstat_chain_lock);

	kz = e->e_zone.next;
	while (kz != NULL) {
		kstat_zone_t *t = kz;

		kz = kz->next;
		kmem_free(t, sizeof(*t));
	}
	kstat_rele(ksp);
	kstat_free(e);
}

void
kstat_delete_byname_zone(const char *ks_module, int ks_instance,
	const char *ks_name, zoneid_t ks_zoneid)
{
	kstat_t *ksp;

	ksp = kstat_hold_byname(ks_module, ks_instance, ks_name, ks_zoneid);
	if (ksp != NULL) {
		kstat_rele(ksp);
		kstat_delete(ksp);
	}
}

void
kstat_delete_byname(const char *ks_module, int ks_instance, const char *ks_name)
{
	kstat_delete_byname_zone(ks_module, ks_instance, ks_name, ALL_ZONES);
}

/*
* The sparc V9 versions of these routines can be much cheaper than
* the poor 32-bit compiler can comprehend, so they're in sparcv9_subr.s.
* For simplicity, however, we always feed the C versions to lint.
*/
#if !defined(__sparc) || defined(lint) || defined(__lint)

void
kstat_waitq_enter(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t wcnt;

	new = gethrtime();
	delta = new - kiop->wlastupdate;
	kiop->wlastupdate = new;
	wcnt = kiop->wcnt++;
	if (wcnt != 0) {
		kiop->wlentime += delta * wcnt;
		kiop->wtime += delta;
	}
}

void
kstat_waitq_exit(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t wcnt;

	new = gethrtime();
	delta = new - kiop->wlastupdate;
	kiop->wlastupdate = new;
	wcnt = kiop->wcnt--;
	ASSERT((int)wcnt > 0);
	kiop->wlentime += delta * wcnt;
	kiop->wtime += delta;
}

void
kstat_runq_enter(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t rcnt;

	new = gethrtime();
	delta = new - kiop->rlastupdate;
	kiop->rlastupdate = new;
	rcnt = kiop->rcnt++;
	if (rcnt != 0) {
		kiop->rlentime += delta * rcnt;
		kiop->rtime += delta;
	}
}

void
kstat_runq_exit(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t rcnt;

	new = gethrtime();
	delta = new - kiop->rlastupdate;
	kiop->rlastupdate = new;
	rcnt = kiop->rcnt--;
	ASSERT((int)rcnt > 0);
	kiop->rlentime += delta * rcnt;
	kiop->rtime += delta;
}

void
kstat_waitq_to_runq(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t wcnt, rcnt;

	new = gethrtime();

	delta = new - kiop->wlastupdate;
	kiop->wlastupdate = new;
	wcnt = kiop->wcnt--;
	ASSERT((int)wcnt > 0);
	kiop->wlentime += delta * wcnt;
	kiop->wtime += delta;

	delta = new - kiop->rlastupdate;
	kiop->rlastupdate = new;
	rcnt = kiop->rcnt++;
	if (rcnt != 0) {
		kiop->rlentime += delta * rcnt;
		kiop->rtime += delta;
	}
}

void
kstat_runq_back_to_waitq(kstat_io_t *kiop)
{
	hrtime_t new, delta;
	ulong_t wcnt, rcnt;

	new = gethrtime();

	delta = new - kiop->rlastupdate;
	kiop->rlastupdate = new;
	rcnt = kiop->rcnt--;
	ASSERT((int)rcnt > 0);
	kiop->rlentime += delta * rcnt;
	kiop->rtime += delta;

	delta = new - kiop->wlastupdate;
	kiop->wlastupdate = new;
	wcnt = kiop->wcnt++;
	if (wcnt != 0) {
		kiop->wlentime += delta * wcnt;
		kiop->wtime += delta;
	}
}

#endif

void
kstat_timer_start(kstat_timer_t *ktp)
{
	ktp->start_time = gethrtime();
}

void
kstat_timer_stop(kstat_timer_t *ktp)
{
	hrtime_t	etime;
	u_longlong_t	num_events;

	ktp->stop_time = etime = gethrtime();
	etime -= ktp->start_time;
	num_events = ktp->num_events;
	if (etime < ktp->min_time || num_events == 0)
		ktp->min_time = etime;
	if (etime > ktp->max_time)
		ktp->max_time = etime;
	ktp->elapsed_time += etime;
	ktp->num_events = num_events + 1;
}

/* io/kstat.c */

/*
 * kernel statistics driver
 */

#include <sys/types.h>
#include <sys/time.h>
#include <sys/param.h>
#include <sys/sysmacros.h>
#include <sys/file.h>
#include <sys/cmn_err.h>
#include <sys/t_lock.h>
#include <sys/proc.h>
#include <sys/fcntl.h>
#include <sys/uio.h>
#include <sys/kmem.h>
#include <sys/cred.h>
//#include <sys/mman.h>
#include <sys/errno.h>
//#include <sys/ioccom.h>
#include <sys/cpuvar.h>
#include <sys/stat.h>
#include <sys/conf.h>
#include <sys/ddi.h>
#include <sys/sunddi.h>
//#include <sys/modctl.h>
#include <sys/kobj.h>
#include <sys/kstat.h>
#include <sys/atomic.h>
#include <sys/policy.h>
#include <sys/zone.h>

static dev_info_t *kstat_devi;

static int
read_kstat_data(int *rvalp, void *user_ksp, int flag)
{
	kstat_t user_kstat, *ksp;
#ifdef _MULTI_DATAMODEL
	kstat32_t user_kstat32;
#endif
	void *kbuf = NULL;
	size_t kbufsize, ubufsize, copysize, allocsize;
	int error = 0;
	uint_t model;

#define DDI_MODEL_NONE 0
//	switch (model = ddi_model_convert_from(flag & FMODELS)) {
	switch (model = DDI_MODEL_NONE) {
#ifdef _MULTI_DATAMODEL
	case DDI_MODEL_ILP32:
		if (copyin(user_ksp, &user_kstat32, sizeof(kstat32_t)) != 0)
			return (EFAULT);
		user_kstat.ks_kid = user_kstat32.ks_kid;
		user_kstat.ks_data = (void *)(uintptr_t)user_kstat32.ks_data;
		user_kstat.ks_data_size = (size_t)user_kstat32.ks_data_size;
		break;
#endif
	default:
	case DDI_MODEL_NONE:
		if (ddi_copyin(user_ksp, &user_kstat, sizeof(kstat_t), 0) != 0)
			return (EFAULT);
	}

	ksp = kstat_hold_bykid(user_kstat.ks_kid, getzoneid());
	if (ksp == NULL) {
		/*
		* There is no kstat with the specified KID
		*/
		return (ENXIO);
	}
	if (ksp->ks_flags & KSTAT_FLAG_INVALID) {
		/*
		* The kstat exists, but is momentarily in some
		* indeterminate state (e.g. the data section is not
		* yet initialized).  Try again in a few milliseconds.
		*/
		kstat_rele(ksp);
		return (EAGAIN);
	}

	/*
	* If it's a fixed-size kstat, allocate the buffer now, so we
	* don't have to do it under the kstat's data lock.  (If it's a
	* var-size kstat or one with long strings, we don't know the size
	* until after the update routine is called, so we can't do this
	* optimization.)
	* The allocator relies on this behavior to prevent recursive
	* mutex_enter in its (fixed-size) kstat update routine.
	* It's a zalloc to prevent unintentional exposure of random
	* juicy morsels of (old) kernel data.
	*/
	if (!(ksp->ks_flags & (KSTAT_FLAG_VAR_SIZE | KSTAT_FLAG_LONGSTRINGS))) {
		kbufsize = ksp->ks_data_size;
		allocsize = kbufsize + 1;
		kbuf = kmem_zalloc(allocsize, KM_NOSLEEP);
		if (kbuf == NULL) {
			kstat_rele(ksp);
			return (EAGAIN);
		}
	}
	KSTAT_ENTER(ksp);
	if ((error = KSTAT_UPDATE(ksp, KSTAT_READ)) != 0) {
		KSTAT_EXIT(ksp);
		kstat_rele(ksp);
		if (kbuf != NULL)
			kmem_free(kbuf, allocsize);
		return (error);
	}

	kbufsize = ksp->ks_data_size;
	ubufsize = user_kstat.ks_data_size;

	if (ubufsize < kbufsize) {
		error = ENOMEM;
	} else {
		if (kbuf == NULL) {
			allocsize = kbufsize + 1;
			kbuf = kmem_zalloc(allocsize, KM_NOSLEEP);
		}
		if (kbuf == NULL) {
			error = EAGAIN;
		} else {
			error = KSTAT_SNAPSHOT(ksp, kbuf, KSTAT_READ);
		}
	}

	/*
	* The following info must be returned to user level,
	* even if the the update or snapshot failed.  This allows
	* kstat readers to get a handle on variable-size kstats,
	* detect dormant kstats, etc.
	*/
	user_kstat.ks_ndata = ksp->ks_ndata;
	user_kstat.ks_data_size = kbufsize;
	user_kstat.ks_flags = ksp->ks_flags;
	user_kstat.ks_snaptime = ksp->ks_snaptime;
#ifndef _WIN32
	*rvalp = kstat_chain_id;
#else
	// The above doesn't work, as rvalp refers to the userland struct, before copyin()
	// and we need to write value to kernel version.
	user_kstat.ks_returnvalue = kstat_chain_id;
#endif
	KSTAT_EXIT(ksp);
	kstat_rele(ksp);

	if (kbuf == NULL)
		goto out;

	/*
	* Copy the buffer containing the kstat back to userland.
	*/
	copysize = kbufsize;

	switch (model) {
		int i;
#ifdef _MULTI_DATAMODEL
		kstat32_t *k32;
		kstat_t *k;

	case DDI_MODEL_ILP32:

		if (ksp->ks_type == KSTAT_TYPE_NAMED) {
			kstat_named_t *kn = kbuf;
			char *strbuf = (char *)((kstat_named_t *)kn +
				ksp->ks_ndata);

			for (i = 0; i < user_kstat.ks_ndata; kn++, i++)
				switch (kn->data_type) {
					/*
					* Named statistics have fields of type 'long'.
					* For a 32-bit application looking at a 64-bit
					* kernel, forcibly truncate these 64-bit
					* quantities to 32-bit values.
					*/
				case KSTAT_DATA_LONG:
					kn->value.i32 = (int32_t)kn->value.l;
					kn->data_type = KSTAT_DATA_INT32;
					break;
				case KSTAT_DATA_ULONG:
					kn->value.ui32 = (uint32_t)kn->value.ul;
					kn->data_type = KSTAT_DATA_UINT32;
					break;
					/*
					* Long strings must be massaged before being
					* copied out to userland.  Do that here.
					*/
				case KSTAT_DATA_STRING:
					if (KSTAT_NAMED_STR_PTR(kn) == NULL)
						break;
					/*
					* If the string lies outside of kbuf
					* copy it there and update the pointer.
					*/
					if (KSTAT_NAMED_STR_PTR(kn) <
						(char *)kbuf ||
						KSTAT_NAMED_STR_PTR(kn) +
						KSTAT_NAMED_STR_BUFLEN(kn) >
						(char *)kbuf + kbufsize + 1) {
						bcopy(KSTAT_NAMED_STR_PTR(kn),
							strbuf,
							KSTAT_NAMED_STR_BUFLEN(kn));

						KSTAT_NAMED_STR_PTR(kn) =
							strbuf;
						strbuf +=
							KSTAT_NAMED_STR_BUFLEN(kn);
						ASSERT(strbuf <=
							(char *)kbuf +
							kbufsize + 1);
					}
					/*
					* The offsets within the buffers are
					* the same, so add the offset to the
					* beginning of the new buffer to fix
					* the pointer.
					*/
					KSTAT_NAMED_STR_PTR(kn) =
						(char *)user_kstat.ks_data +
						(KSTAT_NAMED_STR_PTR(kn) -
						(char *)kbuf);
					/*
					* Make sure the string pointer lies
					* within the allocated buffer.
					*/
					ASSERT(KSTAT_NAMED_STR_PTR(kn) +
						KSTAT_NAMED_STR_BUFLEN(kn) <=
						((char *)user_kstat.ks_data +
							ubufsize));
					ASSERT(KSTAT_NAMED_STR_PTR(kn) >=
						(char *)((kstat_named_t *)
							user_kstat.ks_data +
							user_kstat.ks_ndata));
					/*
					* Cast 64-bit ptr to 32-bit.
					*/
					kn->value.str.addr.ptr32 =
						(caddr32_t)(uintptr_t)
						KSTAT_NAMED_STR_PTR(kn);
					break;
				default:
					break;
				}
		}

		if (user_kstat.ks_kid != 0)
			break;

		/*
		* This is the special case of the kstat header
		* list for the entire system.  Reshape the
		* array in place, then copy it out.
		*/
		k32 = kbuf;
		k = kbuf;
		for (i = 0; i < user_kstat.ks_ndata; k32++, k++, i++) {
			k32->ks_crtime = k->ks_crtime;
			k32->ks_next = 0;
			k32->ks_kid = k->ks_kid;
			(void)strcpy(k32->ks_module, k->ks_module);
			k32->ks_resv = k->ks_resv;
			k32->ks_instance = k->ks_instance;
			(void)strcpy(k32->ks_name, k->ks_name);
			k32->ks_type = k->ks_type;
			(void)strcpy(k32->ks_class, k->ks_class);
			k32->ks_flags = k->ks_flags;
			k32->ks_data = 0;
			k32->ks_ndata = k->ks_ndata;
			if (k->ks_data_size > UINT32_MAX) {
				error = EOVERFLOW;
				break;
			}
			k32->ks_data_size = (size32_t)k->ks_data_size;
			k32->ks_snaptime = k->ks_snaptime;
		}

		/*
		* XXX	In this case we copy less data than is
		*	claimed in the header.
		*/
		copysize = user_kstat.ks_ndata * sizeof(kstat32_t);
		break;
#endif	/* _MULTI_DATAMODEL */
	default:
	case DDI_MODEL_NONE:
		if (ksp->ks_type == KSTAT_TYPE_NAMED) {
			kstat_named_t *kn = kbuf;
			char *strbuf = (char *)((kstat_named_t *)kn +
				ksp->ks_ndata);

			for (i = 0; i < user_kstat.ks_ndata; kn++, i++)
				switch (kn->data_type) {
#ifdef _LP64
				case KSTAT_DATA_LONG:
					kn->data_type =
						KSTAT_DATA_INT64;
					break;
				case KSTAT_DATA_ULONG:
					kn->data_type =
						KSTAT_DATA_UINT64;
					break;
#endif	/* _LP64 */
				case KSTAT_DATA_STRING:
					if (KSTAT_NAMED_STR_PTR(kn) == NULL)
						break;
					/*
					* If the string lies outside of kbuf
					* copy it there and update the pointer.
					*/
					if (KSTAT_NAMED_STR_PTR(kn) <
						(char *)kbuf ||
						KSTAT_NAMED_STR_PTR(kn) +
						KSTAT_NAMED_STR_BUFLEN(kn) >
						(char *)kbuf + kbufsize + 1) {
						bcopy(KSTAT_NAMED_STR_PTR(kn),
							strbuf,
							KSTAT_NAMED_STR_BUFLEN(kn));

						KSTAT_NAMED_STR_PTR(kn) =
							strbuf;
						strbuf +=
							KSTAT_NAMED_STR_BUFLEN(kn);
						ASSERT(strbuf <=
							(char *)kbuf +
							kbufsize + 1);
					}

					KSTAT_NAMED_STR_PTR(kn) =
						(char *)user_kstat.ks_data +
						(KSTAT_NAMED_STR_PTR(kn) -
						(char *)kbuf);
					ASSERT(KSTAT_NAMED_STR_PTR(kn) +
						KSTAT_NAMED_STR_BUFLEN(kn) <=
						((char *)user_kstat.ks_data +
							ubufsize));
					ASSERT(KSTAT_NAMED_STR_PTR(kn) >=
						(char *)((kstat_named_t *)
							user_kstat.ks_data +
							user_kstat.ks_ndata));
					break;
				default:
					break;
				}
		}
		break;
	}

	if (error == 0 &&
		ddi_copyout(kbuf, user_kstat.ks_data, copysize, 0))
		error = EFAULT;
	kmem_free(kbuf, allocsize);

out:
	/*
	* We have modified the ks_ndata, ks_data_size, ks_flags, and
	* ks_snaptime fields of the user kstat; now copy it back to userland.
	*/
	switch (model) {
#ifdef _MULTI_DATAMODEL
	case DDI_MODEL_ILP32:
		if (kbufsize > UINT32_MAX) {
			error = EOVERFLOW;
			break;
		}
		user_kstat32.ks_ndata = user_kstat.ks_ndata;
		user_kstat32.ks_data_size = (size32_t)kbufsize;
		user_kstat32.ks_flags = user_kstat.ks_flags;
		user_kstat32.ks_snaptime = user_kstat.ks_snaptime;
		if (copyout(&user_kstat32, user_ksp, sizeof(kstat32_t)) &&
			error == 0)
			error = EFAULT;
		break;
#endif
	default:
	case DDI_MODEL_NONE:
		// If we have an errorcode, set it in ks_errnovalue
		// Above sets returnvalue with *rval =
		// Must be done before this copyout()
		user_kstat.ks_errnovalue = 0;
		if (error) {
			user_kstat.ks_errnovalue = error;
			user_kstat.ks_returnvalue = -1;
		} 
		if (ddi_copyout(&user_kstat, user_ksp, sizeof(kstat_t), 0) &&
			error == 0)
			error = EFAULT;
		break;
	}

	return (error);
}

static int
write_kstat_data(int *rvalp, void *user_ksp, int flag, cred_t *cred)
{
	kstat_t user_kstat, *ksp;
	void *buf = NULL;
	size_t bufsize;
	int error = 0;

	if (secpolicy_sys_config(cred, B_FALSE) != 0)
		return (EPERM);

//	switch (ddi_model_convert_from(flag & FMODELS)) {
	switch (DDI_MODEL_NONE) {
#ifdef _MULTI_DATAMODEL
		kstat32_t user_kstat32;

	case DDI_MODEL_ILP32:
		if (copyin(user_ksp, &user_kstat32, sizeof(kstat32_t)))
			return (EFAULT);
		/*
		* These are the only fields we actually look at.
		*/
		user_kstat.ks_kid = user_kstat32.ks_kid;
		user_kstat.ks_data = (void *)(uintptr_t)user_kstat32.ks_data;
		user_kstat.ks_data_size = (size_t)user_kstat32.ks_data_size;
		user_kstat.ks_ndata = user_kstat32.ks_ndata;
		break;
#endif
	default:
	case DDI_MODEL_NONE:
		if (ddi_copyin(user_ksp, &user_kstat, sizeof(kstat_t), 0))
			return (EFAULT);
	}

	bufsize = user_kstat.ks_data_size;
	buf = kmem_alloc(bufsize + 1, KM_NOSLEEP);
	if (buf == NULL)
		return (EAGAIN);

	if (ddi_copyin(user_kstat.ks_data, buf, bufsize, 0)) {
		kmem_free(buf, bufsize + 1);
		return (EFAULT);
	}

	ksp = kstat_hold_bykid(user_kstat.ks_kid, getzoneid());
	if (ksp == NULL) {
		kmem_free(buf, bufsize + 1);
		return (ENXIO);
	}
	if (ksp->ks_flags & KSTAT_FLAG_INVALID) {
		kstat_rele(ksp);
		kmem_free(buf, bufsize + 1);
		return (EAGAIN);
	}
	if (!(ksp->ks_flags & KSTAT_FLAG_WRITABLE)) {
		kstat_rele(ksp);
		kmem_free(buf, bufsize + 1);
		return (EACCES);
	}

	/*
	* With KSTAT_FLAG_VAR_SIZE, one must call the kstat's update callback
	* routine to ensure ks_data_size is up to date.
	* In this case it makes sense to do it anyhow, as it will be shortly
	* followed by a KSTAT_SNAPSHOT().
	*/
	KSTAT_ENTER(ksp);
	error = KSTAT_UPDATE(ksp, KSTAT_READ);
	if (error || user_kstat.ks_data_size != ksp->ks_data_size ||
		user_kstat.ks_ndata != ksp->ks_ndata) {
		KSTAT_EXIT(ksp);
		kstat_rele(ksp);
		kmem_free(buf, bufsize + 1);
		return (error ? error : EINVAL);
	}

	/*
	* We have to ensure that we don't accidentally change the type of
	* existing kstat_named statistics when writing over them.
	* Since read_kstat_data() modifies some of the types on their way
	* out, we need to be sure to handle these types seperately.
	*/
	if (ksp->ks_type == KSTAT_TYPE_NAMED) {
		void *kbuf;
		kstat_named_t *kold;
		kstat_named_t *knew = buf;
		int i;

#ifdef	_MULTI_DATAMODEL
		int model = ddi_model_convert_from(flag & FMODELS);
#endif

		/*
		* Since ksp->ks_data may be NULL, we need to take a snapshot
		* of the published data to look at the types.
		*/
		kbuf = kmem_alloc(bufsize + 1, KM_NOSLEEP);
		if (kbuf == NULL) {
			KSTAT_EXIT(ksp);
			kstat_rele(ksp);
			kmem_free(buf, bufsize + 1);
			return (EAGAIN);
		}
		error = KSTAT_SNAPSHOT(ksp, kbuf, KSTAT_READ);
		if (error) {
			KSTAT_EXIT(ksp);
			kstat_rele(ksp);
			kmem_free(kbuf, bufsize + 1);
			kmem_free(buf, bufsize + 1);
			return (error);
		}
		kold = kbuf;

		/*
		* read_kstat_data() changes the types of
		* KSTAT_DATA_LONG / KSTAT_DATA_ULONG, so we need to
		* make sure that these (modified) types are considered
		* valid.
		*/
		for (i = 0; i < ksp->ks_ndata; i++, kold++, knew++) {
			switch (kold->data_type) {
#ifdef	_MULTI_DATAMODEL
			case KSTAT_DATA_LONG:
				switch (model) {
				case DDI_MODEL_ILP32:
					if (knew->data_type ==
						KSTAT_DATA_INT32) {
						knew->value.l =
							(long)knew->value.i32;
						knew->data_type =
							KSTAT_DATA_LONG;
					}
					break;
				default:
				case DDI_MODEL_NONE:
#ifdef _LP64
					if (knew->data_type ==
						KSTAT_DATA_INT64) {
						knew->value.l =
							(long)knew->value.i64;
						knew->data_type =
							KSTAT_DATA_LONG;
					}
#endif /* _LP64 */
					break;
				}
				break;
			case KSTAT_DATA_ULONG:
				switch (model) {
				case DDI_MODEL_ILP32:
					if (knew->data_type ==
						KSTAT_DATA_UINT32) {
						knew->value.ul =
							(ulong_t)knew->value.ui32;
						knew->data_type =
							KSTAT_DATA_ULONG;
					}
					break;
				default:
				case DDI_MODEL_NONE:
#ifdef _LP64
					if (knew->data_type ==
						KSTAT_DATA_UINT64) {
						knew->value.ul =
							(ulong_t)knew->value.ui64;
						knew->data_type =
							KSTAT_DATA_ULONG;
					}
#endif /* _LP64 */
					break;
				}
				break;
#endif /* _MULTI_DATAMODEL */
			case KSTAT_DATA_STRING:
				if (knew->data_type != KSTAT_DATA_STRING) {
					KSTAT_EXIT(ksp);
					kstat_rele(ksp);
					kmem_free(kbuf, bufsize + 1);
					kmem_free(buf, bufsize + 1);
					return (EINVAL);
				}

#ifdef _MULTI_DATAMODEL
				if (model == DDI_MODEL_ILP32)
					KSTAT_NAMED_STR_PTR(knew) =
					(char *)(uintptr_t)
					knew->value.str.addr.ptr32;
#endif
				/*
				* Nothing special for NULL
				*/
				if (KSTAT_NAMED_STR_PTR(knew) == NULL)
					break;

				/*
				* Check to see that the pointers all point
				* to within the buffer and after the array
				* of kstat_named_t's.
				*/
				if (KSTAT_NAMED_STR_PTR(knew) <
					(char *)
					((kstat_named_t *)user_kstat.ks_data +
						ksp->ks_ndata)) {
					KSTAT_EXIT(ksp);
					kstat_rele(ksp);
					kmem_free(kbuf, bufsize + 1);
					kmem_free(buf, bufsize + 1);
					return (EINVAL);
				}
				if (KSTAT_NAMED_STR_PTR(knew) +
					KSTAT_NAMED_STR_BUFLEN(knew) >
					((char *)user_kstat.ks_data +
						ksp->ks_data_size)) {
					KSTAT_EXIT(ksp);
					kstat_rele(ksp);
					kmem_free(kbuf, bufsize + 1);
					kmem_free(buf, bufsize + 1);
					return (EINVAL);
				}

				/*
				* Update the pointers within the buffer
				*/
				KSTAT_NAMED_STR_PTR(knew) =
					(char *)buf +
					(KSTAT_NAMED_STR_PTR(knew) -
					(char *)user_kstat.ks_data);
				break;
			default:
				break;
			}
		}

		kold = kbuf;
		knew = buf;

		/*
		* Now make sure the types are what we expected them to be.
		*/
		for (i = 0; i < ksp->ks_ndata; i++, kold++, knew++)
			if (kold->data_type != knew->data_type) {
				KSTAT_EXIT(ksp);
				kstat_rele(ksp);
				kmem_free(kbuf, bufsize + 1);
				kmem_free(buf, bufsize + 1);
				return (EINVAL);
			}

		kmem_free(kbuf, bufsize + 1);
	}

	error = KSTAT_SNAPSHOT(ksp, buf, KSTAT_WRITE);
	if (!error)
		error = KSTAT_UPDATE(ksp, KSTAT_WRITE);
#ifndef _WIN32
	*rvalp = kstat_chain_id;
#else
	// The above doesn't work, as rvalp refers to the userland struct, before copyin()
	// and we need to write value to kernel version.
	user_kstat.ks_returnvalue = kstat_chain_id;
	// We need to copyout() so userland will get the return values.
#endif

	KSTAT_EXIT(ksp);
	kstat_rele(ksp);
	kmem_free(buf, bufsize + 1);
	return (error);
}

/* spl-kstat.c */

void
spl_kstat_init()
{
    /*
	 * Create the kstat root OID
	 */
	mutex_init(&kstat_chain_lock, NULL, MUTEX_DEFAULT, NULL);
}

void
spl_kstat_fini()
{
	/*
	 * Destroy the kstat module/class/name tree
	 *
	 * Done in two passes, first unregisters all
	 * of the oids, second releases all the memory.
	 */
	
	vmem_fini(kstat_arena);
	mutex_destroy(&kstat_chain_lock);
}


void kstat_set_raw_ops(kstat_t *ksp,
	int(*headers)(char *buf, size_t size),
	int(*data)(char *buf, size_t size, void *data),
	void *(*addr)(kstat_t *ksp, off_t index))
{
}

int spl_kstat_chain_id(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	kstat_t ksp = { 0 };
	ksp.ks_returnvalue = kstat_chain_id;
	ASSERT3U(IrpSp->Parameters.DeviceIoControl.OutputBufferLength, >=, sizeof(ksp));
	ddi_copyout(&ksp, IrpSp->Parameters.DeviceIoControl.Type3InputBuffer,
		sizeof(ksp), 0);
	dprintf("%s: returning kstat_chain_id %d\n", __func__, kstat_chain_id);
	return 0;
}

int spl_kstat_read(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	int rval, rc;
	kstat_t *ksp;
	ksp = (kstat_t *)IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;
	rc = read_kstat_data(&ksp->ks_returnvalue, (void *)ksp, 0);
	return 0;
}

int spl_kstat_write(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp)
{
	int rval, rc;
	kstat_t *ksp;
	ksp = (kstat_t *)IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;
	rc = write_kstat_data(&ksp->ks_returnvalue, (void *)ksp, 0, NULL);
	return 0;
}
