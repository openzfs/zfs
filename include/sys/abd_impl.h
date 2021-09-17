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
#include <sys/wmsum.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum abd_stats_op {
	ABDSTAT_INCR, /* Increase abdstat values */
	ABDSTAT_DECR  /* Decrease abdstat values */
} abd_stats_op_t;

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
abd_t *abd_alloc_struct(size_t);
void abd_free_struct(abd_t *);

/*
 * OS specific functions
 */

abd_t *abd_alloc_struct_impl(size_t);
abd_t *abd_get_offset_scatter(abd_t *, abd_t *, size_t, size_t);
void abd_free_struct_impl(abd_t *);
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
#define	ABDSTAT_INCR(stat, val) \
	wmsum_add(&abd_sums.stat, (val))
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
