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
 * Copyright (c) 1994, 2010, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright (c) 2013, Joyent, Inc.  All rights reserved.
 */
/*
 * Copyright (c) 2015, Evan Susarret.  All rights reserved.
 */
/*
 * Portions of this document are copyright Oracle and Joyent.
 * OS X implementation of ldi_ named functions for ZFS written by
 * Evan Susarret in 2015.
 */

/*
 * LDI Subsystem on OS X:
 *
 * Designed as a drop-in replacement for sunldi.h and driver_lyr.c,
 * LDI abstracts away platform-specific device handling. This allows
 * vdev_disk.c to more closely match 'upstream' illumos/OpenZFS.
 *
 * LDI handles may use IOKit or vnode ops to locate and use devices.
 * - This reduces the call stack and work needed for almost all IO.
 * - Allows for vdev discovery and use during early boot, before the
 * root device is mounted.
 * - Having both types allows use of non-standard kexts which publish
 * bdevsw block devices (without IOMedia).
 *
 * XXX Review correct call stack using dtrace, annotate stack size.
 * Previously, vnode_open and VNOP_STRATEGY were used, which required
 * allocating buf_t for IO. This meant translating byte offsets to
 * block numbers for every IO. Once issued, dtrace showed that a very
 * large stack was required:
 * VNOP_STRATEGY macro performs work then calls
 * spec_strategy (vop->vop_strategy) which performs work then calls
 * dkiostrategy (syscall) which passes the IO to an IOMediaBSDClient
 * IOMediaBSDClient performed work and passes to its IOMedia provider
 *
 * Beyond that is a common path shared by vnode and IOMedia:
 * IOMedia performs work, then does prepareRequest, breakUpRequest,
 * deBlockRequest, and executeRequest.
 * Potentially passed down the provider stack through IOPartitionMap
 * then to the whole-disk IOMedia, with more work
 * Passed down through IOBlockStorageDriver, with more work
 * Passed down through IOBlockStorageDevice, with more work
 * Finally passed to Family-specific driver (AHCI, diskimage, etc.)
 *
 * By directly accessing IOMedia, the stack is reduced, and byte
 * offsets are passed to read()/write() via ldi_strategy.
 * We still need to allocate an IOMemoryDescriptor for the data buf,
 * however only an IOMemoryDescriptor::withAddress() reference is
 * required, similar to buf_setdataptr.
 */

/*
 * LDI Handle hash lists:
 *
 * During ldi_init, LH_HASH_SZ lists and locks are allocated. New handles
 * will be added to the list indexed by the hash of the dev_t number.
 *
 * The hash function simply performs a modulus on the dev_t number based on
 * the LH_HASH_SZ, as opposed to illumos which hashes based on the vnode
 * pointer.
 * This has been tested by hashing disk0, disk0s1, disk0s2, disk1, disk1s1,
 * etc. to verify results were distributed across hash range.
 *
 * OS X dev_t numbers should be unique unless a new device claims the same
 * dev_t as a removed/failed device. This would only be a collision if we
 * still have a handle for the failed device (notification/event handlers
 * should remove these before that occurs).
 * Since Offline status is a dead-end and the handle cannot be dereferenced
 * or freed while iterating the hash list, it is safe to check the status
 * and skip a handle if the status is Offline (without taking handle lock).
 *
 * XXX On illumos the hash function uses the vnode's pointer address as the
 * unique key. Since vnode addresses are aligned to the size of the vnode
 * struct, the hash function shifts the pointer address to the right in order
 * to hash the unique bits of the address. OS X dev_t use all the bits of
 * an unsigned 32-bit int.
 */

/*
 * LDI Handle locks:
 *
 * Handle references and list membership are protected by the hash list
 * locks.
 * Handle status and other fields are protected by a per-handle mutex.
 *
 * To prevent deadlocks and artificial delays, the hash list locks should
 * be held only for handle hold/release and handle_add/remove (list
 * iterate/insert/remove). Those functions avoid blocking.
 * Use the handle mutex to change state, and avoid blocking there, too.
 *
 * XXX Right now handle_status_change does allocate for taskq_dispatch
 * with the handle lock held, but uses TQ_NOSLEEP and verifies result.
 *
 * Non-locking ops such as ldi_strategy, ldi_get_size, and ldi_sync will
 * check the instantaneous status/refs before attempting to proceed, and
 * can only perform IO while the device is Online.
 */

/*
 * LDI Handle allocation:
 *
 * ldi_open_by_name and ldi_open_by_dev locate the device and call
 * ldi_open_media_by_path, ldi_open_media_by_dev, or ldi_open_vnode_by_path.
 *
 * From ldi_open_by_media and _by_vnode, we call handle_alloc_{type}. Both
 * call handle_alloc_common to allocate and configure the handle.
 *
 * A handle is allocated in the Closed state with 1 reference. The handle
 * is added to the hash list on allocation, unless a duplicate handle exists
 * (same dev_t as well as fmode, not in Offline status). If an existing
 * handle is found, the newly allocated handle is freed.
 *
 * handle_open_start is called, which takes the handle lock to check current
 * status. Each of these states is possible:
 * Offline: device has disappeared between allocation and now (unlikely).
 * Closed: new or recently closed handle, changes status to Opening.
 * Closing: already in progress. Sleeps on lock and rechecks the status.
 * Opening: already in progress. Sleeps on lock and rechecks the status.
 * Online: no need to open device, just increment openref count.
 *
 * If handle_open_start changes the status to Opening, the device is opened
 * by calling handle_open_iokit or handle_open_vnode.
 *
 * This differs from illumos driver_lyr.c where handle_alloc first opens a
 * vnode for the device, allocates a handle by vnode, and finally checks for
 * a duplicate handle in the list (open, alloc, find vs. alloc, open, find).
 * To do so, illumos has a VOP_OPEN that is aware of layered-driver opens.
 */

/*
 * LDI Handle list membership:
 *
 * Allocate with one reference, to be used or released by the caller.
 * Call handle_hold if additional references are needed.
 *
 * Call handle_release to drop reference. On last release, this calls
 * handle_free (but does not remove the handle from the list, see below).
 *
 * Call handle_add to determine if this handle is a duplicate, inserting
 * handle into list or returning an existing handle with a hold.
 * Check the result and call handle_release on the new handle if another
 * handle was returned (new handle is not added to list).
 *
 * Each call to handle_find will take optionally take a hold, which should
 * be released when no longer needed (used by handle_add).
 *
 * Calling handle_open increments lh_openref but does not change lh_ref.
 * Caller should already have called handle_hold to get a reference.
 *
 * If lh_ref is 1, call handle_remove_locked (with list lock) to remove the
 * handle from the list, then call handle_release_locked to remove last ref
 * and free.
 * A handle shouldn't remain in the list in Closed status with no refs.
 *
 * Calling handle_close with the last openref will automatically take list
 * lock, call handle_remove_locked, and then handle_release_locked.
 */

/*
 * LDI Handle device objects:
 *
 * Multiple read-only opens share one read-only handle.
 * Multiple read-write opens share one read-write handle.
 *
 * IOKit handles are allocated with the dev_t number and fmode.
 * handle_open_iokit is passed an IOMedia object (which should have a
 * retain held).
 * Once handle_open returns, the IOMedia can be released by the caller.
 *
 * Vnode handles are allocated with the dev_t number and fmode.
 * handle_open_vnode is passed a path (null-terminated C string).
 * vnode_open increments both iocount and refcount, vnode_ref increments
 * usecount, vnode_put drops iocount between ops.
 * vnode_getwithref takes an iocount, and vnode_rele drops usecount
 * before vnode_close decrements iocount and refcount.
 */

/*
 * LDI Handle status:
 *
 * #define	LDI_STATUS_OFFLINE	0x0
 * #define	LDI_STATUS_CLOSED	0x1
 * #define	LDI_STATUS_CLOSING	0x2
 * #define	LDI_STATUS_OPENING	0x3
 * #define	LDI_STATUS_ONLINE	0x4
 *
 * The handle lock will be taken to change status.
 *
 * Handle state can only progress from Closed to Opening status, and must
 * have a reference held to do so. The lock is dropped for open and close
 * ops while the handle is in Opening or Closing status.
 *
 * If the open is successful, the state is set to Online (with handle lock
 * held). This state is required for IO operations to be started. The state
 * may have changed by the time an IO completes.
 *
 * For IOKit devices, and vnode devices that have an IOMedia, a callback is
 * registered for IOMedia termination which changes the state to Offline and
 * posts event callbacks.
 *
 * Closing a handle, by the user or as a result of an event, sets the state
 * to Closing. Once device close is issued, the state changes from Closing
 * to Closed (even if close returned failure).
 *
 * A handle that still has refs and openrefs will remain in the Online
 * state, dropping refs and openrefs each time ldi_close is called.
 *
 * If there are refs but no openrefs, it remains in the Closed state, and
 * drops refs each time handle_release is called.
 * This allows clients to call ldi_open_by_* to reopen the handle, in the
 * case where one client is opening the handle at the same time another is
 * closing it.
 *
 * If the device has gone missing (IOMedia terminated), the handle will
 * change to Offline status. This is a dead-end which issues Offline Notify
 * and Finalize events, then cleans up the handle once all clients have
 * called ldi_close.
 *
 * Once all references have been dropped, the handle is removed from the
 * hash list with the hash list lock held, then freed.
 */

/*
 * LDI Events:
 *
 * XXX Degrade event is not implemented, doubt it will be useful. Intended
 * to be set when a vdev that is backed by RAID becomes degraded. This is
 * not a recommended use case for ZFS, and on OS X we only have AppleRAID
 * or custom hardware or software RAID. Also per the comments, the vdev
 * would be marked Degraded only to inform the user via zpool status.
 *
 * XXX Tested in VirtualBox by hotplugging a SATA device, have yet to
 * test with USB removal, etc.
 *
 * ldi_register_ev_callback can be used to add a struct to the event
 * callback list containing the handle pointer, a notify callback, and
 * a finalize callback.
 *
 * Supported events are Offline Notify/Finalize, which will be
 * posted when the device enters the Offline state (IOMedia terminated).
 *
 * The event callback functions should be non-blocking. It is recommended
 * to update a flag that can be checked prior to calling ldi_strategy.
 */

