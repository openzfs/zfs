// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */
/*
 * Copyright (c) 2014 by Chunwei Chen. All rights reserved.
 * Copyright (c) 2016, 2019 by Delphix. All rights reserved.
 * Copyright (c) 2023, 2024, Klara Inc.
 */

#ifndef _ABD_IMPL_H
#define	_ABD_IMPL_H

#include <sys/abd.h>
#include <sys/abd_impl_os.h>
#include <sys/wmsum.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum abd_stats_op {
	ABDSTAT_INCR, /* Increase abdstat values */
	ABDSTAT_DECR  /* Decrease abdstat values */
} abd_stats_op_t;

/* forward declarations */
struct scatterlist;
struct page;
#if defined(__FreeBSD__) && defined(_KERNEL)
struct sf_buf;
#endif

struct abd_iter {
	/* public interface */
	union {
		/* for abd_iter_map()/abd_iter_unmap() */
		struct {
			/* addr corresponding to iter_pos */
			void		*iter_mapaddr;
			/* length of data valid at mapaddr */
			size_t		iter_mapsize;
		};
		/* for abd_iter_page() */
		struct {
			/* current page */
			struct page	*iter_page;
			/* offset of data in page */
			size_t		iter_page_doff;
			/* size of data in page */
			size_t		iter_page_dsize;
		};
	};

	/* private */
	abd_t		*iter_abd;	/* ABD being iterated through */
	size_t		iter_pos;
	size_t		iter_offset;	/* offset in current sg/abd_buf, */
					/* abd_offset included */
#if defined(__FreeBSD__) && defined(_KERNEL)
	struct sf_buf	*sf;		/* used to map in vm_page_t FreeBSD */
#else
	struct scatterlist *iter_sg;	/* current sg */
#endif
};

extern abd_t *abd_zero_scatter;

abd_t *abd_gang_get_offset(abd_t *, size_t *);
abd_t *abd_alloc_struct(size_t);
void abd_free_struct(abd_t *);
void abd_init_struct(abd_t *);

/*
 * OS specific functions
 */

abd_t *abd_alloc_struct_impl(size_t);
abd_t *abd_get_offset_scatter(abd_t *, abd_t *, size_t, size_t);
void abd_free_struct_impl(abd_t *);
void abd_alloc_chunks(abd_t *, size_t);
void abd_free_chunks(abd_t *);
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
void abd_iter_page(struct abd_iter *);

/*
 * Helper macros
 */
#define	ABDSTAT_INCR(stat, val) \
	wmsum_add(&abd_sums.stat, (val))
#define	ABDSTAT_BUMP(stat)	ABDSTAT_INCR(stat, 1)
#define	ABDSTAT_BUMPDOWN(stat)	ABDSTAT_INCR(stat, -1)

#define	ABD_SCATTER(abd)	((abd)->abd_u.abd_scatter)
#define	ABD_LINEAR_BUF(abd)	((abd)->abd_u.abd_linear.abd_buf)
#define	ABD_GANG(abd)		((abd)->abd_u.abd_gang)

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_IMPL_H */
