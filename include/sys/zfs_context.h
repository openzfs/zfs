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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Copyright 2011 Nexenta Systems, Inc. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2012 by Delphix. All rights reserved.
 */

#ifndef _SYS_ZFS_CONTEXT_H
#define	_SYS_ZFS_CONTEXT_H

#ifdef __KERNEL__

#include <sys/note.h>
#include <sys/types.h>
#include <sys/t_lock.h>
#include <sys/atomic.h>
#include <sys/sysmacros.h>
#include <sys/bitmap.h>
#include <sys/cmn_err.h>
#include <sys/kmem.h>
#include <sys/taskq.h>
#include <sys/buf.h>
#include <sys/param.h>
#include <sys/systm.h>
#include <sys/cpuvar.h>
#include <sys/kobj.h>
#include <sys/conf.h>
#include <sys/disp.h>
#include <sys/debug.h>
#include <sys/random.h>
#include <sys/byteorder.h>
#include <sys/systm.h>
#include <sys/list.h>
#include <sys/uio_impl.h>
#include <sys/dirent.h>
#include <sys/time.h>
#include <vm/seg_kmem.h>
#include <sys/zone.h>
#include <sys/zfs_debug.h>
#include <sys/fm/fs/zfs.h>
#include <sys/sunddi.h>
#include <sys/ctype.h>
#include <sys/disp.h>
#include <linux/dcache_compat.h>

#else /* _KERNEL */

#define	_SYS_MUTEX_H
#define	_SYS_RWLOCK_H
#define	_SYS_CONDVAR_H
#define	_SYS_SYSTM_H
#define	_SYS_T_LOCK_H
#define	_SYS_VNODE_H
#define	_SYS_VFS_H
#define	_SYS_SUNDDI_H
#define	_SYS_CALLB_H

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <assert.h>
#include <alloca.h>
#include <umem.h>
#include <limits.h>
#include <atomic.h>
#include <dirent.h>
#include <time.h>
#include <ctype.h>
#include <sys/note.h>
#include <sys/types.h>
#include <sys/cred.h>
#include <sys/sysmacros.h>
#include <sys/bitmap.h>
#include <sys/resource.h>
#include <sys/byteorder.h>
#include <sys/list.h>
#include <sys/uio.h>
#include <sys/zfs_debug.h>
#include <sys/sdt.h>
#include <sys/kstat.h>
#include <sys/u8_textprep.h>
#include <sys/fm/fs/zfs.h>
#include <sys/sunddi.h>
#include <sys/debug.h>

/*
 * Stack
 */

#define  noinline	__attribute__((noinline))

/*
 * Debugging
 */

/*
 * Note that we are not using the debugging levels.
 */

#define	CE_CONT		0	/* continuation		*/
#define	CE_NOTE		1	/* notice		*/
#define	CE_WARN		2	/* warning		*/
#define	CE_PANIC	3	/* panic		*/
#define	CE_IGNORE	4	/* print nothing	*/

extern int aok;

/*
 * ZFS debugging
 */

extern void dprintf_setup(int *argc, char **argv);
extern void __dprintf(const char *file, const char *func,
    int line, const char *fmt, ...);
extern void cmn_err(int, const char *, ...);
extern void vcmn_err(int, const char *, __va_list);
extern void panic(const char *, ...);
extern void vpanic(const char *, __va_list);

#define	fm_panic	panic

/*
 * DTrace SDT probes have different signatures in userland than they do in
 * kernel.  If they're being used in kernel code, re-define them out of
 * existence for their counterparts in libzpool.
 */

#ifdef DTRACE_PROBE
#undef	DTRACE_PROBE
#define	DTRACE_PROBE(a)	((void)0)
#endif	/* DTRACE_PROBE */

#ifdef DTRACE_PROBE1
#undef	DTRACE_PROBE1
#define	DTRACE_PROBE1(a, b, c)	((void)0)
#endif	/* DTRACE_PROBE1 */