/*
 * LDI client interfaces:
 *
 * ldi_open_by_name
 * ldi_open_by_dev
 * ldi_close
 *
 * ldi_register_ev_callback
 * ldi_unregister_ev_callback
 *
 * ldi_get_size
 * ldi_sync
 * ldi_ioctl
 * ldi_strategy
 *
 * ldi_bioinit
 * ldi_biofini
 */

/*
 * LDI Buffers:
 *
 * ldi_strategy uses an abstract buffer for IO, so clients do not need to
 * be concerned with type-specific buf_t and IOMemoryDescriptor handling.
 *
 * Allocate and free ldi_buf_t manually, calling ldi_bioinit after alloc
 * and ldi_biofini prior to free.
 *
 * Synchronous IO can be performed by setting b_iodone to NULL.
 *
 * Allocate and use a buffer like this:
 *
 * ldi_buf_t *bp = (ldi_buf_t *)kmem_alloc(sizeof (ldi_buf_t), KM_SLEEP);
 * // Verify allocation before proceeding
 * error = ldi_bioinit(bp);
 * bp->b_bcount = size;
 * bp->b_bufsize = size;
 * bp->b_offset = offset;
 * bp->b_data = data_ptr;
 * bp->b_flags = B_BUSY | B_NOCACHE | B_READ; // For example
 * bp->b_iodone = &io_intr_func;  // For async IO, omit for sync IO
 * ldi_strategy(handle, bp);      // Issue IO
 *
 * With an async callback function such as:
 * void io_intr_func(ldi_buf_t bp, void *param)
 * {
 *     // Check/copyout bp->b_error and bp->b_resid
 *     ldi_biofini(bp);
 *     kmem_free(bp, sizeof (ldi_buf_t));
 * }
 */

/*
 * XXX LDI TO DO
 *
 * LDI handle stats. In debug builds, we have IO counters - number of IOs,
 * number of bytes in/out.
 * kstats for handle counts and sysctls for vnode/IOKit modes also implemented.
 *
 * To implement events, both vnode and IOKit handles register for matching
 * notifications from the IOMedia object (if found).
 * Using subclassed IOService can also receive IOMessage events, which
 * would be issued earlier.
 *
 * Vnode handles with no IOMedia could post events on (multiple) IO failures.
 */

/*
 * ZFS internal
 */
#include <sys/zfs_context.h>
#include <sys/taskq.h>
#include <sys/kstat.h>
#include <sys/kstat_osx.h>
#include <sys/dkio.h>

/*
 * LDI Includes
 */
#include <sys/ldi_impl_osx.h>

/* Debug prints */
#ifdef DEBUG
#define	LDI_EVDBG(args)		cmn_err args
#define	LDI_EVTRC(args)		cmn_err args
#else
#define	LDI_EVDBG(args)		do {} while (0)
#define	LDI_EVTRC(args)		do {} while (0)
#endif

#define	ldi_log(fmt, ...) do {		\
	dprintf(fmt, __VA_ARGS__);	\
	/* delay(hz>>1); */		\
_NOTE(CONSTCOND) } while (0)

/*
 * Defines
 * comment out defines to alter behavior.
 */
// #define	LDI_ZERO		/* For debugging, zero allocations */

/* Find IOMedia by matching on the BSD disk name. */
static boolean_t ldi_use_iokit_from_path = 1;

/* Find IOMedia by matching on the BSD major/minor (dev_t) number. */
static boolean_t ldi_use_iokit_from_dev = 1;

/*
 * Find dev_t by vnode_lookup.
 * Resolves symlinks to block devices, symlinks, InvariantDisk links.
 */
static boolean_t ldi_use_dev_from_path = 1;

/*
 * Open device by vnode if all else fails.
 * Not intented to be a fallback for unsuccessful IOMedia open, but rather
 * for bdev devices that do not have an IOMedia (published by other KEXTs).
 */
static boolean_t ldi_use_vnode_from_path = 1;

/*
 * Sysctls
 */
#include <libkern/sysctl.h>
SYSCTL_DECL(_ldi);
SYSCTL_NODE(, OID_AUTO, ldi, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "");
SYSCTL_NODE(_ldi, OID_AUTO, debug, CTLFLAG_RD | CTLFLAG_LOCKED, 0, "");
SYSCTL_UINT(_ldi_debug, OID_AUTO, use_iokit_from_dev,
    CTLFLAG_RW | CTLFLAG_LOCKED, &ldi_use_iokit_from_dev, 0,
	"ZFS LDI use iokit_from_path");
SYSCTL_UINT(_ldi_debug, OID_AUTO, use_iokit_from_path,
    CTLFLAG_RW | CTLFLAG_LOCKED, &ldi_use_iokit_from_path, 0,
	"ZFS LDI use iokit_from_dev");
SYSCTL_UINT(_ldi_debug, OID_AUTO, use_dev_from_path,
    CTLFLAG_RW | CTLFLAG_LOCKED, &ldi_use_dev_from_path, 0,
	"ZFS LDI use dev_from_path");
SYSCTL_UINT(_ldi_debug, OID_AUTO, use_vnode_from_path,
    CTLFLAG_RW | CTLFLAG_LOCKED, &ldi_use_vnode_from_path, 0,
	"ZFS LDI use vnode_from_path");

/*
 * Globals
 */
static volatile int64_t		ldi_handle_hash_count;

static list_t			ldi_handle_hash_list[LH_HASH_SZ];
static kmutex_t			ldi_handle_hash_lock[LH_HASH_SZ];

/*
 * Use of "ldi_ev_callback_list" must be protected by ldi_ev_lock()
 * and ldi_ev_unlock().
 */
static struct ldi_ev_callback_list ldi_ev_callback_list;

static uint32_t ldi_ev_id_pool = 0;

struct ldi_ev_cookie {
	char *ck_evname;
	uint_t ck_sync;
	uint_t ck_ctype;
};

#define	CT_DEV_EV_OFFLINE	0x1
#define	CT_DEV_EV_DEGRADED	0x2
static struct ldi_ev_cookie ldi_ev_cookies[] = {
	{LDI_EV_OFFLINE, 1, CT_DEV_EV_OFFLINE},
	{LDI_EV_DEGRADE, 0, CT_DEV_EV_DEGRADED},
	{LDI_EV_DEVICE_REMOVE, 0, 0},
	{NULL}		/* must terminate list */
};

/*
 * kstats
 */
static kstat_t				*ldi_ksp;

typedef struct ldi_stats {
	kstat_named_t			handle_count;
	kstat_named_t			handle_count_iokit;
	kstat_named_t			handle_count_vnode;
	kstat_named_t			handle_refs;
	kstat_named_t			handle_open_rw;
	kstat_named_t			handle_open_ro;
} ldi_stats_t;

static ldi_stats_t ldi_stats = {
	{ "handle_count",		KSTAT_DATA_UINT64 },
	{ "handle_count_iokit",		KSTAT_DATA_UINT64 },
	{ "handle_count_vnode",		KSTAT_DATA_UINT64 },
	{ "handle_refs",		KSTAT_DATA_UINT64 },
	{ "handle_open_rw",		KSTAT_DATA_UINT64 },
	{ "handle_open_ro",		KSTAT_DATA_UINT64 }
};

#define	LDISTAT(stat)	(ldi_stats.stat.value.ui64)
#define	LDISTAT_INCR(stat, val) \
atomic_add_64(&ldi_stats.stat.value.ui64, (val))
#define	LDISTAT_BUMP(stat)	LDISTAT_INCR(stat, 1)
#define	LDISTAT_BUMPDOWN(stat)	LDISTAT_INCR(stat, -1)

/*
 * Define macros for accessing layered driver hash structures
 */
#define	LH_HASH(dev)		handle_hash_func(dev)

static inline uint_t
handle_hash_func(dev_t device)
{
	/* Just cast, macro does modulus to hash value */
	return ((uint_t)device % LH_HASH_SZ);
}

typedef struct status_change_args {
	struct ldi_handle *lhp;
	int new_status;
} status_change_args_t;

static void
handle_status_change_callback(void *arg)
{
	status_change_args_t *sc = (status_change_args_t *)arg;

	/* Validate arg struct */
	if (!sc || !sc->lhp) {
		dprintf("%s missing callback struct %p or lh\n",
		    __func__, sc);
		return;
	}
	if (sc->new_status > LDI_STATUS_ONLINE) {
		dprintf("%s invalid status %d\n",
		    __func__, sc->new_status);
		return;
	}

	dprintf("%s Invoking notify for handle %p status %d\n",
	    __func__, sc->lhp, sc->new_status);
	ldi_invoke_notify(0 /* dip */, sc->lhp->lh_dev, S_IFBLK,
	    LDI_EV_OFFLINE, sc->lhp);

	dprintf("%s Invoking finalize for handle %p status %d\n",
	    __func__, sc->lhp, sc->new_status);
	ldi_invoke_finalize(0 /* dip */, sc->lhp->lh_dev, S_IFBLK,
	    LDI_EV_OFFLINE, LDI_EV_SUCCESS, sc->lhp);

	/* Free callback struct */
	kmem_free(sc, sizeof (status_change_args_t));
}

