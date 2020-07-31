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
 * Copyright 2006 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SPL_KSTAT_H
#define _SPL_KSTAT_H

#include <sys/types.h>
#include <sys/time.h>
#include <sys/kmem.h>
//#include <sys/sysctl.h>
#include <sys/mutex.h>

/*
* Kernel statistics driver (/dev/zfs) ioctls
* Defined outside the ZFS ioctls, and handled separately in zfs_vnops_windows.c
*/

#define	KSTAT_IOC_CHAIN_ID	CTL_CODE(ZFSIOCTL_TYPE, 0x7FD, METHOD_NEITHER, FILE_ANY_ACCESS)
#define KSTAT_IOC_READ		CTL_CODE(ZFSIOCTL_TYPE, 0x7FE, METHOD_NEITHER, FILE_ANY_ACCESS)
#define	KSTAT_IOC_WRITE		CTL_CODE(ZFSIOCTL_TYPE, 0x7FF, METHOD_NEITHER, FILE_ANY_ACCESS)


#define KSTAT_STRLEN            31

#if     defined(_KERNEL)

#define KSTAT_ENTER(k)  \
        { kmutex_t *lp = (k)->ks_lock; if (lp) mutex_enter(lp); }

#define KSTAT_EXIT(k)   \
        { kmutex_t *lp = (k)->ks_lock; if (lp) mutex_exit(lp); }

#define KSTAT_UPDATE(k, rw)             (*(k)->ks_update)((k), (rw))

#define KSTAT_SNAPSHOT(k, buf, rw)      (*(k)->ks_snapshot)((k), (buf), (rw))

#endif  /* defined(_KERNEL) */

/* For reference valid classes are:
 * disk, tape, net, controller, vm, kvm, hat, streams, kstat, misc
 */

#define KSTAT_TYPE_RAW          0       /* can be anything; ks_ndata >= 1 */
#define KSTAT_TYPE_NAMED        1       /* name/value pair; ks_ndata >= 1 */
#define KSTAT_TYPE_INTR         2       /* interrupt stats; ks_ndata == 1 */
#define KSTAT_TYPE_IO           3       /* I/O stats; ks_ndata == 1 */
#define KSTAT_TYPE_TIMER        4       /* event timer; ks_ndata >= 1 */
#define KSTAT_TYPE_TXG          5       /* txg sync; ks_ndata >= 1 */
#define KSTAT_NUM_TYPES         6

#define KSTAT_DATA_CHAR         0
#define KSTAT_DATA_INT32        1
#define KSTAT_DATA_UINT32       2
#define KSTAT_DATA_INT64        3
#define KSTAT_DATA_UINT64       4
#define KSTAT_DATA_LONG         5
#define KSTAT_DATA_ULONG        6
#define KSTAT_DATA_STRING       7
#define KSTAT_NUM_DATAS         8

#define KSTAT_INTR_HARD         0
#define KSTAT_INTR_SOFT         1
#define KSTAT_INTR_WATCHDOG     2
#define KSTAT_INTR_SPURIOUS     3
#define KSTAT_INTR_MULTSVC      4
#define KSTAT_NUM_INTRS         5

#define KSTAT_FLAG_VIRTUAL      0x01
#define KSTAT_FLAG_VAR_SIZE     0x02
#define KSTAT_FLAG_WRITABLE     0x04
#define KSTAT_FLAG_PERSISTENT   0x08
#define KSTAT_FLAG_DORMANT      0x10
#define KSTAT_FLAG_UNSUPPORTED  (KSTAT_FLAG_VAR_SIZE | KSTAT_FLAG_WRITABLE | \
KSTAT_FLAG_PERSISTENT | KSTAT_FLAG_DORMANT)
#define KSTAT_FLAG_INVALID      0x20
#define KSTAT_FLAG_LONGSTRINGS	0x40

#define KS_MAGIC                0x9d9d9d9d

#define KSTAT_NAMED_PTR(kptr)   ((kstat_named_t *)(kptr)->ks_data)


/* Dynamic updates */
#define KSTAT_READ              0
#define KSTAT_WRITE             1

struct kstat;

typedef int kid_t;                                  /* unique kstat id */
typedef int kstat_update_t(struct kstat *, int);  /* dynamic update cb */