#ifdef DTRACE_PROBE2
#undef	DTRACE_PROBE2
#define	DTRACE_PROBE2(a, b, c, d, e)	((void)0)
#endif	/* DTRACE_PROBE2 */

#ifdef DTRACE_PROBE3
#undef	DTRACE_PROBE3
#define	DTRACE_PROBE3(a, b, c, d, e, f, g)	((void)0)
#endif	/* DTRACE_PROBE3 */

#ifdef DTRACE_PROBE4
#undef	DTRACE_PROBE4
#define	DTRACE_PROBE4(a, b, c, d, e, f, g, h, i)	((void)0)
#endif	/* DTRACE_PROBE4 */

/*
 * Threads
 */
#define	TS_MAGIC		0x72f158ab4261e538ull
#define	TS_RUN			0x00000002
#ifdef __linux__
#define	STACK_SIZE		8192	/* Linux x86 and amd64 */
#else
#define	STACK_SIZE		24576	/* Solaris */
#endif

/* in libzpool, p0 exists only to have its address taken */
typedef struct proc {
	uintptr_t	this_is_never_used_dont_dereference_it;
} proc_t;

extern struct proc p0;
#define	curproc		(&p0)

typedef void (*thread_func_t)(void *);
typedef void (*thread_func_arg_t)(void *);
typedef pthread_t kt_did_t;

typedef struct kthread {
	kt_did_t	t_tid;
	thread_func_t	t_func;
	void *		t_arg;
} kthread_t;

#define	tsd_get(key)			pthread_getspecific(key)
#define	tsd_set(key, val)		pthread_setspecific(key, val)
#define	curthread			zk_thread_current()
#define	thread_exit			zk_thread_exit
#define	thread_create(stk, stksize, func, arg, len, pp, state, pri)	\
	zk_thread_create(stk, stksize, (thread_func_t)func, arg,	\
			 len, NULL, state, pri, PTHREAD_CREATE_DETACHED)
#define	thread_join(t)			zk_thread_join(t)
#define	newproc(f,a,cid,pri,ctp,pid)	(ENOSYS)

extern kthread_t *zk_thread_current(void);
extern void zk_thread_exit(void);
extern kthread_t *zk_thread_create(caddr_t stk, size_t  stksize,
	thread_func_t func, void *arg, size_t len,
	proc_t *pp, int state, pri_t pri, int detachstate);
extern void zk_thread_join(kt_did_t tid);

#define	kpreempt_disable()	((void)0)
#define	kpreempt_enable()	((void)0)

#define	PS_NONE		-1

#define	issig(why)	(FALSE)
#define	ISSIG(thr, why)	(FALSE)

/*
 * Mutexes
 */
#define	MTX_MAGIC	0x9522f51362a6e326ull
#define	MTX_INIT	((void *)NULL)
#define	MTX_DEST	((void *)-1UL)

typedef struct kmutex {
	void		*m_owner;
	uint64_t	m_magic;
	pthread_mutex_t	m_lock;
} kmutex_t;

#define	MUTEX_DEFAULT	0
#define MUTEX_HELD(m)	((m)->m_owner == curthread)
#define	MUTEX_NOT_HELD(m) (!MUTEX_HELD(m))

extern void mutex_init(kmutex_t *mp, char *name, int type, void *cookie);
extern void mutex_destroy(kmutex_t *mp);
extern void mutex_enter(kmutex_t *mp);
extern void mutex_exit(kmutex_t *mp);
extern int mutex_tryenter(kmutex_t *mp);
extern void *mutex_owner(kmutex_t *mp);
extern int mutex_held(kmutex_t *mp);

/*
 * RW locks
 */
#define	RW_MAGIC	0x4d31fb123648e78aull
#define	RW_INIT		((void *)NULL)
#define	RW_DEST		((void *)-1UL)

typedef struct krwlock {
	void			*rw_owner;
	void			*rw_wr_owner;
	uint64_t		rw_magic;
	pthread_rwlock_t	rw_lock;
	uint_t			rw_readers;
} krwlock_t;

