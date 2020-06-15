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
#define	_SPL_KSTAT_H

#include <sys/types.h>
#include <sys/time.h>
#include <sys/kmem.h>
#include <sys/sysctl.h>
#include <sys/mutex.h>

#define	KSTAT_STRLEN		255
#define	KSTAT_RAW_MAX		(128*1024)

/*
 * For reference valid classes are:
 * disk, tape, net, controller, vm, kvm, hat, streams, kstat, misc
 */

#define	KSTAT_TYPE_RAW		0	/* can be anything; ks_ndata >= 1 */
#define	KSTAT_TYPE_NAMED	1	/* name/value pair; ks_ndata >= 1 */
#define	KSTAT_TYPE_INTR		2	/* interrupt stats; ks_ndata == 1 */
#define	KSTAT_TYPE_IO		3	/* I/O stats; ks_ndata == 1 */
#define	KSTAT_TYPE_TIMER	4	/* event timer; ks_ndata >= 1 */
#define	KSTAT_TYPE_TXG		5	/* txg sync; ks_ndata >= 1 */
#define	KSTAT_NUM_TYPES		6

#define	KSTAT_DATA_CHAR		0
#define	KSTAT_DATA_INT32	1
#define	KSTAT_DATA_UINT32	2
#define	KSTAT_DATA_INT64	3
#define	KSTAT_DATA_UINT64	4
#define	KSTAT_DATA_LONG		5
#define	KSTAT_DATA_ULONG	6
#define	KSTAT_DATA_STRING	7
#define	KSTAT_NUM_DATAS		8

#define	KSTAT_INTR_HARD		0
#define	KSTAT_INTR_SOFT		1
#define	KSTAT_INTR_WATCHDOG	2
#define	KSTAT_INTR_SPURIOUS	3
#define	KSTAT_INTR_MULTSVC	4
#define	KSTAT_NUM_INTRS		5

#define	KSTAT_FLAG_VIRTUAL	0x01
#define	KSTAT_FLAG_VAR_SIZE	0x02
#define	KSTAT_FLAG_WRITABLE	0x04
#define	KSTAT_FLAG_PERSISTENT	0x08
#define	KSTAT_FLAG_DORMANT	0x10
#define	KSTAT_FLAG_UNSUPPORTED (KSTAT_FLAG_VAR_SIZE | KSTAT_FLAG_WRITABLE | \
    KSTAT_FLAG_PERSISTENT | KSTAT_FLAG_DORMANT)
#define	KSTAT_FLAG_INVALID	0x20
#define	KSTAT_FLAG_LONGSTRINGS	0x40
#define	KSTAT_FLAG_NO_HEADERS	0x80

#define	KS_MAGIC		0x9d9d9d9d

/* Dynamic updates */
#define	KSTAT_READ		0
#define	KSTAT_WRITE		1

struct kstat_s;

typedef int kid_t;					/* unique kstat id */
typedef int kstat_update_t(struct kstat_s *, int);	/* dynamic update cb */

struct seq_file {
	char *sf_buf;
	size_t sf_size;
};

void seq_printf(struct seq_file *m, const char *fmt, ...);

typedef struct kstat_raw_ops {
	int (*headers)(char *buf, size_t size);
	int (*seq_headers)(struct seq_file *);
	int (*data)(char *buf, size_t size, void *data);
	void *(*addr)(struct kstat_s *ksp, loff_t index);
} kstat_raw_ops_t;

typedef struct kstat_s {
	int	ks_magic;		/* magic value */
	kid_t	ks_kid;			/* unique kstat ID */
	hrtime_t ks_crtime;		/* creation time */
	hrtime_t ks_snaptime;		/* last access time */
	char	ks_module[KSTAT_STRLEN+1]; /* provider module name */
	int	ks_instance;		/* provider module instance */
	char	ks_name[KSTAT_STRLEN+1]; /* kstat name */
	char	ks_class[KSTAT_STRLEN+1]; /* kstat class */
	uchar_t	ks_type;		/* kstat data type */
	uchar_t	ks_flags;		/* kstat flags */
	void	*ks_data;		/* kstat type-specific data */
	uint_t	ks_ndata;		/* # of type-specific data records */
	size_t	ks_data_size;		/* size of kstat data section */
	struct proc_dir_entry *ks_proc; /* proc linkage */
	kstat_update_t *ks_update;	/* dynamic updates */
	void	*ks_private;		/* private data */
	void	*ks_private1;		/* private data */
	kmutex_t ks_private_lock;	/* kstat private data lock */
	kmutex_t *ks_lock;		/* kstat data lock */
	kstat_raw_ops_t	ks_raw_ops;	/* ops table for raw type */
	char	*ks_raw_buf;	/* buf used for raw ops */
	size_t	ks_raw_bufsize;	/* size of raw ops buffer */
} kstat_t;

