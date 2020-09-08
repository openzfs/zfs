/*
 * Copyright (c) 2020 iXsystems, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef ZFS_CONTEXT_OS_H_
#define	ZFS_CONTEXT_OS_H_

#include <sys/condvar.h>
#include <sys/rwlock.h>
#include <sys/sig.h>
#include_next <sys/sdt.h>
#include <sys/misc.h>
#include <sys/kdb.h>
#include <sys/pathname.h>
#include <sys/conf.h>
#include <sys/types.h>
#include <sys/ccompat.h>
#include <linux/types.h>

#define	cond_resched()		kern_yield(PRI_USER)

#define	taskq_create_sysdc(a, b, d, e, p, dc, f) \
	    (taskq_create(a, b, maxclsyspri, d, e, f))

#define	tsd_create(keyp, destructor)    do {                 \
		*(keyp) = osd_thread_register((destructor));         \
		KASSERT(*(keyp) > 0, ("cannot register OSD"));       \
} while (0)

#define	tsd_destroy(keyp)	osd_thread_deregister(*(keyp))
#define	tsd_get(key)	osd_thread_get(curthread, (key))
#define	tsd_set(key, value)	osd_thread_set(curthread, (key), (value))
#define	fm_panic	panic

#define	cond_resched()		kern_yield(PRI_USER)
extern int zfs_debug_level;
extern struct mtx zfs_debug_mtx;
#define	ZFS_LOG(lvl, ...) do {   \
		if (((lvl) & 0xff) <= zfs_debug_level) {  \
			mtx_lock(&zfs_debug_mtx);			  \
			printf("%s:%u[%d]: ",				  \
			    __func__, __LINE__, (lvl)); \
			printf(__VA_ARGS__); \
			printf("\n"); \
			if ((lvl) & 0x100) \
				kdb_backtrace(); \
			mtx_unlock(&zfs_debug_mtx);	\
	}	   \
} while (0)

#define	MSEC_TO_TICK(msec)	(howmany((hrtime_t)(msec) * hz, MILLISEC))
extern int hz;
extern int tick;
typedef int fstrans_cookie_t;
#define	spl_fstrans_mark() (0)
#define	spl_fstrans_unmark(x) (x = 0)
#define	signal_pending(x) SIGPENDING(x)
#define	current curthread
#define	thread_join(x)
typedef struct opensolaris_utsname	utsname_t;
extern utsname_t *utsname(void);
extern int spa_import_rootpool(const char *name, bool checkpointrewind);
#endif