typedef int krw_t;

#define	RW_READER	0
#define	RW_WRITER	1
#define RW_DEFAULT	RW_READER

#define	RW_READ_HELD(x)		((x)->rw_readers > 0)
#define	RW_WRITE_HELD(x)	((x)->rw_wr_owner == curthread)
#define	RW_LOCK_HELD(x)		(RW_READ_HELD(x) || RW_WRITE_HELD(x))

extern void rw_init(krwlock_t *rwlp, char *name, int type, void *arg);
extern void rw_destroy(krwlock_t *rwlp);
extern void rw_enter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryenter(krwlock_t *rwlp, krw_t rw);
extern int rw_tryupgrade(krwlock_t *rwlp);
extern void rw_exit(krwlock_t *rwlp);
#define	rw_downgrade(rwlp) do { } while (0)

extern uid_t crgetuid(cred_t *cr);
extern gid_t crgetgid(cred_t *cr);
extern int crgetngroups(cred_t *cr);
extern gid_t *crgetgroups(cred_t *cr);

/*
 * Condition variables
 */
#define CV_MAGIC	0xd31ea9a83b1b30c4ull

typedef struct kcondvar {
	uint64_t		cv_magic;
	pthread_cond_t		cv;
} kcondvar_t;

#define	CV_DEFAULT	0

extern void cv_init(kcondvar_t *cv, char *name, int type, void *arg);
extern void cv_destroy(kcondvar_t *cv);
extern void cv_wait(kcondvar_t *cv, kmutex_t *mp);
extern clock_t cv_timedwait(kcondvar_t *cv, kmutex_t *mp, clock_t abstime);
extern void cv_signal(kcondvar_t *cv);
extern void cv_broadcast(kcondvar_t *cv);
#define cv_timedwait_interruptible(cv, mp, at)	cv_timedwait(cv, mp, at)
#define cv_wait_interruptible(cv, mp)		cv_wait(cv, mp)
#define cv_wait_io(cv, mp)			cv_wait(cv, mp)

/*
 * kstat creation, installation and deletion
 */
extern kstat_t *kstat_create(char *, int,
    char *, char *, uchar_t, ulong_t, uchar_t);
extern void kstat_install(kstat_t *);
extern void kstat_delete(kstat_t *);

/*
 * Kernel memory
 */
#define	KM_SLEEP		UMEM_NOFAIL
#define	KM_PUSHPAGE		KM_SLEEP
#define	KM_NOSLEEP		UMEM_DEFAULT
#define	KM_NODEBUG		0x0
#define	KMC_NODEBUG		UMC_NODEBUG
#define	KMC_KMEM		0x0
#define	KMC_VMEM		0x0
#define	kmem_alloc(_s, _f)	umem_alloc(_s, _f)
#define	kmem_zalloc(_s, _f)	umem_zalloc(_s, _f)
#define	kmem_free(_b, _s)	umem_free(_b, _s)
#define	vmem_alloc(_s, _f)	kmem_alloc(_s, _f)
#define	vmem_zalloc(_s, _f)	kmem_zalloc(_s, _f)
#define	vmem_free(_b, _s)	kmem_free(_b, _s)
#define	kmem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i) \
	umem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i)
#define	kmem_cache_destroy(_c)	umem_cache_destroy(_c)
#define	kmem_cache_alloc(_c, _f) umem_cache_alloc(_c, _f)
#define	kmem_cache_free(_c, _b)	umem_cache_free(_c, _b)
#define	kmem_debugging()	0
#define	kmem_cache_reap_now(_c)		/* nothing */
#define	kmem_cache_set_move(_c, _cb)	/* nothing */
#define	POINTER_INVALIDATE(_pp)		/* nothing */
#define	POINTER_IS_VALID(_p)	0

typedef umem_cache_t kmem_cache_t;