/* Protected by handle lock */
static int
handle_status_change_locked(struct ldi_handle *lhp, int new_status)
{
	status_change_args_t *sc = 0;

	/* Validate lhp */
	if (!lhp) {
		dprintf("%s missing handle\n", __func__);
		return (EINVAL);
	}
	if (new_status > LDI_STATUS_ONLINE) {
		dprintf("%s invalid status %d\n", __func__, new_status);
		return (EINVAL);
	}

	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_dev, !=, 0);
	ASSERT(MUTEX_HELD(&lhp->lh_lock));

	/* Set the status first */
	lhp->lh_status = new_status;

	/* Only Offline needs an event */
	if (new_status != LDI_STATUS_OFFLINE) {
		dprintf("%s skipping status %d\n", __func__, new_status);
		return (0);
	}

	dprintf("%s new_status is Offline %d\n", __func__, new_status);

	/* Allocate struct to pass to event callback */
	/* Allocating with lock held, use KM_NOSLEEP */
	sc = (status_change_args_t *)kmem_alloc(sizeof (status_change_args_t),
	    KM_NOSLEEP);
	if (!sc) {
		dprintf("%s couldn't allocate callback struct\n",
		    __func__);
		return (ENOMEM);
	}
	sc->lhp = lhp;
	sc->new_status = new_status;

	mutex_exit(&lhp->lh_lock);	/* Currently needs to drop lock */
	handle_status_change_callback((void *)sc);
	mutex_enter(&lhp->lh_lock);	/* Retake before return */

	return (0);
}

/* Protected by handle lock */
int
handle_status_change(struct ldi_handle *lhp, int new_status)
{
	int error;

	/* Validate lh and new_status */
	if (!lhp) {
		dprintf("%s missing handle\n", __func__);
		return (EINVAL);
	}
	if (new_status > LDI_STATUS_ONLINE) {
		dprintf("%s invalid state %d\n", __func__, new_status);
		return (EINVAL);
	}

	mutex_enter(&lhp->lh_lock);
	error = handle_status_change_locked(lhp, new_status);
	mutex_exit(&lhp->lh_lock);

	return (error);
}

/* Protected by hash list lock */
void
handle_hold_locked(struct ldi_handle *lhp)
{
#ifdef DEBUG
	int index;

	ASSERT3U(lhp, !=, NULL);
	index = LH_HASH(lhp->lh_dev);
	ASSERT(MUTEX_HELD(&ldi_handle_hash_lock[index]));
#endif

	/* Increment ref count and kstat */
	lhp->lh_ref++;
	LDISTAT_BUMP(handle_refs);
}

/* Protected by hash list lock */
void
handle_hold(struct ldi_handle *lhp)
{
	int index;

	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_dev, !=, 0);

	index = LH_HASH(lhp->lh_dev);
	mutex_enter(&ldi_handle_hash_lock[index]);
	handle_hold_locked(lhp);
	mutex_exit(&ldi_handle_hash_lock[index]);
}

/*
 * Locate existing handle in linked list, may return NULL. Optionally places a
 * hold on found handle.
 */
static struct ldi_handle *
handle_find_locked(dev_t device, int fmode, boolean_t hold)
{
	struct ldi_handle *retlhp = NULL, *lhp;
	int index = LH_HASH(device);

	/* Validate device */
	if (device == 0) {
		dprintf("%s invalid device\n", __func__);
		return (NULL);
	}
	/* If fmode is 0, find any handle with matching dev_t */

	ASSERT(MUTEX_HELD(&ldi_handle_hash_lock[index]));

	/* Iterate over handle hash list */
	for (lhp = list_head(&ldi_handle_hash_list[index]);
	    lhp != NULL;
	    lhp = list_next(&ldi_handle_hash_list[index], lhp)) {
		/* Check for matching dev_t and fmode (if set) */
		if (lhp->lh_dev != device) {
			continue;
		}

		/* Special case for find any */
		if (fmode == 0) {
			/* Found a match */
			retlhp = lhp;
			break;
		}

		/* fmode must match write level */
		if (((lhp->lh_fmode & FWRITE) && !(fmode & FWRITE)) ||
		    (!(lhp->lh_fmode & FWRITE) && (fmode & FWRITE))) {
			continue;
		}

		/* Found a match */
		retlhp = lhp;
		break;
	}

	/* Take hold, if requested */
	if (hold && retlhp) {
		/* Caller asked for hold on found handle */
		handle_hold_locked(retlhp);
	}

	return (retlhp);
}

/*
 * Call without lock held to find a handle by dev_t,
 * optionally placing a hold on the found handle.
 */
struct ldi_handle *
handle_find(dev_t device, int fmode, boolean_t hold)
{
	struct ldi_handle *lhp;
	int index = LH_HASH(device);

	if (device == 0) {
		dprintf("%s invalid device\n", __func__);
		return (NULL);
	}

	/* Lock for duration of find */
	mutex_enter(&ldi_handle_hash_lock[index]);

	/* Find handle by dev_t (with hold) */
	lhp = handle_find_locked(device, fmode, hold);

	/* Unlock and return handle (could be NULL) */
	mutex_exit(&ldi_handle_hash_lock[index]);
	return (lhp);
}

static void
handle_free(struct ldi_handle *lhp)
{
	ASSERT3U(lhp, !=, NULL);

	/* Validate lhp, references, and status */
	if (lhp->lh_ref != 0 ||
	    lhp->lh_status != LDI_STATUS_CLOSED) {
		dprintf("%s ref %d status %d\n", __func__, lhp->lh_ref,
		    lhp->lh_status);
	}

	/* Remove notification handler */
	if (handle_remove_notifier(lhp) != 0) {
		dprintf("%s lhp %p notifier %s\n",
		    __func__, lhp, "couldn't be removed");
	}

	/* Destroy condvar and mutex */
	cv_destroy(&lhp->lh_cv);
	mutex_destroy(&lhp->lh_lock);

	/* Decrement kstat handle count */
	LDISTAT_BUMPDOWN(handle_count);
	/* IOKit or vnode */
	switch (lhp->lh_type) {
	case LDI_TYPE_IOKIT:
		/* Decrement kstat handle count and free iokit_tsd */
		LDISTAT_BUMPDOWN(handle_count_iokit);
		handle_free_iokit(lhp);
		break;

	case LDI_TYPE_VNODE:
		/* Decrement kstat handle count and free vnode_tsd */
		LDISTAT_BUMPDOWN(handle_count_vnode);
		handle_free_vnode(lhp);
		break;
	default:
		dprintf("%s invalid handle type\n", __func__);
		break;
	}

	/* Deallocate handle */
	dprintf("%s freeing %p\n", __func__, lhp);
	kmem_free(lhp, sizeof (struct ldi_handle));
	lhp = 0;
}

/*
 * Remove handle from list, decrementing counters
 */
static void
handle_remove_locked(struct ldi_handle *lhp)
{
	int index;

	ASSERT3U(lhp, !=, NULL);
	index = LH_HASH(lhp->lh_dev);
	ASSERT(MUTEX_HELD(&ldi_handle_hash_lock[index]));

	/* Remove from list, update handle count */
	list_remove(&ldi_handle_hash_list[index], lhp);
	OSDecrementAtomic(&ldi_handle_hash_count);
}

void
handle_remove(struct ldi_handle *lhp)
{
	int index = LH_HASH(lhp->lh_dev);

	mutex_enter(&ldi_handle_hash_lock[index]);
	handle_remove_locked(lhp);
	mutex_exit(&ldi_handle_hash_lock[index]);
}

/* Protected by hash list lock */
static void
handle_release_locked(struct ldi_handle *lhp)
{
	boolean_t lastrelease = B_FALSE;

#ifdef DEBUG
	ASSERT3U(lhp, !=, NULL);
	int index = LH_HASH(lhp->lh_dev);
	ASSERT(MUTEX_HELD(&ldi_handle_hash_lock[index]));
#endif

	if (lhp->lh_ref != 0) {
		lhp->lh_ref--;
		LDISTAT_BUMPDOWN(handle_refs);
	} else {
		dprintf("%s with 0 refs\n", __func__);
	}

	dprintf("%s %x remaining holds\n", __func__, lhp->lh_ref);

	/* If last open ref was dropped */
	lastrelease = (lhp->lh_ref == 0);

	if (lastrelease) {
		dprintf("%s removing handle %p from list\n", __func__, lhp);
		handle_remove_locked(lhp);
		dprintf("%s freeing handle %p\n", __func__, lhp);
		handle_free(lhp);
	}
}

/* Protected by hash list lock */
void
handle_release(struct ldi_handle *lhp)
{
	int index;

	ASSERT3U(lhp, !=, NULL);
	index = LH_HASH(lhp->lh_dev);

	mutex_enter(&ldi_handle_hash_lock[index]);
	handle_release_locked(lhp);
	mutex_exit(&ldi_handle_hash_lock[index]);
}

/*
 * Add new handle to list.
 */
static struct ldi_handle *
handle_add_locked(struct ldi_handle *lhp)
{
	struct ldi_handle *retlhp;
	int index = 0;

	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_dev, !=, 0);

	/* Lock should be held */
	index = LH_HASH(lhp->lh_dev);
	ASSERT(MUTEX_HELD(&ldi_handle_hash_lock[index]));

	/* Search for existing handle */
	if ((retlhp = handle_find_locked(lhp->lh_dev, lhp->lh_fmode,
	    B_TRUE)) != NULL) {
		dprintf("%s found handle %p\n", __func__, retlhp);
		return (retlhp);
	}

	/* Insert into list */
	list_insert_head(&ldi_handle_hash_list[index], lhp);

	/* Update handle count */
	OSIncrementAtomic(&ldi_handle_hash_count);

	/* Return success */
	return (lhp);
}

/*
 * Caller should check if returned handle is the same and free new
 * handle if an existing handle was returned
 */
struct ldi_handle *
handle_add(struct ldi_handle *lhp)
{
	struct ldi_handle *retlhp;
	int index;

	ASSERT3U(lhp, !=, NULL);
	index = LH_HASH(lhp->lh_dev);

	mutex_enter(&ldi_handle_hash_lock[index]);
	retlhp = handle_add_locked(lhp);
	mutex_exit(&ldi_handle_hash_lock[index]);

	return (retlhp);
}

/*
 * Returns a handle with 1 reference and status Closed
 */
