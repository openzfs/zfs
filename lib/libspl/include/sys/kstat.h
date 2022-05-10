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
 * Copyright 2006 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef	_SYS_KSTAT_H
#define	_SYS_KSTAT_H



/*
 * Definition of general kernel statistics structures and /dev/kstat ioctls
 */

#include <sys/types.h>
#include <sys/time.h>

#ifdef	__cplusplus
extern "C" {
#endif

typedef int	kid_t;		/* unique kstat id */

/*
 * Kernel statistics driver (/dev/kstat) ioctls
 */

#define	KSTAT_IOC_BASE		('K' << 8)

#define	KSTAT_IOC_CHAIN_ID	KSTAT_IOC_BASE | 0x01
#define	KSTAT_IOC_READ		KSTAT_IOC_BASE | 0x02
#define	KSTAT_IOC_WRITE		KSTAT_IOC_BASE | 0x03

/*
 * /dev/kstat ioctl usage (kd denotes /dev/kstat descriptor):
 *
 *	kcid = ioctl(kd, KSTAT_IOC_CHAIN_ID, NULL);
 *	kcid = ioctl(kd, KSTAT_IOC_READ, kstat_t *);
 *	kcid = ioctl(kd, KSTAT_IOC_WRITE, kstat_t *);
 */

#define	KSTAT_STRLEN	255	/* 254 chars + NULL; must be 16 * n - 1 */

/*
 * The generic kstat header
 */

typedef struct kstat {
	/*
	 * Fields relevant to both kernel and user
	 */
	hrtime_t	ks_crtime;	/* creation time (from gethrtime()) */
	struct kstat	*ks_next;	/* kstat chain linkage */
	kid_t		ks_kid;		/* unique kstat ID */
	char		ks_module[KSTAT_STRLEN]; /* provider module name */
	uchar_t		ks_resv;	/* reserved, currently just padding */
	int		ks_instance;	/* provider module's instance */
	char		ks_name[KSTAT_STRLEN]; /* kstat name */
	uchar_t		ks_type;	/* kstat data type */
	char		ks_class[KSTAT_STRLEN]; /* kstat class */
	uchar_t		ks_flags;	/* kstat flags */
	void		*ks_data;	/* kstat type-specific data */
	uint_t		ks_ndata;	/* # of type-specific data records */
	size_t		ks_data_size;	/* total size of kstat data section */
	hrtime_t	ks_snaptime;	/* time of last data snapshot */
	/*
	 * Fields relevant to kernel only
	 */
	int		(*ks_update)(struct kstat *, int); /* dynamic update */
	void		*ks_private;	/* arbitrary provider-private data */
	int		(*ks_snapshot)(struct kstat *, void *, int);
	void		*ks_lock;	/* protects this kstat's data */
} kstat_t;

/*
 * kstat structure and locking strategy
 *
 * Each kstat consists of a header section (a kstat_t) and a data section.
 * The system maintains a set of kstats, protected by kstat_chain_lock.
 * kstat_chain_lock protects all additions to/deletions from this set,
 * as well as all changes to kstat headers.  kstat data sections are
 * *optionally* protected by the per-kstat ks_lock.  If ks_lock is non-NULL,
 * kstat clients (e.g. /dev/kstat) will acquire this lock for all of their
 * operations on that kstat.  It is up to the kstat provider to decide whether
 * guaranteeing consistent data to kstat clients is sufficiently important
 * to justify the locking cost.  Note, however, that most statistic updates
 * already occur under one of the provider's mutexes, so if the provider sets
 * ks_lock to point to that mutex, then kstat data locking is free.
 *
 * NOTE: variable-size kstats MUST employ kstat data locking, to prevent
 * data-size races with kstat clients.
 *
 * NOTE: ks_lock is really of type (kmutex_t *); it is declared as (void *)
 * in the kstat header so that users don't have to be exposed to all of the
 * kernel's lock-related data structures.
 */

#if	defined(_KERNEL)

#define	KSTAT_ENTER(k)	\
	{ kmutex_t *lp = (k)->ks_lock; if (lp) mutex_enter(lp); }

#define	KSTAT_EXIT(k)	\
	{ kmutex_t *lp = (k)->ks_lock; if (lp) mutex_exit(lp); }

#define	KSTAT_UPDATE(k, rw)		(*(k)->ks_update)((k), (rw))

#define	KSTAT_SNAPSHOT(k, buf, rw)	(*(k)->ks_snapshot)((k), (buf), (rw))

#endif	/* defined(_KERNEL) */

/*
 * kstat time
 *
 * All times associated with kstats (e.g. creation time, snapshot time,
 * kstat_timer_t and kstat_io_t timestamps, etc.) are 64-bit nanosecond values,
 * as returned by gethrtime().  The accuracy of these timestamps is machine
 * dependent, but the precision (units) is the same across all platforms.
 */

/*
 * kstat identity (KID)
 *
 * Each kstat is assigned a unique KID (kstat ID) when it is added to the
 * global kstat chain.  The KID is used as a cookie by /dev/kstat to
 * request information about the corresponding kstat.  There is also
 * an identity associated with the entire kstat chain, kstat_chain_id,
 * which is bumped each time a kstat is added or deleted.  /dev/kstat uses
 * the chain ID to detect changes in the kstat chain (e.g., a new disk
 * coming online) between ioctl()s.
 */

/*
 * kstat module, kstat instance
 *
 * ks_module and ks_instance contain the name and instance of the module
 * that created the kstat.  In cases where there can only be one instance,
 * ks_instance is 0.  The kernel proper (/kernel/unix) uses "unix" as its
 * module name.
 */

/*
 * kstat name
 *
 * ks_name gives a meaningful name to a kstat.  The full kstat namespace
 * is module.instance.name, so the name only need be unique within a
 * module.  kstat_create() will fail if you try to create a kstat with
 * an already-used (ks_module, ks_instance, ks_name) triplet.  Spaces are
 * allowed in kstat names, but strongly discouraged, since they hinder
 * awk-style processing at user level.
 */

/*
 * kstat type
 *
 * The kstat mechanism provides several flavors of kstat data, defined
 * below.  The "raw" kstat type is just treated as an array of bytes; you
 * can use this to export any kind of data you want.
 *
 * Some kstat types allow multiple data structures per kstat, e.g.
 * KSTAT_TYPE_NAMED; others do not.  This is part of the spec for each
 * kstat data type.
 *
 * User-level tools should *not* rely on the #define KSTAT_NUM_TYPES.  To
 * get this information, read out the standard system kstat "kstat_types".
 */

#define	KSTAT_TYPE_RAW		0	/* can be anything */
					/* ks_ndata >= 1 */
#define	KSTAT_TYPE_NAMED	1	/* name/value pair */
					/* ks_ndata >= 1 */
#define	KSTAT_TYPE_INTR		2	/* interrupt statistics */
					/* ks_ndata == 1 */
#define	KSTAT_TYPE_IO		3	/* I/O statistics */
					/* ks_ndata == 1 */
#define	KSTAT_TYPE_TIMER	4	/* event timer */
					/* ks_ndata >= 1 */

#define	KSTAT_NUM_TYPES		5

/*
 * kstat class
 *
 * Each kstat can be characterized as belonging to some broad class
 * of statistics, e.g. disk, tape, net, vm, streams, etc.  This field
 * can be used as a filter to extract related kstats.  The following
 * values are currently in use: disk, tape, net, controller, vm, kvm,
 * hat, streams, kstat, and misc.  (The kstat class encompasses things
 * like kstat_types.)
 */

/*
 * kstat flags
 *
 * Any of the following flags may be passed to kstat_create().  They are
 * all zero by default.
 *
 *	KSTAT_FLAG_VIRTUAL:
 *
 *		Tells kstat_create() not to allocate memory for the
 *		kstat data section; instead, you will set the ks_data
 *		field to point to the data you wish to export.  This
 *		provides a convenient way to export existing data
 *		structures.
 *
 *	KSTAT_FLAG_VAR_SIZE:
 *
 *		The size of the kstat you are creating will vary over time.
 *		For example, you may want to use the kstat mechanism to
 *		export a linked list.  NOTE: The kstat framework does not
 *		manage the data section, so all variable-size kstats must be
 *		virtual kstats.  Moreover, variable-size kstats MUST employ
 *		kstat data locking to prevent data-size races with kstat
 *		clients.  See the section on "kstat snapshot" for details.
 *
 *	KSTAT_FLAG_WRITABLE:
 *
 *		Makes the kstat's data section writable by root.
 *		The ks_snapshot routine (see below) does not need to check for
 *		this; permission checking is handled in the kstat driver.
 *
 *	KSTAT_FLAG_PERSISTENT:
 *
 *		Indicates that this kstat is to be persistent over time.
 *		For persistent kstats, kstat_delete() simply marks the
 *		kstat as dormant; a subsequent kstat_create() reactivates
 *		the kstat.  This feature is provided so that statistics
 *		are not lost across driver close/open (e.g., raw disk I/O
 *		on a disk with no mounted partitions.)
 *		NOTE: Persistent kstats cannot be virtual, since ks_data
 *		points to garbage as soon as the driver goes away.
 *
 * The following flags are maintained by the kstat framework:
 *
 *	KSTAT_FLAG_DORMANT:
 *
 *		For persistent kstats, indicates that the kstat is in the
 *		dormant state (e.g., the corresponding device is closed).
 *
 *	KSTAT_FLAG_INVALID:
 *
 *		This flag is set when a kstat is in a transitional state,
 *		e.g. between kstat_create() and kstat_install().
 *		kstat clients must not attempt to access the kstat's data
 *		if this flag is set.
 */

#define	KSTAT_FLAG_VIRTUAL		0x01
#define	KSTAT_FLAG_VAR_SIZE		0x02
#define	KSTAT_FLAG_WRITABLE		0x04
#define	KSTAT_FLAG_PERSISTENT		0x08
#define	KSTAT_FLAG_DORMANT		0x10
#define	KSTAT_FLAG_INVALID		0x20
#define	KSTAT_FLAG_LONGSTRINGS		0x40
#define	KSTAT_FLAG_NO_HEADERS		0x80

/*
 * Dynamic update support
 *
 * The kstat mechanism allows for an optional ks_update function to update
 * kstat data.  This is useful for drivers where the underlying device
 * keeps cheap hardware stats, but extraction is expensive.  Instead of
 * constantly keeping the kstat data section up to date, you can supply a
 * ks_update function which updates the kstat's data section on demand.
 * To take advantage of this feature, simply set the ks_update field before
 * calling kstat_install().
 *
 * The ks_update function, if supplied, must have the following structure:
 *
 *	int
 *	foo_kstat_update(kstat_t *ksp, int rw)
 *	{
 *		if (rw == KSTAT_WRITE) {
 *			... update the native stats from ksp->ks_data;
 *				return EACCES if you don't support this
 *		} else {
 *			... update ksp->ks_data from the native stats
 *		}
 *	}
 *
 * The ks_update return codes are: 0 for success, EACCES if you don't allow
 * KSTAT_WRITE, and EIO for any other type of error.
 *
 * In general, the ks_update function may need to refer to provider-private
 * data; for example, it may need a pointer to the provider's raw statistics.
 * The ks_private field is available for this purpose.  Its use is entirely
 * at the provider's discretion.
 *
 * All variable-size kstats MUST supply a ks_update routine, which computes
 * and sets ks_data_size (and ks_ndata if that is meaningful), since these
 * are needed to perform kstat snapshots (see below).
 *
 * No kstat locking should be done inside the ks_update routine.  The caller
 * will already be holding the kstat's ks_lock (to ensure consistent data).
 */

#define	KSTAT_READ	0
#define	KSTAT_WRITE	1

/*
 * Kstat snapshot
 *
 * In order to get a consistent view of a kstat's data, clients must obey
 * the kstat's locking strategy.  However, these clients may need to perform
 * operations on the data which could cause a fault (e.g. copyout()), or
 * operations which are simply expensive.  Doing so could cause deadlock
 * (e.g. if you're holding a disk's kstat lock which is ultimately required
 * to resolve a copyout() fault), performance degradation (since the providers'
 * activity is serialized at the kstat lock), device timing problems, etc.
 *
 * To avoid these problems, kstat data is provided via snapshots.  Taking
 * a snapshot is a simple process: allocate a wired-down kernel buffer,
 * acquire the kstat's data lock, copy the data into the buffer ("take the
 * snapshot"), and release the lock.  This ensures that the kstat's data lock
 * will be held as briefly as possible, and that no faults will occur while
 * the lock is held.
 *
 * Normally, the snapshot is taken by default_kstat_snapshot(), which
 * timestamps the data (sets ks_snaptime), copies it, and does a little
 * massaging to deal with incomplete transactions on i/o kstats.  However,
 * this routine only works for kstats with contiguous data (the typical case).
 * If you create a kstat whose data is, say, a linked list, you must provide
 * your own ks_snapshot routine.  The routine you supply must have the
 * following prototype (replace "foo" with something appropriate):
 *
 *	int foo_kstat_snapshot(kstat_t *ksp, void *buf, int rw);
 *
 * The minimal snapshot routine -- one which copies contiguous data that
 * doesn't need any massaging -- would be this:
 *
 *	ksp->ks_snaptime = gethrtime();
 *	if (rw == KSTAT_WRITE)
 *		memcpy(ksp->ks_data, buf, ksp->ks_data_size);
 *	else
 *		memcpy(buf, ksp->ks_data, ksp->ks_data_size);
 *	return (0);
 *
 * A more illuminating example is taking a snapshot of a linked list:
 *
 *	ksp->ks_snaptime = gethrtime();
 *	if (rw == KSTAT_WRITE)
 *		return (EACCES);		... See below ...
 *	for (foo = first_foo; foo; foo = foo->next) {
 *		memcpy(buf, foo, sizeof (struct foo));
 *		buf = ((struct foo *) buf) + 1;
 *	}
 *	return (0);
 *
 * In the example above, we have decided that we don't want to allow
 * KSTAT_WRITE access, so we return EACCES if this is attempted.
 *
 * The key points are:
 *
 *	(1) ks_snaptime must be set (via gethrtime()) to timestamp the data.
 *	(2) Data gets copied from the kstat to the buffer on KSTAT_READ,
 *		and from the buffer to the kstat on KSTAT_WRITE.
 *	(3) ks_snapshot return values are: 0 for success, EACCES if you
 *		don't allow KSTAT_WRITE, and EIO for any other type of error.
 *
 * Named kstats (see section on "Named statistics" below) containing long
 * strings (KSTAT_DATA_STRING) need special handling.  The kstat driver
 * assumes that all strings are copied into the buffer after the array of
 * named kstats, and the pointers (KSTAT_NAMED_STR_PTR()) are updated to point
 * into the copy within the buffer. The default snapshot routine does this,
 * but overriding routines should contain at least the following:
 *
 * if (rw == KSTAT_READ) {
 * 	kstat_named_t *knp = buf;
 * 	char *end = knp + ksp->ks_ndata;
 * 	uint_t i;
 *
 * 	... Do the regular copy ...
 * 	memcpy(buf, ksp->ks_data, sizeof (kstat_named_t) * ksp->ks_ndata);
 *
 * 	for (i = 0; i < ksp->ks_ndata; i++, knp++) {
 *		if (knp[i].data_type == KSTAT_DATA_STRING &&
 *		    KSTAT_NAMED_STR_PTR(knp) != NULL) {
 *			memcpy(end, KSTAT_NAMED_STR_PTR(knp),
 *			    KSTAT_NAMED_STR_BUFLEN(knp));
 *			KSTAT_NAMED_STR_PTR(knp) = end;
 *			end += KSTAT_NAMED_STR_BUFLEN(knp);
 *		}
 *	}
 */

/*
 * Named statistics.
 *
 * List of arbitrary name=value statistics.
 */

typedef struct kstat_named {
	char	name[KSTAT_STRLEN];	/* name of counter */
	uchar_t	data_type;		/* data type */
	union {
		char		c[16];	/* enough for 128-bit ints */
		int32_t		i32;
		uint32_t	ui32;
		struct {
			union {
				char 		*ptr;	/* NULL-term string */
#if defined(_KERNEL) && defined(_MULTI_DATAMODEL)
				caddr32_t	ptr32;
#endif
				char 		__pad[8]; /* 64-bit padding */
			} addr;
			uint32_t	len;	/* # bytes for strlen + '\0' */
		} str;
/*
 * The int64_t and uint64_t types are not valid for a maximally conformant
 * 32-bit compilation environment (cc -Xc) using compilers prior to the
 * introduction of C99 conforming compiler (reference ISO/IEC 9899:1990).
 * In these cases, the visibility of i64 and ui64 is only permitted for
 * 64-bit compilation environments or 32-bit non-maximally conformant
 * C89 or C90 ANSI C compilation environments (cc -Xt and cc -Xa). In the
 * C99 ANSI C compilation environment, the long long type is supported.
 * The _INT64_TYPE is defined by the implementation (see sys/inttypes.h).
 */
#if defined(_INT64_TYPE)
		int64_t		i64;
		uint64_t	ui64;
#endif
		long		l;
		ulong_t		ul;

		/* These structure members are obsolete */

		longlong_t	ll;
		u_longlong_t	ull;
		float		f;
		double		d;
	} value;			/* value of counter */
} kstat_named_t;

#define	KSTAT_DATA_CHAR		0
#define	KSTAT_DATA_INT32	1
#define	KSTAT_DATA_UINT32	2
#define	KSTAT_DATA_INT64	3
#define	KSTAT_DATA_UINT64	4

#if !defined(_LP64)
#define	KSTAT_DATA_LONG		KSTAT_DATA_INT32
#define	KSTAT_DATA_ULONG	KSTAT_DATA_UINT32
#else
#if !defined(_KERNEL)
#define	KSTAT_DATA_LONG		KSTAT_DATA_INT64
#define	KSTAT_DATA_ULONG	KSTAT_DATA_UINT64
#else
#define	KSTAT_DATA_LONG		7	/* only visible to the kernel */
#define	KSTAT_DATA_ULONG	8	/* only visible to the kernel */
#endif	/* !_KERNEL */
#endif	/* !_LP64 */

/*
 * Statistics exporting named kstats with long strings (KSTAT_DATA_STRING)
 * may not make the assumption that ks_data_size is equal to (ks_ndata * sizeof
 * (kstat_named_t)).  ks_data_size in these cases is equal to the sum of the
 * amount of space required to store the strings (ie, the sum of
 * KSTAT_NAMED_STR_BUFLEN() for all KSTAT_DATA_STRING statistics) plus the
 * space required to store the kstat_named_t's.
 *
 * The default update routine will update ks_data_size automatically for
 * variable-length kstats containing long strings (using the default update
 * routine only makes sense if the string is the only thing that is changing
 * in size, and ks_ndata is constant).  Fixed-length kstats containing long
 * strings must explicitly change ks_data_size (after creation but before
 * initialization) to reflect the correct amount of space required for the
 * long strings and the kstat_named_t's.
 */
#define	KSTAT_DATA_STRING	9

/* These types are obsolete */

#define	KSTAT_DATA_LONGLONG	KSTAT_DATA_INT64
#define	KSTAT_DATA_ULONGLONG	KSTAT_DATA_UINT64
#define	KSTAT_DATA_FLOAT	5
#define	KSTAT_DATA_DOUBLE	6

#define	KSTAT_NAMED_PTR(kptr)	((kstat_named_t *)(kptr)->ks_data)

/*
 * Retrieve the pointer of the string contained in the given named kstat.
 */
#define	KSTAT_NAMED_STR_PTR(knptr) ((knptr)->value.str.addr.ptr)

/*
 * Retrieve the length of the buffer required to store the string in the given
 * named kstat.
 */
#define	KSTAT_NAMED_STR_BUFLEN(knptr) ((knptr)->value.str.len)

/*
 * Interrupt statistics.
 *
 * An interrupt is a hard interrupt (sourced from the hardware device
 * itself), a soft interrupt (induced by the system via the use of
 * some system interrupt source), a watchdog interrupt (induced by
 * a periodic timer call), spurious (an interrupt entry point was
 * entered but there was no interrupt condition to service),
 * or multiple service (an interrupt condition was detected and
 * serviced just prior to returning from any of the other types).
 *
 * Measurement of the spurious class of interrupts is useful for
 * autovectored devices in order to pinpoint any interrupt latency
 * problems in a particular system configuration.
 *
 * Devices that have more than one interrupt of the same
 * type should use multiple structures.
 */

#define	KSTAT_INTR_HARD			0
#define	KSTAT_INTR_SOFT			1
#define	KSTAT_INTR_WATCHDOG		2
#define	KSTAT_INTR_SPURIOUS		3
#define	KSTAT_INTR_MULTSVC		4

#define	KSTAT_NUM_INTRS			5

typedef struct kstat_intr {
	uint_t	intrs[KSTAT_NUM_INTRS];	/* interrupt counters */
} kstat_intr_t;

#define	KSTAT_INTR_PTR(kptr)	((kstat_intr_t *)(kptr)->ks_data)

/*
 * I/O statistics.
 */

typedef struct kstat_io {

	/*
	 * Basic counters.
	 *
	 * The counters should be updated at the end of service
	 * (e.g., just prior to calling biodone()).
	 */

	u_longlong_t	nread;		/* number of bytes read */
	u_longlong_t	nwritten;	/* number of bytes written */
	uint_t		reads;		/* number of read operations */
	uint_t		writes;		/* number of write operations */

	/*
	 * Accumulated time and queue length statistics.
	 *
	 * Accumulated time statistics are kept as a running sum
	 * of "active" time.  Queue length statistics are kept as a
	 * running sum of the product of queue length and elapsed time
	 * at that length -- i.e., a Riemann sum for queue length
	 * integrated against time.  (You can also think of the active time
	 * as a Riemann sum, for the boolean function (queue_length > 0)
	 * integrated against time, or you can think of it as the
	 * Lebesgue measure of the set on which queue_length > 0.)
	 *
	 *		^
	 *		|			_________
	 *		8			| i4	|
	 *		|			|	|
	 *	Queue	6			|	|
	 *	Length	|	_________	|	|
	 *		4	| i2	|_______|	|
	 *		|	|	    i3		|
	 *		2_______|			|
	 *		|    i1				|
	 *		|_______________________________|
	 *		Time->	t1	t2	t3	t4
	 *
	 * At each change of state (entry or exit from the queue),
	 * we add the elapsed time (since the previous state change)
	 * to the active time if the queue length was non-zero during
	 * that interval; and we add the product of the elapsed time
	 * times the queue length to the running length*time sum.
	 *
	 * This method is generalizable to measuring residency
	 * in any defined system: instead of queue lengths, think
	 * of "outstanding RPC calls to server X".
	 *
	 * A large number of I/O subsystems have at least two basic
	 * "lists" of transactions they manage: one for transactions
	 * that have been accepted for processing but for which processing
	 * has yet to begin, and one for transactions which are actively
	 * being processed (but not done). For this reason, two cumulative
	 * time statistics are defined here: wait (pre-service) time,
	 * and run (service) time.
	 *
	 * All times are 64-bit nanoseconds (hrtime_t), as returned by
	 * gethrtime().
	 *
	 * The units of cumulative busy time are accumulated nanoseconds.
	 * The units of cumulative length*time products are elapsed time
	 * times queue length.
	 *
	 * Updates to the fields below are performed implicitly by calls to
	 * these five functions:
	 *
	 *	kstat_waitq_enter()
	 *	kstat_waitq_exit()
	 *	kstat_runq_enter()
	 *	kstat_runq_exit()
	 *
	 *	kstat_waitq_to_runq()		(see below)
	 *	kstat_runq_back_to_waitq()	(see below)
	 *
	 * Since kstat_waitq_exit() is typically followed immediately
	 * by kstat_runq_enter(), there is a single kstat_waitq_to_runq()
	 * function which performs both operations.  This is a performance
	 * win since only one timestamp is required.
	 *
	 * In some instances, it may be necessary to move a request from
	 * the run queue back to the wait queue, e.g. for write throttling.
	 * For these situations, call kstat_runq_back_to_waitq().
	 *
	 * These fields should never be updated by any other means.
	 */

	hrtime_t wtime;		/* cumulative wait (pre-service) time */
	hrtime_t wlentime;	/* cumulative wait length*time product */
	hrtime_t wlastupdate;	/* last time wait queue changed */
	hrtime_t rtime;		/* cumulative run (service) time */
	hrtime_t rlentime;	/* cumulative run length*time product */
	hrtime_t rlastupdate;	/* last time run queue changed */

	uint_t	wcnt;		/* count of elements in wait state */
	uint_t	rcnt;		/* count of elements in run state */

} kstat_io_t;

#define	KSTAT_IO_PTR(kptr)	((kstat_io_t *)(kptr)->ks_data)

/*
 * Event timer statistics - cumulative elapsed time and number of events.
 *
 * Updates to these fields are performed implicitly by calls to
 * kstat_timer_start() and kstat_timer_stop().
 */

typedef struct kstat_timer {
	char		name[KSTAT_STRLEN];	/* event name */
	uchar_t		resv;			/* reserved */
	u_longlong_t	num_events;		/* number of events */
	hrtime_t	elapsed_time;		/* cumulative elapsed time */
	hrtime_t	min_time;		/* shortest event duration */
	hrtime_t	max_time;		/* longest event duration */
	hrtime_t	start_time;		/* previous event start time */
	hrtime_t	stop_time;		/* previous event stop time */
} kstat_timer_t;

#define	KSTAT_TIMER_PTR(kptr)	((kstat_timer_t *)(kptr)->ks_data)

#if	defined(_KERNEL)

#include <sys/t_lock.h>

extern kid_t	kstat_chain_id;		/* bumped at each state change */
extern void	kstat_init(void);	/* initialize kstat framework */

/*
 * Adding and deleting kstats.
 *
 * The typical sequence to add a kstat is:
 *
 *	ksp = kstat_create(module, instance, name, class, type, ndata, flags);
 *	if (ksp) {
 *		... provider initialization, if necessary
 *		kstat_install(ksp);
 *	}
 *
 * There are three logically distinct steps here:
 *
 * Step 1: System Initialization (kstat_create)
 *
 * kstat_create() performs system initialization.  kstat_create()
 * allocates memory for the entire kstat (header plus data), initializes
 * all header fields, initializes the data section to all zeroes, assigns
 * a unique KID, and puts the kstat onto the system's kstat chain.
 * The returned kstat is marked invalid (KSTAT_FLAG_INVALID is set),
 * because the provider (caller) has not yet had a chance to initialize
 * the data section.
 *
 * By default, kstats are exported to all zones on the system.  A kstat may be
 * created via kstat_create_zone() to specify a zone to which the statistics
 * should be exported.  kstat_zone_add() may be used to specify additional
 * zones to which the statistics are to be exported.
 *
 * Step 2: Provider Initialization
 *
 * The provider performs any necessary initialization of the data section,
 * e.g. setting the name fields in a KSTAT_TYPE_NAMED.  Virtual kstats set
 * the ks_data field at this time.  The provider may also set the ks_update,
 * ks_snapshot, ks_private, and ks_lock fields if necessary.
 *
 * Step 3: Installation (kstat_install)
 *
 * Once the kstat is completely initialized, kstat_install() clears the
 * INVALID flag, thus making the kstat accessible to the outside world.
 * kstat_install() also clears the DORMANT flag for persistent kstats.
 *
 * Removing a kstat from the system
 *
 * kstat_delete(ksp) removes ksp from the kstat chain and frees all
 * associated system resources.  NOTE: When you call kstat_delete(),
 * you must NOT be holding that kstat's ks_lock.  Otherwise, you may
 * deadlock with a kstat reader.
 *
 * Persistent kstats
 *
 * From the provider's point of view, persistence is transparent.  The only
 * difference between ephemeral (normal) kstats and persistent kstats
 * is that you pass KSTAT_FLAG_PERSISTENT to kstat_create().  Magically,
 * this has the effect of making your data visible even when you're
 * not home.  Persistence is important to tools like iostat, which want
 * to get a meaningful picture of disk activity.  Without persistence,
 * raw disk i/o statistics could never accumulate: they would come and
 * go with each open/close of the raw device.
 *
 * The magic of persistence works by slightly altering the behavior of
 * kstat_create() and kstat_delete().  The first call to kstat_create()
 * creates a new kstat, as usual.  However, kstat_delete() does not
 * actually delete the kstat: it performs one final update of the data
 * (i.e., calls the ks_update routine), marks the kstat as dormant, and
 * sets the ks_lock, ks_update, ks_private, and ks_snapshot fields back
 * to their default values (since they might otherwise point to garbage,
 * e.g. if the provider is going away).  kstat clients can still access
 * the dormant kstat just like a live kstat; they just continue to see
 * the final data values as long as the kstat remains dormant.
 * All subsequent kstat_create() calls simply find the already-existing,
 * dormant kstat and return a pointer to it, without altering any fields.
 * The provider then performs its usual initialization sequence, and
 * calls kstat_install().  kstat_install() uses the old data values to
 * initialize the native data (i.e., ks_update is called with KSTAT_WRITE),
 * thus making it seem like you were never gone.
 */

extern kstat_t *kstat_create(const char *, int, const char *, const char *,
    uchar_t, uint_t, uchar_t);
extern kstat_t *kstat_create_zone(const char *, int, const char *,
    const char *, uchar_t, uint_t, uchar_t, zoneid_t);
extern void kstat_install(kstat_t *);
extern void kstat_delete(kstat_t *);
extern void kstat_named_setstr(kstat_named_t *knp, const char *src);
extern void kstat_set_string(char *, const char *);
extern void kstat_delete_byname(const char *, int, const char *);
extern void kstat_delete_byname_zone(const char *, int, const char *, zoneid_t);
extern void kstat_named_init(kstat_named_t *, const char *, uchar_t);
extern void kstat_timer_init(kstat_timer_t *, const char *);
extern void kstat_timer_start(kstat_timer_t *);
extern void kstat_timer_stop(kstat_timer_t *);

extern void kstat_zone_add(kstat_t *, zoneid_t);
extern void kstat_zone_remove(kstat_t *, zoneid_t);
extern int kstat_zone_find(kstat_t *, zoneid_t);

extern kstat_t *kstat_hold_bykid(kid_t kid, zoneid_t);
extern kstat_t *kstat_hold_byname(const char *, int, const char *, zoneid_t);
extern void kstat_rele(kstat_t *);

#endif	/* defined(_KERNEL) */

#ifdef	__cplusplus
}
#endif

#endif	/* _SYS_KSTAT_H */