typedef enum kmem_cbrc {
	KMEM_CBRC_YES,
	KMEM_CBRC_NO,
	KMEM_CBRC_LATER,
	KMEM_CBRC_DONT_NEED,
	KMEM_CBRC_DONT_KNOW
} kmem_cbrc_t;

/*
 * Task queues
 */
typedef struct taskq taskq_t;
typedef uintptr_t taskqid_t;
typedef void (task_func_t)(void *);

typedef struct taskq_ent {
	struct taskq_ent	*tqent_next;
	struct taskq_ent	*tqent_prev;
	task_func_t		*tqent_func;
	void			*tqent_arg;
	uintptr_t		tqent_flags;
} taskq_ent_t;

#define	TQENT_FLAG_PREALLOC	0x1	/* taskq_dispatch_ent used */

#define	TASKQ_PREPOPULATE	0x0001
#define	TASKQ_CPR_SAFE		0x0002	/* Use CPR safe protocol */
#define	TASKQ_DYNAMIC		0x0004	/* Use dynamic thread scheduling */
#define	TASKQ_THREADS_CPU_PCT	0x0008	/* Scale # threads by # cpus */
#define	TASKQ_DC_BATCH		0x0010	/* Mark threads as batch */

#define	TQ_SLEEP	KM_SLEEP	/* Can block for memory */
#define	TQ_NOSLEEP	KM_NOSLEEP	/* cannot block for memory; may fail */
#define	TQ_PUSHPAGE	KM_PUSHPAGE	/* Cannot perform I/O */
#define	TQ_NOQUEUE	0x02		/* Do not enqueue if can't dispatch */
#define	TQ_FRONT	0x08		/* Queue in front */

extern taskq_t *system_taskq;

extern taskq_t	*taskq_create(const char *, int, pri_t, int, int, uint_t);
#define	taskq_create_proc(a, b, c, d, e, p, f) \
	    (taskq_create(a, b, c, d, e, f))
#define	taskq_create_sysdc(a, b, d, e, p, dc, f) \
	    (taskq_create(a, b, maxclsyspri, d, e, f))
extern taskqid_t taskq_dispatch(taskq_t *, task_func_t, void *, uint_t);
extern taskqid_t taskq_dispatch_delay(taskq_t *, task_func_t, void *, uint_t,
    clock_t);
extern void	taskq_dispatch_ent(taskq_t *, task_func_t, void *, uint_t,
    taskq_ent_t *);
extern int	taskq_empty_ent(taskq_ent_t *);
extern void	taskq_init_ent(taskq_ent_t *);
extern void	taskq_destroy(taskq_t *);
extern void	taskq_wait(taskq_t *);
extern void	taskq_wait_id(taskq_t *, taskqid_t);
extern int	taskq_member(taskq_t *, kthread_t *);
extern int	taskq_cancel_id(taskq_t *, taskqid_t);
extern void	system_taskq_init(void);
extern void	system_taskq_fini(void);

#define	XVA_MAPSIZE	3
#define	XVA_MAGIC	0x78766174

/*
 * vnodes
 */
typedef struct vnode {
	uint64_t	v_size;
	int		v_fd;
	char		*v_path;
} vnode_t;

#define	AV_SCANSTAMP_SZ	32		/* length of anti-virus scanstamp */

typedef struct xoptattr {
	timestruc_t	xoa_createtime;	/* Create time of file */
	uint8_t		xoa_archive;
	uint8_t		xoa_system;
	uint8_t		xoa_readonly;
	uint8_t		xoa_hidden;
	uint8_t		xoa_nounlink;
	uint8_t		xoa_immutable;
	uint8_t		xoa_appendonly;
	uint8_t		xoa_nodump;
	uint8_t		xoa_settable;
	uint8_t		xoa_opaque;
	uint8_t		xoa_av_quarantined;
	uint8_t		xoa_av_modified;
	uint8_t		xoa_av_scanstamp[AV_SCANSTAMP_SZ];
	uint8_t		xoa_reparse;
	uint8_t		xoa_offline;
	uint8_t		xoa_sparse;
} xoptattr_t;