#ifdef illumos
static struct ldi_handle *
handle_alloc(vnode_t *vp, struct ldi_ident_t *li)
#else /* illumos */
struct ldi_handle *
handle_alloc_common(uint_t type, dev_t device, int fmode)
#endif /* !illumos */
{
	struct ldi_handle *new_lh;
	size_t len;

	/* Validate arguments */
	if ((type != LDI_TYPE_IOKIT && type != LDI_TYPE_VNODE) ||
	    device == 0 || fmode == 0) {
		dprintf("%s Invalid type %d, device %d, or fmode %d\n",
		    __func__, type, device, fmode);
		return (NULL);
	}

	/* Allocate and verify */
	len = sizeof (struct ldi_handle);
	if (NULL == (new_lh = (struct ldi_handle *)kmem_alloc(len,
	    KM_SLEEP))) {
		dprintf("%s couldn't allocate ldi_handle\n", __func__);
		return (NULL);
	}
#ifdef LDI_ZERO
	/* Clear the struct for safety */
	bzero(new_lh, len);
#endif

	/* Create handle lock */
	mutex_init(&new_lh->lh_lock, NULL, MUTEX_DEFAULT, NULL);
	/* And condvar */
	cv_init(&new_lh->lh_cv, NULL, CV_DEFAULT, NULL);

	/*
	 * Set the handle type, which dictates the type of device pointer
	 * and buffers used for the lifetime of the ldi_handle
	 */
	new_lh->lh_type = type;
	/* Set dev_t (major/minor) device number */
	new_lh->lh_dev = device;

	/* Clear list head */
	new_lh->lh_node.list_next = NULL;
	new_lh->lh_node.list_prev = NULL;

	/* Initialize with 1 handle ref and 0 open refs */
	new_lh->lh_ref = 1;
	new_lh->lh_openref = 0;

	/* Clear type-specific device data */
	new_lh->lh_tsd.iokit_tsd = 0;
	/* No need to clear vnode_tsd in union */
	new_lh->lh_notifier = 0;

	/* Assign fmode */
	new_lh->lh_fmode = fmode;

	/* Alloc in status Closed */
	new_lh->lh_status = LDI_STATUS_CLOSED;

	/* Increment kstats */
	LDISTAT_BUMP(handle_count);
	LDISTAT_BUMP(handle_refs);
	if (type == LDI_TYPE_IOKIT) {
		LDISTAT_BUMP(handle_count_iokit);
	} else if (type == LDI_TYPE_VNODE) {
		LDISTAT_BUMP(handle_count_vnode);
	}

	return (new_lh);
}

static void
handle_set_open_locked(struct ldi_handle *lhp)
{
	ASSERT3U(lhp, !=, NULL);
	ASSERT(MUTEX_HELD(&lhp->lh_lock));

	/* Increment number of open clients */
	lhp->lh_openref++;

	/* Increment kstats */
	if (lhp->lh_fmode & FWRITE) {
		LDISTAT_BUMP(handle_open_rw);
	} else {
		LDISTAT_BUMP(handle_open_ro);
	}
}

#if 0
static void
handle_set_open(struct ldi_handle *lhp)
{
	ASSERT3U(lhp, !=, NULL);

	mutex_enter(&lhp->lh_lock);
	handle_set_open_locked(lhp);
	mutex_exit(&lhp->lh_lock);
}
#endif

static void
handle_clear_open_locked(struct ldi_handle *lhp)
{
	ASSERT3U(lhp, !=, NULL);
	ASSERT(MUTEX_HELD(&lhp->lh_lock));

	/* Decrement number of open clients */
	if (lhp->lh_openref == 0) {
		dprintf("%s with 0 open refs\n", __func__);
		return;
	}

	/* Decrement kstats */
	lhp->lh_openref--;
	if (lhp->lh_fmode & FWRITE) {
		LDISTAT_BUMPDOWN(handle_open_rw);
	} else {
		LDISTAT_BUMPDOWN(handle_open_ro);
	}
}

#if 0
static inline void
handle_clear_open(struct ldi_handle *lhp)
{
	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_dev, !=, 0);
	ASSERT3U(lhp->lh_openref, !=, 0);

	mutex_enter(&lhp->lh_lock);
	handle_clear_open_locked(lhp, lhp->lh_fmode);
	mutex_exit(&lhp->lh_lock);
}
#endif

static int
handle_close(struct ldi_handle *lhp)
{
#ifdef DEBUG
	int openrefs;
#endif
	int error = EINVAL;

	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_ref, !=, 0);
	ASSERT3U(lhp->lh_openref, !=, 0);
	ASSERT(lhp->lh_type == LDI_TYPE_IOKIT ||
	    lhp->lh_type == LDI_TYPE_VNODE);

	/* Take lock */
	mutex_enter(&lhp->lh_lock);

	/*
	 * Possible statuses:
	 * Online with one or more openref
	 * Offline due to IOMedia termination, one or more openref remain
	 * Impossible or programming error:
	 * Closing and Closed should only be set with 0 openref
	 * Opening should have 0 openref so far, and clients should not be
	 * calling ldi_close
	 */
	switch (lhp->lh_status) {
	case LDI_STATUS_ONLINE:
		if (lhp->lh_openref == 0) {
			/* Unlock and return error */
			mutex_exit(&lhp->lh_lock);
			/* Shouldn't happen */
			dprintf("%s status Online with 0 openrefs\n",
			    __func__);
			return (ENXIO);
		}

		/* If multiple open refs are held */
		if (lhp->lh_openref > 1) {
			goto drop_openref;
		}

		/* Otherwise open with last open ref */
		/* change status to closing and proceed */
		handle_status_change_locked(lhp, LDI_STATUS_CLOSING);
		/* Unlock and exit loop */
		mutex_exit(&lhp->lh_lock);
		goto do_close;

	case LDI_STATUS_OFFLINE:
		if (lhp->lh_openref == 0) {
			/* Unlock and return error */
			mutex_exit(&lhp->lh_lock);
			/* Shouldn't happen */
			dprintf("%s status Offline with 0 openrefs\n",
			    __func__);
			return (ENXIO);
		}

		/*
		 * Otherwise the device was marked missing and clients need
		 * to drop openrefs until it can be released.
		 */
		goto drop_openref;

	default:
		mutex_exit(&lhp->lh_lock);
		dprintf("%s invalid handle status %d\n",
		    __func__, lhp->lh_status);
		return (ENXIO);
	}

drop_openref:
	/* Just decrement open refs/stats */
	handle_clear_open_locked(lhp);
#ifdef DEBUG
	/* Save openrefs to report after unlock */
	openrefs = lhp->lh_openref;
#endif
	mutex_exit(&lhp->lh_lock);

#ifdef DEBUG
	dprintf("%s has %d remaining openrefs\n", __func__, openrefs);
#endif
	return (0);

do_close:
	/* Remove notification handler */
	if (lhp->lh_notifier) {
		error = handle_remove_notifier(lhp);
		if (error) {
			dprintf("%s lhp %p notifier %p error %d %s\n",
			    __func__, lhp, lhp->lh_notifier, error,
			    "couldn't be removed");
			/* Proceeds with close */
		}
	}

	/* IOMedia or vnode */
	switch (lhp->lh_type) {
	case LDI_TYPE_IOKIT:
		error = handle_close_iokit(lhp);
		/* Preserve error for return */
		break;
	case LDI_TYPE_VNODE:
		error = handle_close_vnode(lhp);
		/* Preserve error for return */
		break;
	}

#ifdef DEBUG
	if (error != 0) {
		/* We will still set the handle to Closed status */
		dprintf("%s error %d from handle_close_{type}\n",
		    __func__, error);
	}
#endif

	/* Take lock to drop openref and set status */
	mutex_enter(&lhp->lh_lock);
	handle_clear_open_locked(lhp);
	handle_status_change_locked(lhp, LDI_STATUS_CLOSED);

	/* Wake any waiting opens and unlock */
	cv_signal(&lhp->lh_cv);
	mutex_exit(&lhp->lh_lock);

dprintf("%s returning %d\n", __func__, error);
	return (error);
}

ldi_status_t
handle_open_start(struct ldi_handle *lhp)
{
	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_ref, !=, 0);

	/* Take lock */
	mutex_enter(&lhp->lh_lock);
	/* Loop if the handle is in opening or closing status */
	do {
		/* XXX Needs sleep timeout */
		switch (lhp->lh_status) {
		case LDI_STATUS_ONLINE:
			/* Increment readonly / readwrite count */
			handle_set_open_locked(lhp);
			mutex_exit(&lhp->lh_lock);

			/* Success */
			return (LDI_STATUS_ONLINE);

		case LDI_STATUS_CLOSED:
			/* Not yet open, change status to opening and proceed */
			handle_status_change_locked(lhp, LDI_STATUS_OPENING);

			/* Unlock and exit loop */
			mutex_exit(&lhp->lh_lock);
			/* Return success */
			return (LDI_STATUS_OPENING);

		case LDI_STATUS_OPENING:
		case LDI_STATUS_CLOSING:
			/* Open or close in progress, sleep until signaled */
			dprintf("%s sleeping on lock\n", __func__);
			cv_wait(&lhp->lh_cv, &lhp->lh_lock);
			continue;
		default:
			mutex_exit(&lhp->lh_lock);
			dprintf("%s invalid handle status %d\n",
			    __func__, lhp->lh_status);
			return (LDI_STATUS_OFFLINE);
		}
	} while (1);

	/* Shouldn't reach this */
	return (LDI_STATUS_CLOSED);
}