#pragma pack(4)
typedef struct kstat {
	/*
	 * Fields relevant to both kernel and user
	 */
	hrtime_t        ks_crtime;      /* creation time (from gethrtime()) */
	struct kstat    *ks_next;       /* kstat chain linkage */
	kid_t           ks_kid;         /* unique kstat ID */
	char            ks_module[KSTAT_STRLEN]; /* provider module name */
	uchar_t         ks_resv;        /* reserved, currently just padding */
	int             ks_instance;    /* provider module's instance */
	char            ks_name[KSTAT_STRLEN]; /* kstat name */
	uchar_t         ks_type;        /* kstat data type */
	char            ks_class[KSTAT_STRLEN]; /* kstat class */
	uchar_t         ks_flags;       /* kstat flags */
	void            *ks_data;       /* kstat type-specific data */
	uint_t          ks_ndata;       /* # of type-specific data records */
	size_t        ks_data_size;   /* total size of kstat data section */
	hrtime_t        ks_snaptime;    /* time of last data shapshot */
									/*
									* Fields relevant to kernel only
									*/
	int(*ks_update)(struct kstat *, int); /* dynamic update */
	void            *ks_private;    /* arbitrary provider-private data */
	int(*ks_snapshot)(struct kstat *, void *, int);
	void            *ks_lock;       /* protects this kstat's data */

	int				ks_returnvalue;
	int			ks_errnovalue;
} kstat_t;
#pragma pack()

#pragma pack(4)
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
		* The _INT64_TYPE is defined by the implementation (see sys/int_types.h).
		*/
		int64_t		i64;
		uint64_t	ui64;

		long		l;
		ulong_t		ul;

		/* These structure members are obsolete */

		longlong_t	ll;
		u_longlong_t	ull;
		float		f;
		double		d;
	} value;			/* value of counter */
} kstat_named_t;
#pragma pack()


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

typedef struct kstat_intr {
	uint_t intrs[KSTAT_NUM_INTRS];
} kstat_intr_t;

typedef struct kstat_io {
	u_longlong_t    nread;       /* number of bytes read */
	u_longlong_t    nwritten;    /* number of bytes written */
	uint_t          reads;       /* number of read operations */
	uint_t          writes;      /* number of write operations */
	hrtime_t        wtime;       /* cumulative wait (pre-service) time */
	hrtime_t        wlentime;    /* cumulative wait length*time product*/
	hrtime_t        wlastupdate; /* last time wait queue changed */
	hrtime_t        rtime;       /* cumulative run (service) time */
	hrtime_t        rlentime;    /* cumulative run length*time product */
	hrtime_t        rlastupdate; /* last time run queue changed */
	uint_t          wcnt;        /* count of elements in wait state */
	uint_t          rcnt;        /* count of elements in run state */
} kstat_io_t;

typedef struct kstat_timer {
	char            name[KSTAT_STRLEN+1]; /* event name */
	u_longlong_t    num_events;           /* number of events */
	hrtime_t        elapsed_time;         /* cumulative elapsed time */
	hrtime_t        min_time;             /* shortest event duration */
	hrtime_t        max_time;             /* longest event duration */
	hrtime_t        start_time;           /* previous event start time */
	hrtime_t        stop_time;            /* previous event stop time */
} kstat_timer_t;

void spl_kstat_init(void);
void spl_kstat_fini(void);

typedef uint64_t zoneid_t;
#define ALL_ZONES 0

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
extern void kstat_waitq_enter(kstat_io_t *);
extern void kstat_waitq_exit(kstat_io_t *);
extern void kstat_runq_enter(kstat_io_t *);
extern void kstat_runq_exit(kstat_io_t *);
extern void kstat_waitq_to_runq(kstat_io_t *);
extern void kstat_runq_back_to_waitq(kstat_io_t *);
extern void kstat_timer_start(kstat_timer_t *);
extern void kstat_timer_stop(kstat_timer_t *);

extern void kstat_zone_add(kstat_t *, zoneid_t);
extern void kstat_zone_remove(kstat_t *, zoneid_t);
extern int kstat_zone_find(kstat_t *, zoneid_t);

extern kstat_t *kstat_hold_bykid(kid_t kid, zoneid_t);
extern kstat_t *kstat_hold_byname(const char *, int, const char *, zoneid_t);
extern void kstat_rele(kstat_t *);

extern void kstat_set_raw_ops(kstat_t *ksp,
	int(*headers)(char *buf, size_t size),
	int(*data)(char *buf, size_t size, void *data),
	void *(*addr)(kstat_t *ksp, off_t index));

int spl_kstat_chain_id(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp);
int spl_kstat_read(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp);
int spl_kstat_write(PDEVICE_OBJECT DiskDevice, PIRP Irp, PIO_STACK_LOCATION IrpSp);

#endif  /* _SPL_KSTAT_H */