typedef struct vattr {
	uint_t		va_mask;	/* bit-mask of attributes */
	u_offset_t	va_size;	/* file size in bytes */
} vattr_t;


typedef struct xvattr {
	vattr_t		xva_vattr;	/* Embedded vattr structure */
	uint32_t	xva_magic;	/* Magic Number */
	uint32_t	xva_mapsize;	/* Size of attr bitmap (32-bit words) */
	uint32_t	*xva_rtnattrmapp;	/* Ptr to xva_rtnattrmap[] */
	uint32_t	xva_reqattrmap[XVA_MAPSIZE];	/* Requested attrs */
	uint32_t	xva_rtnattrmap[XVA_MAPSIZE];	/* Returned attrs */
	xoptattr_t	xva_xoptattrs;	/* Optional attributes */
} xvattr_t;

typedef struct vsecattr {
	uint_t		vsa_mask;	/* See below */
	int		vsa_aclcnt;	/* ACL entry count */
	void		*vsa_aclentp;	/* pointer to ACL entries */
	int		vsa_dfaclcnt;	/* default ACL entry count */
	void		*vsa_dfaclentp;	/* pointer to default ACL entries */
	size_t		vsa_aclentsz;	/* ACE size in bytes of vsa_aclentp */
} vsecattr_t;

#define	AT_TYPE		0x00001
#define	AT_MODE		0x00002
#define	AT_UID		0x00004
#define	AT_GID		0x00008
#define	AT_FSID		0x00010
#define	AT_NODEID	0x00020
#define	AT_NLINK	0x00040
#define	AT_SIZE		0x00080
#define	AT_ATIME	0x00100
#define	AT_MTIME	0x00200
#define	AT_CTIME	0x00400
#define	AT_RDEV		0x00800
#define	AT_BLKSIZE	0x01000
#define	AT_NBLOCKS	0x02000
#define	AT_SEQ		0x08000
#define	AT_XVATTR	0x10000

#define	CRCREAT		0

extern int fop_getattr(vnode_t *vp, vattr_t *vap);

#define	VOP_CLOSE(vp, f, c, o, cr, ct)	vn_close(vp)
#define	VOP_PUTPAGE(vp, of, sz, fl, cr, ct)	0
#define	VOP_GETATTR(vp, vap, fl, cr, ct)  fop_getattr((vp), (vap));

#define	VOP_FSYNC(vp, f, cr, ct)	fsync((vp)->v_fd)

#define	VN_RELE(vp)	vn_close(vp)

extern int vn_open(char *path, int x1, int oflags, int mode, vnode_t **vpp,
    int x2, int x3);
extern int vn_openat(char *path, int x1, int oflags, int mode, vnode_t **vpp,
    int x2, int x3, vnode_t *vp, int fd);
extern int vn_rdwr(int uio, vnode_t *vp, void *addr, ssize_t len,
    offset_t offset, int x1, int x2, rlim64_t x3, void *x4, ssize_t *residp);
extern void vn_close(vnode_t *vp);

#define	vn_remove(path, x1, x2)		remove(path)
#define	vn_rename(from, to, seg)	rename((from), (to))
#define	vn_is_readonly(vp)		B_FALSE

extern vnode_t *rootdir;

#include <sys/file.h>		/* for FREAD, FWRITE, etc */

/*
 * Random stuff
 */
#define	ddi_get_lbolt()		(gethrtime() >> 23)
#define	ddi_get_lbolt64()	(gethrtime() >> 23)
#define	hz	119	/* frequency when using gethrtime() >> 23 for lbolt */

extern void delay(clock_t ticks);

#define	SEC_TO_TICK(sec)	((sec) * hz)
#define	MSEC_TO_TICK(msec)	((msec) / (MILLISEC / hz))
#define	USEC_TO_TICK(usec)	((usec) / (MICROSEC / hz))
#define	NSEC_TO_TICK(usec)	((usec) / (NANOSEC / hz))