typedef struct kstat_named_s {
	char	name[KSTAT_STRLEN];	/* name of counter */
	uchar_t	data_type;		/* data type */
	union {
		char		c[16];	/* 128-bit int */
		int32_t		i32;	/* 32-bit signed int */
		uint32_t	ui32;	/* 32-bit unsigned int */
		int64_t		i64;	/* 64-bit signed int */
		uint64_t	ui64;	/* 64-bit unsigned int */
		long		l;	/* native signed long */
		ulong_t		ul;	/* native unsigned long */
		struct {
			union {
				char	*ptr;	/* NULL-term string */
				char	__pad[8]; /* 64-bit padding */
			} addr;
			uint32_t	len;	/* # bytes for strlen + '\0' */
		} string;
	} value;
} kstat_named_t;

#define	KSTAT_NAMED_STR_PTR(knptr) ((knptr)->value.string.addr.ptr)
#define	KSTAT_NAMED_STR_BUFLEN(knptr) ((knptr)->value.string.len)

typedef struct kstat_intr {
	uint_t intrs[KSTAT_NUM_INTRS];
} kstat_intr_t;

typedef struct kstat_io {
	u_longlong_t	nread;		/* number of bytes read */
	u_longlong_t	nwritten;	/* number of bytes written */
	uint_t		reads;		/* number of read operations */
	uint_t		writes;		/* number of write operations */
	hrtime_t	wtime;		/* cumulative wait (pre-service) time */
	hrtime_t	wlentime;	/* cumulative wait len * time product */
	hrtime_t	wlastupdate;	/* last time wait queue changed */
	hrtime_t	rtime;		/* cumulative run (service) time */
	hrtime_t	rlentime;	/* cumulative run length*time product */
	hrtime_t	rlastupdate;	/* last time run queue changed */
	uint_t		wcnt;		/* count of elements in wait state */
	uint_t		rcnt;		/* count of elements in run state */
} kstat_io_t;

typedef struct kstat_timer {
	char		name[KSTAT_STRLEN+1];	/* event name */
	u_longlong_t	num_events;		/* number of events */
	hrtime_t	elapsed_time;		/* cumulative elapsed time */
	hrtime_t	min_time;		/* shortest event duration */
	hrtime_t	max_time;		/* longest event duration */
	hrtime_t	start_time;		/* previous event start time */
	hrtime_t	stop_time;		/* previous event stop time */
} kstat_timer_t;

void spl_kstat_init(void);
void spl_kstat_fini(void);

extern void __kstat_set_raw_ops(kstat_t *ksp,
    int (*headers)(char *buf, size_t size),
    int (*data)(char *buf, size_t size, void *data),
    void* (*addr)(kstat_t *ksp, loff_t index));

extern void __kstat_set_seq_raw_ops(kstat_t *ksp,
    int (*headers)(struct seq_file *),
    int (*data)(char *buf, size_t size, void *data),
    void* (*addr)(kstat_t *ksp, loff_t index));

extern kstat_t *__kstat_create(const char *ks_module, int ks_instance,
    const char *ks_name, const char *ks_class,
    uchar_t ks_type, ulong_t ks_ndata,
    uchar_t ks_flags);
extern void __kstat_install(kstat_t *ksp);
extern void __kstat_delete(kstat_t *ksp);

#define	kstat_create(m, i, n, c, t, s, f) \
    __kstat_create(m, i, n, c, t, s, f)
#define	kstat_install(k)		__kstat_install(k)
#define	kstat_delete(k)			__kstat_delete(k)

extern void kstat_waitq_enter(kstat_io_t *);
extern void kstat_waitq_exit(kstat_io_t *);
extern void kstat_runq_enter(kstat_io_t *);
extern void kstat_runq_exit(kstat_io_t *);
extern void kstat_named_init(kstat_named_t *, const char *, uchar_t);

#define	kstat_set_seq_raw_ops(k, h, d, a) __kstat_set_seq_raw_ops(k, h, d, a)
#define	kstat_set_raw_ops(k, h, d, a) __kstat_set_raw_ops(k, h, d, a)
void kstat_named_setstr(kstat_named_t *knp, const char *src);

struct sbuf;
struct sysctl_req;
extern void sbuf_finish(struct sbuf *s);
extern struct sbuf *sbuf_new_for_sysctl(struct sbuf *s, char *buf,
    int length,	struct sysctl_req *req);

#endif  /* _SPL_KSTAT_H */