void
handle_open_done(struct ldi_handle *lhp, ldi_status_t new_status)
{
	ASSERT3U(lhp, !=, NULL);
	ASSERT3U(lhp->lh_status, ==, LDI_STATUS_OPENING);

	/* Lock to change status */
	mutex_enter(&lhp->lh_lock);

	if (new_status != LDI_STATUS_ONLINE) {
		/* Set status, issues event */
		handle_status_change_locked(lhp, LDI_STATUS_CLOSED);
	} else {
		/* Increment open count and fmode */
		handle_set_open_locked(lhp);
		/* Set status, issues event */
		handle_status_change_locked(lhp, LDI_STATUS_ONLINE);
	}

	/* Wake any waiting opens and unlock */
	cv_signal(&lhp->lh_cv);
	mutex_exit(&lhp->lh_lock);

	/*
	 * Flush out any old buffers remaining from
	 * a previous use, only if opening read-write.
	 */
	if (new_status == LDI_STATUS_ONLINE &&
	    (lhp->lh_fmode & FWRITE) &&
	    ldi_sync((ldi_handle_t)lhp) != 0) {
		dprintf("%s ldi_sync failed\n", __func__);
	}
}

/*
 * Release all remaining handles (during ldi_fini)
 * Unless something went wrong, all handles should
 * be closed and have zero references.
 */
static void
handle_hash_release()
{
	struct ldi_handle *lhp;
	int index, refs, j;

	for (index = 0; index < LH_HASH_SZ; index++) {
		mutex_enter(&ldi_handle_hash_lock[index]);
		if (!list_empty(&ldi_handle_hash_list[index])) {
			dprintf("%s still have LDI handle(s) in list %d\n",
			    __func__, index);
		}

		/* Iterate over the list */
		while ((lhp = list_head(&ldi_handle_hash_list[index]))) {
			/* remove from list to deallocate */
			list_remove(&ldi_handle_hash_list[index], lhp);

			/* Update handle count */
			OSDecrementAtomic(&ldi_handle_hash_count);

			dprintf("%s releasing %p with %u refs and status %d\n",
			    __func__, lhp, lhp->lh_ref, lhp->lh_status);
			/* release holds */
			refs = lhp->lh_ref;
			for (j = 0; j < refs; j++) {
				handle_release_locked(lhp);
			}
			lhp = 0;
		}

		list_destroy(&ldi_handle_hash_list[index]);
		mutex_exit(&ldi_handle_hash_lock[index]);
		mutex_destroy(&ldi_handle_hash_lock[index]);
	}
}

/*
 * LDI Event functions
 */
char *
ldi_ev_get_type(ldi_ev_cookie_t cookie)
{
	int i;
	struct ldi_ev_cookie *cookie_impl = (struct ldi_ev_cookie *)cookie;

	for (i = 0; ldi_ev_cookies[i].ck_evname != NULL; i++) {
		if (&ldi_ev_cookies[i] == cookie_impl) {
			LDI_EVTRC((CE_NOTE, "ldi_ev_get_type: LDI: %s",
			    ldi_ev_cookies[i].ck_evname));
			return (ldi_ev_cookies[i].ck_evname);
		}
	}

	return ("UNKNOWN EVENT");
}

static int
ldi_native_cookie(ldi_ev_cookie_t cookie)
{
	int i;
	struct ldi_ev_cookie *cookie_impl = (struct ldi_ev_cookie *)cookie;

	for (i = 0; ldi_ev_cookies[i].ck_evname != NULL; i++) {
		if (&ldi_ev_cookies[i] == cookie_impl) {
			LDI_EVTRC((CE_NOTE, "ldi_native_cookie: native LDI"));
			return (1);
		}
	}

	LDI_EVTRC((CE_NOTE, "ldi_native_cookie: is NDI"));
	return (0);
}

static ldi_ev_cookie_t
ldi_get_native_cookie(const char *evname)
{
	int i;

	for (i = 0; ldi_ev_cookies[i].ck_evname != NULL; i++) {
		if (strcmp(ldi_ev_cookies[i].ck_evname, evname) == 0) {
			LDI_EVTRC((CE_NOTE, "ldi_get_native_cookie: found"));
			return ((ldi_ev_cookie_t)&ldi_ev_cookies[i]);
		}
	}

	LDI_EVTRC((CE_NOTE, "ldi_get_native_cookie: NOT found"));
	return (NULL);
}

/*
 * ldi_ev_lock() needs to be recursive, since layered drivers may call
 * other LDI interfaces (such as ldi_close() from within the context of
 * a notify callback. Since the notify callback is called with the
 * ldi_ev_lock() held and ldi_close() also grabs ldi_ev_lock, the lock needs
 * to be recursive.
 */
static void
ldi_ev_lock(void)
{
	LDI_EVTRC((CE_NOTE, "ldi_ev_lock: entered"));

	mutex_enter(&ldi_ev_callback_list.le_lock);
	if (ldi_ev_callback_list.le_thread == curthread) {
		ASSERT(ldi_ev_callback_list.le_busy >= 1);
		ldi_ev_callback_list.le_busy++;
	} else {
		while (ldi_ev_callback_list.le_busy)
			cv_wait(&ldi_ev_callback_list.le_cv,
			    &ldi_ev_callback_list.le_lock);
		ASSERT(ldi_ev_callback_list.le_thread == NULL);
		ldi_ev_callback_list.le_busy = 1;
		ldi_ev_callback_list.le_thread = curthread;
	}
	mutex_exit(&ldi_ev_callback_list.le_lock);

	LDI_EVTRC((CE_NOTE, "ldi_ev_lock: exit"));
}

static void
ldi_ev_unlock(void)
{
	LDI_EVTRC((CE_NOTE, "ldi_ev_unlock: entered"));
	mutex_enter(&ldi_ev_callback_list.le_lock);
	ASSERT(ldi_ev_callback_list.le_thread == curthread);
	ASSERT(ldi_ev_callback_list.le_busy >= 1);

	ldi_ev_callback_list.le_busy--;
	if (ldi_ev_callback_list.le_busy == 0) {
		ldi_ev_callback_list.le_thread = NULL;
		cv_signal(&ldi_ev_callback_list.le_cv);
	}
	mutex_exit(&ldi_ev_callback_list.le_lock);
	LDI_EVTRC((CE_NOTE, "ldi_ev_unlock: exit"));
}

int
ldi_ev_get_cookie(ldi_handle_t lh, char *evname, ldi_ev_cookie_t *cookiep)
{
	ldi_ev_cookie_t		tcookie;

	LDI_EVDBG((CE_NOTE, "ldi_ev_get_cookie: entered: evname=%s",
	    evname ? evname : "<NULL>"));

	if (lh == NULL || evname == NULL ||
	    strlen(evname) == 0 || cookiep == NULL) {
		LDI_EVDBG((CE_NOTE, "ldi_ev_get_cookie: invalid args"));
		return (LDI_EV_FAILURE);
	}

	*cookiep = NULL;

	/*
	 * First check if it is a LDI native event
	 */
	tcookie = ldi_get_native_cookie(evname);
	if (tcookie) {
		LDI_EVDBG((CE_NOTE, "ldi_ev_get_cookie: got native cookie"));
		*cookiep = tcookie;
		return (LDI_EV_SUCCESS);
	}

	return (LDI_EV_FAILURE);
}

int
ldi_ev_register_callbacks(ldi_handle_t lh, ldi_ev_cookie_t cookie,
    ldi_ev_callback_t *callb, void *arg, ldi_callback_id_t *id)
{
	struct ldi_handle	*lhp = (struct ldi_handle *)lh;
	ldi_ev_callback_impl_t	*lecp;

	if (lh == NULL || cookie == NULL || callb == NULL || id == NULL) {
		LDI_EVDBG((CE_NOTE, "ldi_ev_register_callbacks: Invalid args"));
		return (LDI_EV_FAILURE);
	}

	if (callb->cb_vers != LDI_EV_CB_VERS) {
		LDI_EVDBG((CE_NOTE, "ldi_ev_register_callbacks: Invalid vers"));
		return (LDI_EV_FAILURE);
	}

	if (callb->cb_notify == NULL && callb->cb_finalize == NULL) {
		LDI_EVDBG((CE_NOTE, "ldi_ev_register_callbacks: NULL callb"));
		return (LDI_EV_FAILURE);
	}

	*id = 0;

	lecp = kmem_zalloc(sizeof (ldi_ev_callback_impl_t), KM_SLEEP);

	ldi_ev_lock();

	/*
	 * Add the notify/finalize callback to the LDI's list of callbacks.
	 */
	lecp->lec_lhp = lhp;

	lecp->lec_dev = lhp->lh_dev;
	lecp->lec_spec = S_IFBLK;

	lecp->lec_notify = callb->cb_notify;
	lecp->lec_finalize = callb->cb_finalize;
	lecp->lec_arg = arg;
	lecp->lec_cookie = cookie;

	lecp->lec_id = (void *)(uintptr_t)(++ldi_ev_id_pool);

	list_insert_tail(&ldi_ev_callback_list.le_head, lecp);

	*id = (ldi_callback_id_t)lecp->lec_id;

	ldi_ev_unlock();

	LDI_EVDBG((CE_NOTE, "ldi_ev_register_callbacks: registered "
	    "notify/finalize"));

	return (LDI_EV_SUCCESS);
}

static int
ldi_ev_device_match(ldi_ev_callback_impl_t *lecp, __unused dev_info_t *dip,
    dev_t dev, int spec_type)
{
	ASSERT(lecp);
	ASSERT(dev != DDI_DEV_T_NONE);
	ASSERT(dev != NODEV);
	ASSERT((dev == DDI_DEV_T_ANY && spec_type == 0) ||
	    (spec_type == S_IFCHR || spec_type == S_IFBLK));
	ASSERT(lecp->lec_spec == S_IFCHR || lecp->lec_spec == S_IFBLK);
	ASSERT(lecp->lec_dev != DDI_DEV_T_ANY);
	ASSERT(lecp->lec_dev != DDI_DEV_T_NONE);
	ASSERT(lecp->lec_dev != NODEV);

	if (dev != DDI_DEV_T_ANY) {
		if (dev != lecp->lec_dev || spec_type != lecp->lec_spec)
			return (0);
	}

	LDI_EVTRC((CE_NOTE, "ldi_ev_device_match: MATCH dev=%d",
	    (uint32_t)dev));

	return (1);
}

