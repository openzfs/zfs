/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
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

#ifndef	_SYS_CCOMPAT_H
#define	_SYS_CCOMPAT_H

#if  __FreeBSD_version < 1300051
#define	vm_page_valid(m) (m)->valid = VM_PAGE_BITS_ALL
#define	vm_page_do_sunbusy(m)
#define	vm_page_none_valid(m) ((m)->valid == 0)
#else
#define	vm_page_do_sunbusy(m) vm_page_sunbusy(m)
#endif

#if  __FreeBSD_version < 1300074
#define	VOP_UNLOCK1(x)	VOP_UNLOCK(x, 0)
#else
#define	VOP_UNLOCK1(x)	VOP_UNLOCK(x)
#endif

#if  __FreeBSD_version < 1300064
#define	VN_IS_DOOMED(vp)	((vp)->v_iflag & VI_DOOMED)
#endif

#if  __FreeBSD_version < 1300068
#define	VFS_VOP_VECTOR_REGISTER(x)
#endif

#if  __FreeBSD_version >= 1300076
#define	getnewvnode_reserve_()	getnewvnode_reserve()
#else
#define	getnewvnode_reserve_()	getnewvnode_reserve(1)
#endif

#if  __FreeBSD_version < 1300102
#define	ASSERT_VOP_IN_SEQC(zp)
#define	MNTK_FPLOOKUP 0
#define	vn_seqc_write_begin(vp)
#define	vn_seqc_write_end(vp)

#ifndef VFS_SMR_DECLARE
#define	VFS_SMR_DECLARE
#endif
#ifndef VFS_SMR_ZONE_SET
#define	VFS_SMR_ZONE_SET(zone)
#endif
#endif

struct hlist_node {
	struct hlist_node *next, **pprev;
};

struct hlist_head {
	struct hlist_node *first;
};

typedef struct {
	volatile int counter;
} atomic_t;

	/* BEGIN CSTYLED */
#define	hlist_for_each(p, head)                                      \
	for (p = (head)->first; p; p = (p)->next)

#define	hlist_entry(ptr, type, field)   container_of(ptr, type, field)

#define	container_of(ptr, type, member)                         \
({                                                              \
        const __typeof(((type *)0)->member) *__p = (ptr);       \
        (type *)((uintptr_t)__p - offsetof(type, member));      \
})
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
	/* BEGIN CSTYLED */
#define	READ_ONCE(x) ({			\
	__typeof(x) __var = ({		\
		barrier();		\
		ACCESS_ONCE(x);		\
	});				\
	barrier();			\
	__var;				\
})

#define	HLIST_HEAD_INIT { }
#define	HLIST_HEAD(name) struct hlist_head name = HLIST_HEAD_INIT
#define	INIT_HLIST_HEAD(head) (head)->first = NULL

#define	INIT_HLIST_NODE(node)					\
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
	return (atomic_fetchadd_int(&v->counter, 1) + 1);
}

static inline int
atomic_dec(atomic_t *v)
{
	return (atomic_fetchadd_int(&v->counter, -1) - 1);
}
#endif
