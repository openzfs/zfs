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
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016, 2019 by Delphix. All rights reserved.
 */

#ifndef _ABD_IMPL_H
#define	_ABD_IMPL_H

#include <sys/abd.h>

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
} abd_flags_t;

typedef enum abd_stats_op {
	ABDSTAT_INCR, /* Increase abdstat values */
	ABDSTAT_DECR  /* Decrease abdstat values */
} abd_stats_op_t;

struct abd {
	abd_flags_t	abd_flags;
	uint_t		abd_size;	/* excludes scattered abd_offset */
	list_node_t	abd_gang_link;
	struct abd	*abd_parent;
	zfs_refcount_t	abd_children;
	kmutex_t	abd_mtx;
	union {
		struct abd_scatter {
			uint_t		abd_offset;
#if defined(__FreeBSD__) && defined(_KERNEL)
			uint_t  abd_chunk_size;
			void    *abd_chunks[];
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
};

struct scatterlist; /* forward declaration */

struct abd_iter {
	/* public interface */
	void		*iter_mapaddr;	/* addr corresponding to iter_pos */
	size_t		iter_mapsize;	/* length of data valid at mapaddr */

	/* private */
	abd_t		*iter_abd;	/* ABD being iterated through */
	size_t		iter_pos;
	size_t		iter_offset;	/* offset in current sg/abd_buf, */
					/* abd_offset included */
	struct scatterlist *iter_sg;	/* current sg */
};

extern abd_t *abd_zero_scatter;

abd_t *abd_gang_get_offset(abd_t *, size_t *);

/*
 * OS specific functions
 */

abd_t *abd_alloc_struct(size_t);
abd_t *abd_get_offset_scatter(abd_t *, size_t);
void abd_free_struct(abd_t *);
void abd_alloc_chunks(abd_t *, size_t);
void abd_free_chunks(abd_t *);
boolean_t abd_size_alloc_linear(size_t);
void abd_update_scatter_stats(abd_t *, abd_stats_op_t);
void abd_update_linear_stats(abd_t *, abd_stats_op_t);
void abd_verify_scatter(abd_t *);
void abd_free_linear_page(abd_t *);
/* OS specific abd_iter functions */
void abd_iter_init(struct abd_iter  *, abd_t *);
boolean_t abd_iter_at_end(struct abd_iter *);
void abd_iter_advance(struct abd_iter *, size_t);
void abd_iter_map(struct abd_iter *);
void abd_iter_unmap(struct abd_iter *);

/*
 * Helper macros
 */
#define	ABDSTAT(stat)		(abd_stats.stat.value.ui64)
#define	ABDSTAT_INCR(stat, val) \
	atomic_add_64(&abd_stats.stat.value.ui64, (val))
#define	ABDSTAT_BUMP(stat)	ABDSTAT_INCR(stat, 1)
#define	ABDSTAT_BUMPDOWN(stat)	ABDSTAT_INCR(stat, -1)

#define	ABD_SCATTER(abd)	(abd->abd_u.abd_scatter)
#define	ABD_LINEAR_BUF(abd)	(abd->abd_u.abd_linear.abd_buf)
#define	ABD_GANG(abd)		(abd->abd_u.abd_gang)

#if defined(_KERNEL)
#if defined(__FreeBSD__)
#define	abd_enter_critical(flags)	critical_enter()
#define	abd_exit_critical(flags)	critical_exit()
#else
#define	abd_enter_critical(flags)	local_irq_save(flags)
#define	abd_exit_critical(flags)	local_irq_restore(flags)
#endif
#else /* !_KERNEL */
#define	abd_enter_critical(flags)	((void)0)
#define	abd_exit_critical(flags)	((void)0)
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_IMPL_H */