#define	gethrestime_sec() time(NULL)
#define	gethrestime(t) \
	do {\
		(t)->tv_sec = gethrestime_sec();\
		(t)->tv_nsec = 0;\
	} while (0);

#define	max_ncpus	64

#define	minclsyspri	60
#define	maxclsyspri	99

#define	CPU_SEQID	(pthread_self() & (max_ncpus - 1))

#define	kcred		NULL
#define	CRED()		NULL

#define	ptob(x)		((x) * PAGESIZE)

extern uint64_t physmem;

extern int highbit(ulong_t i);
extern int random_get_bytes(uint8_t *ptr, size_t len);
extern int random_get_pseudo_bytes(uint8_t *ptr, size_t len);

extern void kernel_init(int);
extern void kernel_fini(void);

struct spa;
extern void nicenum(uint64_t num, char *buf);
extern void show_pool_stats(struct spa *);

typedef struct callb_cpr {
	kmutex_t	*cc_lockp;
} callb_cpr_t;

#define	CALLB_CPR_INIT(cp, lockp, func, name)	{		\
	(cp)->cc_lockp = lockp;					\
}

#define	CALLB_CPR_SAFE_BEGIN(cp) {				\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
}

#define	CALLB_CPR_SAFE_END(cp, lockp) {				\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
}

#define	CALLB_CPR_EXIT(cp) {					\
	ASSERT(MUTEX_HELD((cp)->cc_lockp));			\
	mutex_exit((cp)->cc_lockp);				\
}

#define	zone_dataset_visible(x, y)	(1)
#define	INGLOBALZONE(z)			(1)

extern char *kmem_vasprintf(const char *fmt, va_list adx);
extern char *kmem_asprintf(const char *fmt, ...);
#define	strfree(str) kmem_free((str), strlen(str)+1)

/*
 * Hostname information
 */
extern char hw_serial[];	/* for userland-emulated hostid access */
extern int ddi_strtoul(const char *str, char **nptr, int base,
    unsigned long *result);

extern int ddi_strtoull(const char *str, char **nptr, int base,
    u_longlong_t *result);

/* ZFS Boot Related stuff. */

struct _buf {
	intptr_t	_fd;
};

struct bootstat {
	uint64_t st_size;
};

typedef struct ace_object {
	uid_t		a_who;
	uint32_t	a_access_mask;
	uint16_t	a_flags;
	uint16_t	a_type;
	uint8_t		a_obj_type[16];
	uint8_t		a_inherit_obj_type[16];
} ace_object_t;


#define	ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE	0x05
#define	ACE_ACCESS_DENIED_OBJECT_ACE_TYPE	0x06
#define	ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE	0x07
#define	ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE	0x08

extern struct _buf *kobj_open_file(char *name);
extern int kobj_read_file(struct _buf *file, char *buf, unsigned size,
    unsigned off);
extern void kobj_close_file(struct _buf *file);
extern int kobj_get_filesize(struct _buf *file, uint64_t *size);
extern int zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr);
extern int zfs_secpolicy_rename_perms(const char *from, const char *to,
    cred_t *cr);
extern int zfs_secpolicy_destroy_perms(const char *name, cred_t *cr);
extern zoneid_t getzoneid(void);

/* SID stuff */
typedef struct ksiddomain {
	uint_t	kd_ref;
	uint_t	kd_len;
	char	*kd_name;
} ksiddomain_t;

ksiddomain_t *ksid_lookupdomain(const char *);
void ksiddomain_rele(ksiddomain_t *);

#define	DDI_SLEEP	KM_SLEEP
#define	ddi_log_sysevent(_a, _b, _c, _d, _e, _f, _g) \
	sysevent_post_event(_c, _d, _b, "libzpool", _e, _f)

#endif /* _KERNEL */

#endif	/* _SYS_ZFS_CONTEXT_H */
