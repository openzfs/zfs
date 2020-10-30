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

#define	MSEC_TO_TICK(msec)		((msec) / (MILLISEC / hz))

#define	KMALLOC_MAX_SIZE		(128 * 1024) // Win32 MAXPHYS ?

#define	MNTTYPE_ZFS_SUBTYPE ('Z'<<24|'F'<<16|'S'<<8)

#ifndef MAX_UPL_TRANSFER
#define	MAX_UPL_TRANSFER 256
#endif

#define	flock64_t	struct flock

struct spa_iokit;
typedef struct spa_iokit spa_iokit_t;

#define	noinline		__attribute__((noinline))

/* really? */
#define	kpreempt_disable()	((void)0)
#define	kpreempt_enable()	((void)0)
#define	cond_resched()	(void)YieldProcessor()
#define	schedule()	(void)YieldProcessor()

#define	current		curthread

#define	vmem_alloc(A, B)		zfs_kmem_alloc((A), (B))

extern boolean_t ml_set_interrupts_enabled(boolean_t);
extern PDRIVER_OBJECT WIN_DriverObject;

/*
 * Ok this is pretty gross - until we can get rid of it from lua -
 * it works as long as it doesn't parse strings
 */
#define sscanf sscanf_s

#define	DIRENT_RECLEN(namelen)  (((namelen) + 7) & ~7)

/* Make sure kmem and vmem are already included */
#include <sys/seg_kmem.h>
#include <sys/kmem.h>

/* Since Linux code uses vmem_free() and we already have one: */
#define	vmem_free(A, B)			zfs_kmem_free((A), (B))
#define	vmem_alloc(A, B)		zfs_kmem_alloc((A), (B))
#define	vmem_zalloc(A, B)		zfs_kmem_zalloc((A), (B))

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

extern void kx_qsort(void *array, size_t nm, size_t member_size,
    int (*cmpf)(const void *, const void *));
// #define	qsort kx_qsort

#define	strstr kmem_strstr

void spa_create_os(void *spa);
void spa_export_os(void *spa);
void spa_activate_os(void *spa);
void spa_deactivate_os(void *spa);

#endif // _KERNEL

#endif
