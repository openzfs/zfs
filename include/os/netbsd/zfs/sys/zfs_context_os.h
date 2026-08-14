// SPDX-License-Identifier: BSD-2-Clause
#ifndef ZFS_CONTEXT_OS_H_
#define	ZFS_CONTEXT_OS_H_

#include <sys/condvar.h>
#include <sys/rwlock.h>
#include <sys/sig.h>
#include_next <sys/sdt.h>
#include <sys/misc.h>
#include <sys/pathname.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <sys/ccompat.h>
#include <linux/types.h>
#include <sys/specificdata.h>

#define	taskq_create_sysdc(a, b, d, e, p, dc, f) \
	    ((void) sizeof (dc), taskq_create(a, b, maxclsyspri, d, e, f))

/*
 * Thread-specific data via NetBSD lwp_specific(9).
 * Keys are specificdata_key_t (unsigned int).
 */
#define	tsd_create(keyp, destructor)	do {				\
	specificdata_key_t _k;						\
	lwp_specific_key_create(&_k, (destructor));			\
	*(keyp) = _k;							\
} while (0)

#define	tsd_destroy(keyp)	lwp_specific_key_delete(*(keyp))
#define	tsd_get(key)		lwp_getspecific(key)
#define	tsd_set(key, value)	(lwp_setspecific((key), (value)), 0)
#define	fm_panic		panic

extern int zfs_debug_level;
extern kmutex_t zfs_debug_mtx;
#define	ZFS_LOG(lvl, ...) do {					\
		if (((lvl) & 0xff) <= zfs_debug_level) {	\
			mutex_enter(&zfs_debug_mtx);		\
			printf("%s:%u[%d]: ",			\
			    __func__, __LINE__, (lvl));		\
			printf(__VA_ARGS__);			\
			printf("\n");				\
			mutex_exit(&zfs_debug_mtx);		\
	}							\
} while (0)

#define	MSEC_TO_TICK(msec)	(howmany((hrtime_t)(msec) * hz, MILLISEC))
extern int hz;
extern int tick;
typedef int fstrans_cookie_t;
#define	spl_fstrans_mark()	(0)
#define	spl_fstrans_unmark(x)	((void)x)
#ifdef _KERNEL
#include <sys/signalvar.h>
#define	signal_pending(x) \
	(sigispending(((x) != NULL) ? (lwp_t *)(x) : curlwp, 0) != 0)
#else
#define	signal_pending(x)	(0)
#endif
#define	current			curthread
#define	thread_join(x)
typedef struct opensolaris_utsname	utsname_t;
extern utsname_t *utsname(void);
extern int spa_import_rootpool(const char *name, bool checkpointrewind);

extern int zfs_ioctl_debug;

#ifdef _KERNEL
/* NetBSD buf(9) already uses this name. */
#define	buf_init	zfs_arc_buf_init
#define	buf_fini	zfs_arc_buf_fini
/*
 * Kernel setjmp/longjmp take label_t * (sys/systm.h).  Defining
 * LUAI_THROW here keeps module/lua/ldo.c on the upstream Linux
 * and FreeBSD branches.  libzpool is not _KERNEL and uses the
 * 2-arg longjmp((c)->b, 1) path.
 *
 * Do not cap LUAI_MAXCCALLS below the upstream 20: the parser's
 * enterlevel() counts nested if/function syntax against that
 * limit (tst.nested_pos, tst.libraries, tst.get_string_props).
 * Runtime C-stack use (gsub) is guarded by stack_remaining()
 * and LUAI_MINCSTACK in module/lua/ldo.c.
 */
#ifndef LUAI_THROW
#define	LUAI_THROW(L, c)	longjmp(&(c)->b)
#define	LUAI_TRY(L, c, a)	if (setjmp(&(c)->b) == 0) { a }
#define	luai_jmpbuf		label_t
#endif
#endif

#endif
