/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
#ifndef _SPL_ZFS_CONTEXT_OS_H
#define	_SPL_ZFS_CONTEXT_OS_H

#include <sys/utsname.h>
#include <sys/ioctl.h>
#include <sys/callb.h>
#include <sys/string.h>

#include <sys/ldi_buf.h>

#define	ZIO_OS_FIELDS \
	struct { \
		ldi_buf_t	zm_buf; \
	} macos;


#define	MSEC_TO_TICK(msec)		((msec) / (MILLISEC / hz))

#define	KMALLOC_MAX_SIZE		MAXPHYS

#ifndef MAX_UPL_TRANSFER
#define	MAX_UPL_TRANSFER 256
#endif

#define	flock64_t	struct flock

/*
 * XNU reserves fileID 1-15, so we remap them high.
 * 2 is root-of-the-mount.
 * If ID is same as root, return 2. Otherwise, if it is 0-15, return
 * adjusted, otherwise, return as-is.
 * See hfs_format.h: kHFSRootFolderID, kHFSExtentsFileID, ...
 */
#define	INO_ROOT 		2ULL
#define	INO_RESERVED		16ULL	/* [0-15] reserved. */
#define	INO_ISRESERVED(ID)	((ID) < (INO_RESERVED))
/*				0xFFFFFFFFFFFFFFF0 */
#define	INO_MAP			((uint64_t)-INO_RESERVED) /* -16, -15, .., -1 */

#define	INO_ZFSTOXNU(ID, ROOT)	\
	((ID) == (ROOT)?INO_ROOT:(INO_ISRESERVED(ID)?INO_MAP+(ID):(ID)))

/*
 * This macro relies on *unsigned*.
 * If asking for 2, return rootID. If in special range, adjust to
 * normal, otherwise, return as-is.
 */
#define	INO_XNUTOZFS(ID, ROOT)	\
	((ID) == INO_ROOT)?(ROOT): \
	(INO_ISRESERVED((ID)-INO_MAP))?((ID)-INO_MAP):(ID)

struct spa_iokit;
typedef struct spa_iokit spa_iokit_t;

#define	noinline		__attribute__((noinline))

/* really? */
#define	kpreempt_disable()	((void)0)
#define	kpreempt_enable()	((void)0)
#define	cond_resched()	(void)thread_block(THREAD_CONTINUE_NULL);
#define	schedule()	(void)thread_block(THREAD_CONTINUE_NULL);

#define	current		curthread

extern boolean_t ml_set_interrupts_enabled(boolean_t);

/* Make sure kmem and vmem are already included */
#include <sys/seg_kmem.h>
#include <sys/kmem.h>

/*
 * We could add another field to zfs_cmd_t, but since we should be
 * moving to the new-style ioctls, send and recv still hang on to old,
 * we will just (ab)use a field not used on macOS.
 * We use this field to keep userland's file offset pointer, and kernel
 * fp_offset in sync, as we have no means to access "fp_offset" in XNU.
 */
#define	zc_fd_offset zc_zoneid

typedef	int	fstrans_cookie_t;
#define	spl_fstrans_mark()		(0)
#define	spl_fstrans_unmark(x)	(x = 0)

#ifdef _KERNEL

struct hlist_node {
	struct hlist_node *next, **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

typedef struct {
	volatile int counter;
} atomic_t;

#define	ACCESS_ONCE(x) (*(volatile typeof(x) *)&(x))

#define	barrier()		__asm__ __volatile__("": : :"memory")
#define	smp_rmb()		barrier()

#define	READ_ONCE(x) ( \
{	\
			__typeof(x) __var = ( \
					{	\
					barrier();	\
					ACCESS_ONCE(x);	\
				});	\
			barrier();	\
			__var;	\
		})

#define	WRITE_ONCE(x, v) do { \
		barrier();  \
		ACCESS_ONCE(x) = (v);	\
		barrier();	\
	} while (0)

/* BEGIN CSTYLED */
#define	hlist_for_each(p, head)	\
	for (p = (head)->first; p; p = (p)->next)

#define	hlist_entry(ptr, type, field)   container_of(ptr, type, field)
/* END CSTYLED */

static inline void
hlist_add_head(struct hlist_node *n, struct hlist_head *h)
{
	n->next = h->first;
	if (h->first != NULL)
		h->first->pprev = &n->next;
	WRITE_ONCE(h->first, n);
	n->pprev = &h->first;
}

static inline void
hlist_del(struct hlist_node *n)
{
	WRITE_ONCE(*(n->pprev), n->next);
	if (n->next != NULL)
		n->next->pprev = n->pprev;
}


#define	HLIST_HEAD_INIT { }
#define	HLIST_HEAD(name) struct hlist_head name = HLIST_HEAD_INIT
#define	INIT_HLIST_HEAD(head) (head)->first = NULL

/* BEGIN CSTYLED */
#define	INIT_HLIST_NODE(node)											\
	do {																\
		(node)->next = NULL;											\
		(node)->pprev = NULL;											\
	} while (0)

/* END CSTYLED */

static inline int
atomic_read(const atomic_t *v)
{
	return (READ_ONCE(v->counter));
}

static inline int
atomic_inc(atomic_t *v)
{
	return (__sync_fetch_and_add(&v->counter, 1) + 1);
}

static inline int
atomic_dec(atomic_t *v)
{
	return (__sync_fetch_and_add(&v->counter, -1) - 1);
}

extern void spl_qsort(void *array, size_t nm, size_t member_size,
    int (*cmpf)(const void *, const void *));
#define	qsort spl_qsort

#define	strstr kmem_strstr

#define	task_io_account_read(n)
#define	task_io_account_write(n)

#ifndef SEEK_HOLE
#define	SEEK_HOLE 3
#endif

#ifndef SEEK_DATA
#define	SEEK_DATA 4
#endif

void sysctl_os_init(void);
void sysctl_os_fini(void);

/* See rant in vdev_file.c */
#define	CLOSE_ON_UNMOUNT

#ifndef MODULE_PARAM_MAX
#define	MODULE_PARAM_MAX 1024
#endif

#endif // _KERNEL

#endif