/*
 * LDI framework function to post a "notify" event to all layered drivers
 * that have registered for that event
 *
 * Returns:
 *		LDI_EV_SUCCESS - registered callbacks allow event
 *		LDI_EV_FAILURE - registered callbacks block event
 *		LDI_EV_NONE    - No matching LDI callbacks
 *
 * This function is *not* to be called by layered drivers. It is for I/O
 * framework code in Solaris, such as the I/O retire code and DR code
 * to call while servicing a device event such as offline or degraded.
 */
int
ldi_invoke_notify(__unused dev_info_t *dip, dev_t dev, int spec_type,
    char *event, void *ev_data)
{
	ldi_ev_callback_impl_t *lecp;
	list_t	*listp;
	int	ret;
	char	*lec_event;

	ASSERT(dev != DDI_DEV_T_NONE);
	ASSERT(dev != NODEV);
	ASSERT((dev == DDI_DEV_T_ANY && spec_type == 0) ||
	    (spec_type == S_IFCHR || spec_type == S_IFBLK));
	ASSERT(event);

	LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): entered: dip=%p, ev=%s",
	    (void *)dip, event));

	ret = LDI_EV_NONE;
	ldi_ev_lock();

	VERIFY(ldi_ev_callback_list.le_walker_next == NULL);
	listp = &ldi_ev_callback_list.le_head;
	for (lecp = list_head(listp); lecp; lecp =
	    ldi_ev_callback_list.le_walker_next) {
		ldi_ev_callback_list.le_walker_next = list_next(listp, lecp);

		/* Check if matching device */
		if (!ldi_ev_device_match(lecp, dip, dev, spec_type))
			continue;

		if (lecp->lec_lhp == NULL) {
			/*
			 * Consumer has unregistered the handle and so
			 * is no longer interested in notify events.
			 */
			LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): No LDI "
			    "handle, skipping"));
			continue;
		}

		if (lecp->lec_notify == NULL) {
			LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): No notify "
			    "callback. skipping"));
			continue;	/* not interested in notify */
		}

		/*
		 * Check if matching event
		 */
		lec_event = ldi_ev_get_type(lecp->lec_cookie);
		if (strcmp(event, lec_event) != 0) {
			LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): Not matching"
			    " event {%s,%s}. skipping", event, lec_event));
			continue;
		}

		lecp->lec_lhp->lh_flags |= LH_FLAGS_NOTIFY;
		if (lecp->lec_notify((ldi_handle_t)lecp->lec_lhp,
		    lecp->lec_cookie, lecp->lec_arg, ev_data) !=
		    LDI_EV_SUCCESS) {
			ret = LDI_EV_FAILURE;
			LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): notify"
			    " FAILURE"));
			break;
		}

		/* We have a matching callback that allows the event to occur */
		ret = LDI_EV_SUCCESS;

		LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): 1 consumer success"));
	}

	if (ret != LDI_EV_FAILURE)
		goto out;

#ifdef __APPLE__
	dprintf("%s offline notify failed, shouldn't happen\n", __func__);
	goto out;
#endif
#ifdef illumos
	LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): undoing notify"));

	/*
	 * Undo notifies already sent
	 */
	lecp = list_prev(listp, lecp);
	VERIFY(ldi_ev_callback_list.le_walker_prev == NULL);
	for (; lecp; lecp = ldi_ev_callback_list.le_walker_prev) {
		ldi_ev_callback_list.le_walker_prev = list_prev(listp, lecp);

		/*
		 * Check if matching device
		 */
		if (!ldi_ev_device_match(lecp, dip, dev, spec_type))
			continue;

		if (lecp->lec_finalize == NULL) {
			LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): no finalize, "
			    "skipping"));
			continue;	/* not interested in finalize */
		}

		/*
		 * it is possible that in response to a notify event a
		 * layered driver closed its LDI handle so it is ok
		 * to have a NULL LDI handle for finalize. The layered
		 * driver is expected to maintain state in its "arg"
		 * parameter to keep track of the closed device.
		 */

		/* Check if matching event */
		lec_event = ldi_ev_get_type(lecp->lec_cookie);
		if (strcmp(event, lec_event) != 0) {
			LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): not matching "
			    "event: %s,%s, skipping", event, lec_event));
			continue;
		}

		LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): calling finalize"));

		lecp->lec_finalize(lecp->lec_lhp, lecp->lec_cookie,
		    LDI_EV_FAILURE, lecp->lec_arg, ev_data);

		/*
		 * If LDI native event and LDI handle closed in context
		 * of notify, NULL out the finalize callback as we have
		 * already called the 1 finalize above allowed in this situation
		 */
		if (lecp->lec_lhp == NULL &&
		    ldi_native_cookie(lecp->lec_cookie)) {
			LDI_EVDBG((CE_NOTE,
			    "ldi_invoke_notify(): NULL-ing finalize after "
			    "calling 1 finalize following ldi_close"));
			lecp->lec_finalize = NULL;
		}
	}
#endif /* illumos */

out:
	ldi_ev_callback_list.le_walker_next = NULL;
	ldi_ev_callback_list.le_walker_prev = NULL;
	ldi_ev_unlock();

	if (ret == LDI_EV_NONE) {
		LDI_EVDBG((CE_NOTE, "ldi_invoke_notify(): no matching "
		    "LDI callbacks"));
	}

	return (ret);
}

/*
 * LDI framework function to invoke "finalize" callbacks for all layered
 * drivers that have registered callbacks for that event.
 *
 * This function is *not* to be called by layered drivers. It is for I/O
 * framework code in Solaris, such as the I/O retire code and DR code
 * to call while servicing a device event such as offline or degraded.
 */
void
ldi_invoke_finalize(__unused dev_info_t *dip, dev_t dev, int spec_type,
    char *event, int ldi_result, void *ev_data)
{
	ldi_ev_callback_impl_t *lecp;
	list_t	*listp;
	char	*lec_event;
	int	found = 0;

	ASSERT(dev != DDI_DEV_T_NONE);
	ASSERT(dev != NODEV);
	ASSERT((dev == DDI_DEV_T_ANY && spec_type == 0) ||
	    (spec_type == S_IFCHR || spec_type == S_IFBLK));
	ASSERT(event);
	ASSERT(ldi_result == LDI_EV_SUCCESS || ldi_result == LDI_EV_FAILURE);

	LDI_EVDBG((CE_NOTE, "ldi_invoke_finalize(): entered: dip=%p, result=%d"
	    " event=%s", (void *)dip, ldi_result, event));

	ldi_ev_lock();
	VERIFY(ldi_ev_callback_list.le_walker_next == NULL);
	listp = &ldi_ev_callback_list.le_head;
	for (lecp = list_head(listp); lecp; lecp =
	    ldi_ev_callback_list.le_walker_next) {
		ldi_ev_callback_list.le_walker_next = list_next(listp, lecp);

		if (lecp->lec_finalize == NULL) {
			LDI_EVDBG((CE_NOTE, "ldi_invoke_finalize(): No "
			    "finalize. Skipping"));
			continue;	/* Not interested in finalize */
		}

		/*
		 * Check if matching device
		 */
		if (!ldi_ev_device_match(lecp, dip, dev, spec_type))
			continue;

		/*
		 * It is valid for the LDI handle to be NULL during finalize.
		 * The layered driver may have done an LDI close in the notify
		 * callback.
		 */

		/*
		 * Check if matching event
		 */
		lec_event = ldi_ev_get_type(lecp->lec_cookie);
		if (strcmp(event, lec_event) != 0) {
			LDI_EVDBG((CE_NOTE, "ldi_invoke_finalize(): Not "
			    "matching event {%s,%s}. Skipping",
			    event, lec_event));
			continue;
		}

		LDI_EVDBG((CE_NOTE, "ldi_invoke_finalize(): calling finalize"));

		found = 1;

		lecp->lec_finalize((ldi_handle_t)lecp->lec_lhp,
		    lecp->lec_cookie, ldi_result, lecp->lec_arg,
		    ev_data);

		/*
		 * If LDI native event and LDI handle closed in context
		 * of notify, NULL out the finalize callback as we have
		 * already called the 1 finalize above allowed in this situation
		 */
		if (lecp->lec_lhp == NULL &&
		    ldi_native_cookie(lecp->lec_cookie)) {
			LDI_EVDBG((CE_NOTE,
			    "ldi_invoke_finalize(): NULLing finalize after "
			    "calling 1 finalize following ldi_close"));
			lecp->lec_finalize = NULL;
		}
	}
	ldi_ev_callback_list.le_walker_next = NULL;
	ldi_ev_unlock();

	if (found)
		return;

	LDI_EVDBG((CE_NOTE, "ldi_invoke_finalize(): no matching callbacks"));
}

int
ldi_ev_remove_callbacks(ldi_callback_id_t id)
{
	ldi_ev_callback_impl_t	*lecp;
	ldi_ev_callback_impl_t	*next;
	ldi_ev_callback_impl_t	*found;
	list_t			*listp;

	if (id == 0) {
		cmn_err(CE_WARN, "ldi_ev_remove_callbacks: Invalid ID 0");
		return (LDI_EV_FAILURE);
	}

	LDI_EVDBG((CE_NOTE, "ldi_ev_remove_callbacks: entered: id=%p",
	    (void *)id));

	ldi_ev_lock();

	listp = &ldi_ev_callback_list.le_head;
	next = found = NULL;
	for (lecp = list_head(listp); lecp; lecp = next) {
		next = list_next(listp, lecp);
		if (lecp->lec_id == id) {
			VERIFY(found == NULL);

			/*
			 * If there is a walk in progress, shift that walk
			 * along to the next element so that we can remove
			 * this one.  This allows us to unregister an arbitrary
			 * number of callbacks from within a callback.
			 *
			 * See the struct definition (in sunldi_impl.h) for
			 * more information.
			 */
			if (ldi_ev_callback_list.le_walker_next == lecp)
				ldi_ev_callback_list.le_walker_next = next;
			if (ldi_ev_callback_list.le_walker_prev == lecp)
				ldi_ev_callback_list.le_walker_prev = list_prev(
				    listp, ldi_ev_callback_list.le_walker_prev);

			list_remove(listp, lecp);
			found = lecp;
		}
	}
	ldi_ev_unlock();

	if (found == NULL) {
		cmn_err(CE_WARN, "No LDI event handler for id (%p)",
		    (void *)id);
		return (LDI_EV_SUCCESS);
	}

	LDI_EVDBG((CE_NOTE, "ldi_ev_remove_callbacks: removed "
	    "LDI native callbacks"));
	kmem_free(found, sizeof (ldi_ev_callback_impl_t));

	return (LDI_EV_SUCCESS);
}
/*
 * XXX End LDI Events
 */

