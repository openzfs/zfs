/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016, 2019 by Delphix. All rights reserved.
 */

#ifndef _ABD_H
#define	_ABD_H

#include <sys/isa_defs.h>
#include <sys/debug.h>
#include <sys/zfs_refcount.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum abd_flags {
	ABD_FLAG_LINEAR		= 1 << 0, /* is buffer linear (or scattered)? */
	ABD_FLAG_OWNER		= 1 << 1, /* does it own its data buffers? */
	ABD_FLAG_META		= 1 << 2, /* does this represent FS metadata? */
	ABD_FLAG_MULTI_ZONE  	= 1 << 3, /* pages split over memory zones */
	ABD_FLAG_MULTI_CHUNK 	= 1 << 4, /* pages split over multiple chunks */
	ABD_FLAG_LINEAR_PAGE 	= 1 << 5, /* linear but allocd from page */
	ABD_FLAG_GANG		= 1 << 6, /* mult ABDs chained together */
	ABD_FLAG_GANG_FREE	= 1 << 7, /* gang ABD is responsible for mem */
	ABD_FLAG_ZEROS		= 1 << 8, /* ABD for zero-filled buffer */
	ABD_FLAG_ALLOCD		= 1 << 9, /* we allocated the abd_t */
} abd_flags_t;

typedef struct abd {
	abd_flags_t	abd_flags;
	uint_t		abd_size;	/* excludes scattered abd_offset */
	list_node_t	abd_gang_link;
#ifdef ZFS_DEBUG
	struct abd	*abd_parent;
	zfs_refcount_t	abd_children;
#endif
	kmutex_t	abd_mtx;
	union {
		struct abd_scatter {
			uint_t		abd_offset;
#if defined(__FreeBSD__) && defined(_KERNEL)
			void    *abd_chunks[1]; /* actually variable-length */
#else
			uint_t		abd_nents;
			struct scatterlist *abd_sgl;
#endif
		} abd_scatter;
		struct abd_linear {
			void		*abd_buf;
			struct scatterlist *abd_sgl; /* for LINEAR_PAGE */
		} abd_linear;
		struct abd_gang {
			list_t abd_gang_chain;
		} abd_gang;
	} abd_u;
} abd_t;

typedef int abd_iter_func_t(void *buf, size_t len, void *priv);
typedef int abd_iter_func2_t(void *bufa, void *bufb, size_t len, void *priv);

extern int zfs_abd_scatter_enabled;

/*
 * Allocations and deallocations
 */

__attribute__((malloc))
abd_t *abd_alloc(size_t, boolean_t);
__attribute__((malloc))
abd_t *abd_alloc_linear(size_t, boolean_t);
__attribute__((malloc))
abd_t *abd_alloc_gang(void);
__attribute__((malloc))
abd_t *abd_alloc_for_io(size_t, boolean_t);
__attribute__((malloc))
abd_t *abd_alloc_sametype(abd_t *, size_t);
boolean_t abd_size_alloc_linear(size_t);
void abd_gang_add(abd_t *, abd_t *, boolean_t);
void abd_free(abd_t *);
abd_t *abd_get_offset(abd_t *, size_t);
abd_t *abd_get_offset_size(abd_t *, size_t, size_t);
abd_t *abd_get_offset_struct(abd_t *, abd_t *, size_t, size_t);
abd_t *abd_get_zeros(size_t);
abd_t *abd_get_from_buf(void *, size_t);
void abd_cache_reap_now(void);

/*
 * Conversion to and from a normal buffer
 */

void *abd_to_buf(abd_t *);
void *abd_borrow_buf(abd_t *, size_t);
void *abd_borrow_buf_copy(abd_t *, size_t);
void abd_return_buf(abd_t *, void *, size_t);
void abd_return_buf_copy(abd_t *, void *, size_t);
void abd_take_ownership_of_buf(abd_t *, boolean_t);
void abd_release_ownership_of_buf(abd_t *);

/*
 * ABD operations
 */

int abd_iterate_func(abd_t *, size_t, size_t, abd_iter_func_t *, void *);
int abd_iterate_func2(abd_t *, abd_t *, size_t, size_t, size_t,
    abd_iter_func2_t *, void *);
void abd_copy_off(abd_t *, abd_t *, size_t, size_t, size_t);
void abd_copy_from_buf_off(abd_t *, const void *, size_t, size_t);
void abd_copy_to_buf_off(void *, abd_t *, size_t, size_t);
int abd_cmp(abd_t *, abd_t *);
int abd_cmp_buf_off(abd_t *, const void *, size_t, size_t);
void abd_zero_off(abd_t *, size_t, size_t);
void abd_verify(abd_t *);

void abd_raidz_gen_iterate(abd_t **cabds, abd_t *dabd,
	ssize_t csize, ssize_t dsize, const unsigned parity,
	void (*func_raidz_gen)(void **, const void *, size_t, size_t));
void abd_raidz_rec_iterate(abd_t **cabds, abd_t **tabds,
	ssize_t tsize, const unsigned parity,
	void (*func_raidz_rec)(void **t, const size_t tsize, void **c,
	const unsigned *mul),
	const unsigned *mul);

/*
 * Wrappers for calls with offsets of 0
 */

static inline void
abd_copy(abd_t *dabd, abd_t *sabd, size_t size)
{
	abd_copy_off(dabd, sabd, 0, 0, size);
}

static inline void
abd_copy_from_buf(abd_t *abd, const void *buf, size_t size)
{
	abd_copy_from_buf_off(abd, buf, 0, size);
}

static inline void
abd_copy_to_buf(void* buf, abd_t *abd, size_t size)
{
	abd_copy_to_buf_off(buf, abd, 0, size);
}

static inline int
abd_cmp_buf(abd_t *abd, const void *buf, size_t size)
{
	return (abd_cmp_buf_off(abd, buf, 0, size));
}

static inline void
abd_zero(abd_t *abd, size_t size)
{
	abd_zero_off(abd, 0, size);
}

/*
 * ABD type check functions
 */
static inline boolean_t
abd_is_linear(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_LINEAR) ? B_TRUE : B_FALSE);
}

static inline boolean_t
abd_is_linear_page(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_LINEAR_PAGE) ? B_TRUE : B_FALSE);
}

static inline boolean_t
abd_is_gang(abd_t *abd)
{
	return ((abd->abd_flags & ABD_FLAG_GANG) ? B_TRUE : B_FALSE);
}

static inline uint_t
abd_get_size(abd_t *abd)
{
	return (abd->abd_size);
}

/*
 * Module lifecycle
 * Defined in each specific OS's abd_os.c
 */

void abd_init(void);
void abd_fini(void);

/*
 * Linux ABD bio functions
 */
#if defined(__linux__) && defined(_KERNEL)
unsigned int abd_bio_map_off(struct bio *, abd_t *, unsigned int, size_t);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
