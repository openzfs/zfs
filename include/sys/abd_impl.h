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
size_t abd_iter_size(struct abd_iter *aiter);
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

/*
 * Chunk iterators.
 *
 * This is a new type of ABD iterator that iterates over data chunks. The idea
 * is that since ABDs are effectively a wrapper over a set of memory regions,
 * an iterator that yields data chunks can be smaller and simpler.
 *
 * The iterator object abd_chunk_t can be thought of as a pointer to a chunk
 * within an array of chunks. There are three main functions involved with
 * setting up and using an iterator:
 *
 * - abd_chunk_t ch = abd_chunk_start(abd_t *abd, size_t off, size_t size)
 *
 *   Create a new iterator over the given ABD, starting at off bytes, for size
 *   bytes. If off and size would fall outside of the ABD, the returned
 *   iterator will be unusable (already "done").
 *
 * - void abd_chunk_advance(abd_chunk_t *ch)
 *
 *   Move the iterator to the next chunk. If there is no next chunk, the
 *   iterator is changed to the "done" state and is no longer useable.
 *
 * - boolean_t abd_chunk_done(abd_chunk_t *ch)
 *
 *   If true, the iterator is pointing to a valid chunk, and the underlying
 *   memory can be accessed with the access functions. If false, the iterator
 *   is exhausted and no longer useable.
 *
 * Together, these allow an ABD to be processed in a for loop:
 *
 *   for (abd_chunk_t ch = abd_chunk_start(abd, off, size);
 *       !abd_chunk_done(&ch); abd_chunk_advance(&ch))
 *
 * With a valid chunk iterator, the following functions can be used to work
 * with the underlying data or memory:
 *
 * - size_t abd_chunk_size(abd_chunk_t *ch)
 *
 *   The number of data bytes within the chunk.
 *
 * - void *data = abd_chunk_map(abd_chunk_t *ch)
 *
 *   Map the memory within the chunk into the address space, and return a
 *   pointer to the start of the data.
 *
 * - void abd_chunk_unmap(abd_chunk_t *ch)
 *
 *   Unmap previously-mapped chunk memory. For convenience, if there is nothing
 *   mapped, nothing happens.
 */

/* XXX temp exposing old iterator control functions for use in chunk iters */
abd_t *abd_init_abd_iter(abd_t *abd, struct abd_iter *aiter, size_t off);
abd_t *abd_advance_abd_iter(abd_t *abd, abd_t *cabd, struct abd_iter *aiter,
    size_t len);

typedef struct {
	abd_t		*ch_abd;	/* ABD being iterated over */

	size_t		ch_coff;	/* chunk offset within ABD */
	size_t		ch_csize;	/* size of chunk within ABD */
	size_t		ch_doff;	/* data offset within chunk */
	size_t		ch_dsize;	/* size of data remaining in iter */

	struct abd_iter	ch_iter;	/* XXX old-style iterator */
	abd_t		*ch_cabd;	/* XXX child abd, for gang iter */
} abd_chunk_t;

static inline abd_chunk_t
abd_chunk_start(abd_t *abd, size_t off, size_t size)
{
	abd_chunk_t ch = {
		.ch_abd = abd,
	};

	if (size == 0 || (off + size > abd_get_size(abd))) {
		ch.ch_dsize = 0;
		ch.ch_doff = 0;
		return (ch);
	}

	abd_verify(abd);
	ASSERT3U(off + size, <=, abd->abd_size);

	/* start of data, size of data */
	ch.ch_doff = off;
	ch.ch_dsize = size;

	ch.ch_cabd = abd_init_abd_iter(abd, &ch.ch_iter, 0);

	/* size of first chunk */
	ch.ch_coff = 0;
	ch.ch_csize = abd_iter_size(&ch.ch_iter);

	/* roll chunks forward until we reach the one with the data start */
	while (ch.ch_doff >= ch.ch_csize) {
		ch.ch_doff -= ch.ch_csize;
		ch.ch_coff += ch.ch_csize;

		ch.ch_cabd = abd_advance_abd_iter(ch.ch_abd, ch.ch_cabd,
		    &ch.ch_iter, ch.ch_csize);

		ch.ch_csize = abd_iter_size(&ch.ch_iter);
	}

	return (ch);
}

static inline void
abd_chunk_advance(abd_chunk_t *ch)
{
	ASSERT3U(ch->ch_dsize, >, 0);

	/* consume data up to the end of the chunk */
	ch->ch_dsize -= MIN(ch->ch_dsize, ch->ch_csize - ch->ch_doff);

	/* next data will be at the start of the next chunk */
	ch->ch_doff = 0;

	/* no more data, so return */
	if (ch->ch_dsize == 0)
		return;

	ch->ch_cabd = abd_advance_abd_iter(ch->ch_abd, ch->ch_cabd,
	    &ch->ch_iter, ch->ch_csize);

	ch->ch_coff += ch->ch_csize;
	ch->ch_csize = abd_iter_size(&ch->ch_iter);
}

static inline boolean_t
abd_chunk_done(abd_chunk_t *ch)
{
	return (ch->ch_dsize == 0);
}

static inline size_t
abd_chunk_size(abd_chunk_t *ch)
{
	return (MIN(ch->ch_dsize, ch->ch_csize - ch->ch_doff));
}

static inline void *
abd_chunk_map(abd_chunk_t *ch) {
	abd_iter_map(&ch->ch_iter);
	ASSERT3U(ch->ch_iter.iter_mapsize, ==, ch->ch_csize);
	return (ch->ch_iter.iter_mapaddr + ch->ch_doff);
}

static inline void
abd_chunk_unmap(abd_chunk_t *ch) {
	if (ch->ch_iter.iter_mapaddr == NULL)
		return;
	abd_iter_unmap(&ch->ch_iter);
}

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_IMPL_H */