/* Client interface, find IOMedia from dev_t, alloc and open handle */
int
ldi_open_by_dev(dev_t device, __unused int otyp, int fmode,
    __unused cred_t *cred, ldi_handle_t *lhp,
    __unused ldi_ident_t ident)
{
	int error = EINVAL;

	dprintf("%s dev_t %d fmode %d\n", __func__, device, fmode);

	/* Validate arguments */
	if (!lhp || device == 0) {
		dprintf("%s missing argument %p %d\n",
		    __func__, lhp, device);
		return (EINVAL);
	}
	/* In debug build, be loud if we potentially leak a handle */
	ASSERT3U(*((struct ldi_handle **)lhp), ==, NULL);

	/* Try to open by media */
	error = ldi_open_media_by_dev(device, fmode, lhp);

	/* Pass error from open */
	return (error);
}

/* Client interface, find dev_t and IOMedia/vnode, alloc and open handle */
int
ldi_open_by_name(char *path, int fmode, __unused cred_t *cred,
    ldi_handle_t *lhp, __unused ldi_ident_t li)
{
	dev_t device = 0;
	int error = EINVAL;

	dprintf("%s dev_t %d fmode %d\n", __func__, device, fmode);

	/* Validate arguments */
	if (!lhp || !path) {
		dprintf("%s %s %p %s %d\n", __func__,
		    "missing lhp or path", lhp, path, fmode);
		return (EINVAL);
	}
	/* In debug build, be loud if we potentially leak a handle */
	ASSERT3U(*((struct ldi_handle **)lhp), ==, NULL);

	/* Validate active open modes */
	if (!ldi_use_iokit_from_path && !ldi_use_dev_from_path &&
	    !ldi_use_vnode_from_path) {
		dprintf("%s no valid modes to open device\n", __func__);
		return (EINVAL);
	}

	/* Try to open IOMedia by path */
	if (ldi_use_iokit_from_path) {
		error = ldi_open_media_by_path(path, fmode, lhp);

		/* Error check open */
		if (!error) {
			return (0);
		} else {
			dprintf("%s ldi_open_media_by_path failed\n",
			    __func__);
			/* Not fatal, retry by dev_t or vnode */
		}
	}

	/* Get dev_t from path, try to open IOMedia by dev */
	if (ldi_use_dev_from_path) {
		/* Uses vnode_lookup */
		device = dev_from_path(path);
		if (device == 0) {
			dprintf("%s dev_from_path failed %s\n",
			    __func__, path);
			/*
			 * Both media_from_dev and vnode_from_path will fail
			 * if dev_from_path fails, since it uses vnode_lookup.
			 */
			return (ENODEV);
		}

		if (ldi_use_iokit_from_dev) {
			/* Searches for matching IOMedia */
			error = ldi_open_media_by_dev(device, fmode, lhp);
			if (!error) {
				return (0);
			} else {
				dprintf("%s ldi_open_media_by_dev failed %d\n",
				    __func__, device);
				/* Not fatal, retry as vnode */
			}
		}
	}

	if (!ldi_use_vnode_from_path) {
		return (EINVAL);
	}

	/* Try to open vnode by path */
	error = ldi_open_vnode_by_path(path, device, fmode, lhp);
	if (error) {
		dprintf("%s ldi_open_vnode_by_path failed %d\n", __func__,
		    error);
	}

	return (error);
}

/* Client interface, wrapper for handle_close */
int
ldi_close(ldi_handle_t lh, int fmode, __unused cred_t *cred)
{
	struct ldi_handle	*handlep = (struct ldi_handle *)lh;
	int			error = EINVAL;

	ASSERT3U(handlep, !=, NULL);
	ASSERT3U(handlep->lh_ref, !=, 0);
	ASSERT3U(handlep->lh_fmode, ==, fmode);

	dprintf("%s dev_t %d fmode %d\n", __func__, handlep->lh_dev, fmode);

	/* Remove event callbacks */
	boolean_t		notify = B_FALSE;
	list_t			*listp;
	ldi_ev_callback_impl_t	*lecp;

	/*
	 * Search the event callback list for callbacks with this
	 * handle. There are 2 cases
	 * 1. Called in the context of a notify. The handle consumer
	 *    is releasing its hold on the device to allow a reconfiguration
	 *    of the device. Simply NULL out the handle and the notify callback.
	 *    The finalize callback is still available so that the consumer
	 *    knows of the final disposition of the device.
	 * 2. Not called in the context of notify. NULL out the handle as well
	 *    as the notify and finalize callbacks. Since the consumer has
	 *    closed the handle, we assume it is not interested in the
	 *    notify and finalize callbacks.
	 */
	ldi_ev_lock();

	if (handlep->lh_flags & LH_FLAGS_NOTIFY)
		notify = B_TRUE;
	listp = &ldi_ev_callback_list.le_head;
	for (lecp = list_head(listp); lecp; lecp = list_next(listp, lecp)) {
		if (lecp->lec_lhp != handlep)
			continue;
		lecp->lec_lhp = NULL;
		lecp->lec_notify = NULL;
		LDI_EVDBG((CE_NOTE, "ldi_close: NULLed lh and notify"));
		if (!notify) {
			LDI_EVDBG((CE_NOTE, "ldi_close: NULLed finalize"));
			lecp->lec_finalize = NULL;
		}
	}

	if (notify)
		handlep->lh_flags &= ~LH_FLAGS_NOTIFY;
	ldi_ev_unlock();

	/* Close device if only one openref, or just decrement openrefs */
	if ((error = handle_close(handlep)) != 0) {
		dprintf("%s error from handle_close: %d\n",
		    __func__, error);
	}

	/* Decrement lh_ref, if last ref then remove and free */
	handle_release(handlep);
	handlep = 0;

	/* XXX clear pointer arg, and return success? */
	lh = (ldi_handle_t)0;
	return (0);
	// return (error);
}

/*
 * Client interface, must be in LDI_STATUS_ONLINE
 */
int
ldi_get_size(ldi_handle_t lh, uint64_t *dev_size)
{
	struct ldi_handle *handlep = (struct ldi_handle *)lh;
	int error;

	/*
	 * Ensure we have an LDI handle, and a valid dev_size and/or
	 * blocksize pointer. Caller must pass at least one of these.
	 */
	if (!handlep || !dev_size) {
		dprintf("%s handle %p\n", __func__, handlep);
		dprintf("%s dev_size %p\n", __func__, dev_size);
		return (EINVAL);
	}

	/*
	 * Must be in LDI_STATUS_ONLINE
	 * IOMedia can return getSize without being opened, but vnode
	 * devices must be opened first.
	 * Rather than have support differing behaviors, require that
	 * handle is open to retrieve the size.
	 */
	if (handlep->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s device not online\n", __func__);
		return (ENODEV);
	}

	/* IOMedia or vnode */
	switch (handlep->lh_type) {
	case LDI_TYPE_IOKIT:
		error = handle_get_size_iokit(handlep, dev_size);
		return (error);

	case LDI_TYPE_VNODE:
		error = handle_get_size_vnode(handlep, dev_size);
		return (error);
	}

	/* Default case, shouldn't reach this */
	dprintf("%s invalid lh_type %d\n", __func__,
	    handlep->lh_type);
	return (EINVAL);
}

/*
 * Must be in LDI_STATUS_ONLINE
 * XXX Needs async callback
 */
int
ldi_sync(ldi_handle_t lh)
{
	struct ldi_handle *handlep = (struct ldi_handle *)lh;
	int error;

	/* Ensure we have an LDI handle */
	if (!handlep) {
		dprintf("%s no handle\n", __func__);
		return (EINVAL);
	}

	/* Must be in LDI_STATUS_ONLINE */
	if (handlep->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s device not online\n", __func__);
		return (ENODEV);
	}

	/* IOMedia or vnode */
	switch (handlep->lh_type) {
	case LDI_TYPE_IOKIT:
		error = handle_sync_iokit(handlep);
		return (error);

	case LDI_TYPE_VNODE:
		error = handle_sync_vnode(handlep);
		return (error);
	}

	/* Default case, shouldn't reach this */
	dprintf("%s invalid lh_type %d\n", __func__,
	    handlep->lh_type);
	return (EINVAL);
}

int
ldi_ioctl(ldi_handle_t lh, int cmd, intptr_t arg,
    __unused int mode, __unused cred_t *cr, __unused int *rvalp)
{
	struct ldi_handle *handlep = (struct ldi_handle *)lh;
	int error = EINVAL;
	struct dk_callback *dkc;

	switch (cmd) {
	/* Flush write cache */
	case DKIOCFLUSHWRITECACHE:
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			error = handle_sync_iokit(handlep);
			break;

		case LDI_TYPE_VNODE:
			error = handle_sync_vnode(handlep);
			break;

		default:
			error = ENOTSUP;
		}

		if (!arg) {
			return (error);
		}

		dkc = (struct dk_callback *)arg;
		/* Issue completion callback if set */
		if (dkc->dkc_callback) {
			(*dkc->dkc_callback)(dkc->dkc_cookie, error);
		}

		return (error);

	/* Set or clear write cache enabled */
	case DKIOCSETWCE:
		/*
		 * There doesn't seem to be a way to do this by vnode,
		 * so we need to be able to locate an IOMedia and an
		 * IOBlockStorageDevice provider.
		 */
		return (handle_set_wce_iokit(handlep, (int *)arg));

	/* Get media blocksize and block count */
	case DKIOCGMEDIAINFO:
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_get_media_info_iokit(handlep,
			    (struct dk_minfo *)arg));

		case LDI_TYPE_VNODE:
			return (handle_get_media_info_vnode(handlep,
			    (struct dk_minfo *)arg));

		default:
			return (ENOTSUP);
		}

	/* Get media logical/physical blocksize and block count */
	case DKIOCGMEDIAINFOEXT:
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_get_media_info_ext_iokit(handlep,
			    (struct dk_minfo_ext *)arg));

		case LDI_TYPE_VNODE:
			return (handle_get_media_info_ext_vnode(handlep,
			    (struct dk_minfo_ext *)arg));

		default:
			return (ENOTSUP);
		}

	/* Check device status */
	case DKIOCSTATE:
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_check_media_iokit(handlep,
			    (int *)arg));

		case LDI_TYPE_VNODE:
			return (handle_check_media_vnode(handlep,
			    (int *)arg));

		default:
			return (ENOTSUP);
		}

	case DKIOCISSOLIDSTATE:
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_is_solidstate_iokit(handlep,
			    (int *)arg));

		case LDI_TYPE_VNODE:
			return (handle_is_solidstate_vnode(handlep,
			    (int *)arg));

		default:
			return (ENOTSUP);
		}

	case DKIOCGETBOOTINFO:
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_get_bootinfo_iokit(handlep,
			    (struct io_bootinfo *)arg));

		case LDI_TYPE_VNODE:
			return (handle_get_bootinfo_vnode(handlep,
			    (struct io_bootinfo *)arg));

		default:
			return (ENOTSUP);
		}

	case DKIOCGETFEATURES: /* UNMAP? */
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_features_iokit(handlep,
			    (uint32_t *)arg));

		case LDI_TYPE_VNODE:
			return (handle_features_vnode(handlep,
			    (uint32_t *)arg));

		default:
			return (ENOTSUP);
		}

	case DKIOCFREE: /* UNMAP */
		/* IOMedia or vnode */
		switch (handlep->lh_type) {
		case LDI_TYPE_IOKIT:
			return (handle_unmap_iokit(handlep,
			    (dkioc_free_list_ext_t *)arg));

		case LDI_TYPE_VNODE:
			return (handle_unmap_vnode(handlep,
			    (dkioc_free_list_ext_t *)arg));

		default:
			return (ENOTSUP);
		}

	default:
		return (ENOTSUP);
	}
}

/*
 * Must already have handle_open called on lh.
 */
int
ldi_strategy(ldi_handle_t lh, ldi_buf_t *lbp)
{
	struct ldi_handle *handlep = (struct ldi_handle *)lh;
	int error = EINVAL;

	/* Verify arguments */
	if (!handlep || !lbp || lbp->b_bcount == 0) {
		dprintf("%s missing something...\n", __func__);
		dprintf("handlep [%p]\n", handlep);
		dprintf("lbp [%p]\n", lbp);
		if (lbp) {
			dprintf("lbp->b_bcount %llu\n",
			    lbp->b_bcount);
		}
		return (EINVAL);
	}

	/* Check instantaneous value of handle status */
	if (handlep->lh_status != LDI_STATUS_ONLINE) {
		dprintf("%s device not online\n", __func__);
		return (ENODEV);
	}

	/* IOMedia or vnode */
	/* Issue type-specific buf_strategy, preserve error */
	switch (handlep->lh_type) {
	case LDI_TYPE_IOKIT:
		error = buf_strategy_iokit(lbp, handlep);
		break;
	case LDI_TYPE_VNODE:
		error = buf_strategy_vnode(lbp, handlep);
		break;
	default:
		dprintf("%s invalid lh_type %d\n", __func__, handlep->lh_type);
		return (EINVAL);
	}

	return (error);
}

/* Client interface to get an LDI buffer */
ldi_buf_t *
ldi_getrbuf(int flags)
{
/* Example: bp = getrbuf(KM_SLEEP); */
	ldi_buf_t *lbp;

	/* Allocate with requested flags */
	lbp = kmem_alloc(sizeof (ldi_buf_t), flags);
	/* Verify allocation */
	if (!lbp) {
		return (NULL);
	}

	ldi_bioinit(lbp);

	return (lbp);
}

/* Client interface to release an LDI buffer */
void
ldi_freerbuf(ldi_buf_t *lbp)
{
	if (!lbp) {
		return;
	}

	/* Deallocate */
	kmem_free(lbp, sizeof (ldi_buf_t));
}

void
ldi_bioinit(ldi_buf_t *lbp)
{
#ifdef LDI_ZERO
	/* Zero the new buffer struct */
	bzero(lbp, sizeof (ldi_buf_t));
#endif

	/* Initialize defaults */
	lbp->b_un.b_addr = 0;
	lbp->b_flags = 0;
	lbp->b_bcount = 0;
	lbp->b_bufsize = 0;
	lbp->b_lblkno = 0;
	lbp->b_resid = 0;
	lbp->b_error = 0;
}

/*
 * IOKit C++ functions
 */
int
ldi_init(void *provider)
{
	int index;

	/* Allocate kstat pointer */
	ldi_ksp = kstat_create("zfs", 0, "ldi", "darwin", KSTAT_TYPE_NAMED,
	    sizeof (ldi_stats) / sizeof (kstat_named_t), KSTAT_FLAG_VIRTUAL);

	if (ldi_ksp == NULL) {
		dprintf("%s couldn't register kstats\n", __func__);
		return (ENOMEM);
	}

	/* Register kstats */
	ldi_ksp->ks_data = &ldi_stats;
	kstat_install(ldi_ksp);

	/* Register sysctls */
	sysctl_register_oid(&sysctl__ldi);
	sysctl_register_oid(&sysctl__ldi_debug);
	sysctl_register_oid(&sysctl__ldi_debug_use_iokit_from_path);
	sysctl_register_oid(&sysctl__ldi_debug_use_iokit_from_dev);
	sysctl_register_oid(&sysctl__ldi_debug_use_dev_from_path);
	sysctl_register_oid(&sysctl__ldi_debug_use_vnode_from_path);

	/* Create handle hash lists and locks */
	ldi_handle_hash_count = 0;
	for (index = 0; index < LH_HASH_SZ; index++) {
		mutex_init(&ldi_handle_hash_lock[index], NULL,
		    MUTEX_DEFAULT, NULL);
		list_create(&ldi_handle_hash_list[index],
		    sizeof (struct ldi_handle),
		    offsetof(struct ldi_handle, lh_node));
	}

	/*
	 * Initialize the LDI event subsystem
	 */
	mutex_init(&ldi_ev_callback_list.le_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&ldi_ev_callback_list.le_cv, NULL, CV_DEFAULT, NULL);
	ldi_ev_callback_list.le_busy = 0;
	ldi_ev_callback_list.le_thread = NULL;
	ldi_ev_callback_list.le_walker_next = NULL;
	ldi_ev_callback_list.le_walker_prev = NULL;
	list_create(&ldi_ev_callback_list.le_head,
	    sizeof (ldi_ev_callback_impl_t),
	    offsetof(ldi_ev_callback_impl_t, lec_list));

	return (0);
}

void
ldi_fini()
{
	/*
	 * Teardown the LDI event subsystem
	 */
	ldi_ev_lock();
#ifdef DEBUG
	if (ldi_ev_callback_list.le_busy != 1 ||
	    ldi_ev_callback_list.le_thread != curthread ||
	    ldi_ev_callback_list.le_walker_next != NULL ||
	    ldi_ev_callback_list.le_walker_prev != NULL) {
		dprintf("%s still has %s %llu %s %p %s %p %s %p\n", __func__,
		    "le_busy", ldi_ev_callback_list.le_busy,
		    "le_thread", ldi_ev_callback_list.le_thread,
		    "le_walker_next", ldi_ev_callback_list.le_walker_next,
		    "le_walker_prev", ldi_ev_callback_list.le_walker_prev);
	}
#endif
	list_destroy(&ldi_ev_callback_list.le_head);
	ldi_ev_unlock();
#ifdef DEBUG
	ldi_ev_callback_list.le_busy = 0;
	ldi_ev_callback_list.le_thread = NULL;
	ldi_ev_callback_list.le_walker_next = NULL;
	ldi_ev_callback_list.le_walker_prev = NULL;
#endif

	cv_destroy(&ldi_ev_callback_list.le_cv);
	mutex_destroy(&ldi_ev_callback_list.le_lock);

	if (ldi_handle_hash_count != 0) {
		dprintf("%s ldi_handle_hash_count %llu\n", __func__,
		    ldi_handle_hash_count);
	}

	/* Destroy handle hash lists and locks */
	handle_hash_release();

	/* Unregister sysctls */
	sysctl_unregister_oid(&sysctl__ldi_debug_use_iokit_from_path);
	sysctl_unregister_oid(&sysctl__ldi_debug_use_iokit_from_dev);
	sysctl_unregister_oid(&sysctl__ldi_debug_use_dev_from_path);
	sysctl_unregister_oid(&sysctl__ldi_debug_use_vnode_from_path);
	sysctl_unregister_oid(&sysctl__ldi_debug);
	sysctl_unregister_oid(&sysctl__ldi);

	/* Unregister kstats */
	if (ldi_ksp != NULL) {
		kstat_delete(ldi_ksp);
		ldi_ksp = NULL;
	}

	if (ldi_handle_hash_count != 0) {
		dprintf("%s handle_hash_count still %llu\n", __func__,
		    ldi_handle_hash_count);
	}
}
