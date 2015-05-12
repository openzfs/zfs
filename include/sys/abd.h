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
 * Copyright (c) 2015 by Chunwei Chen. All rights reserved.
 */

/*
 * ABD - ARC buffer data
 * ABD is an abstract data structure for ARC. There are two types of ABD:
 * linear for metadata and scatter for data.
 * The public API will automatically determine the type.
 */

#ifndef _ABD_H
#define	_ABD_H

#include <sys/zfs_context.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	ARC_BUF_DATA_MAGIC 0xa7cb0fda


typedef struct arc_buf_data {
	uint32_t	abd_magic;	/* ARC_BUF_DATA_MAGIC */
	uint32_t	abd_flags;
	size_t		abd_size;	/* buffer size, excluding offset */
	size_t		abd_offset;	/* offset in the first segment */
	int		abd_nents;	/* num of sgl entries */
	union {
		struct scatterlist *abd_sgl;
		void *abd_buf;
	};
} abd_t;

#define	ABD_F_SCATTER	(0)		/* abd is scatter */
#define	ABD_F_LINEAR	(1)		/* abd is linear */
#define	ABD_F_OWNER	(1<<1)		/* abd owns the buffer */
#define	ABD_F_HIGHMEM	(1<<2)		/* abd uses highmem */
#define	ABD_F_SG_CHAIN	(1<<3)		/* scatterlist is chained */

#define	ABD_IS_SCATTER(abd)	(!((abd)->abd_flags & ABD_F_LINEAR))
#define	ABD_IS_LINEAR(abd)	(!ABD_IS_SCATTER(abd))
#define	ASSERT_ABD_SCATTER(abd)	ASSERT(ABD_IS_SCATTER(abd))
#define	ASSERT_ABD_LINEAR(abd)	ASSERT(ABD_IS_LINEAR(abd))

/*
 * Convert an linear ABD to normal buffer
 */
static inline void *
abd_to_buf(abd_t *abd)
{
	ASSERT(abd->abd_magic == ARC_BUF_DATA_MAGIC);
	ASSERT_ABD_LINEAR(abd);
	return (abd->abd_buf);
}
#define	ABD_TO_BUF(abd)		abd_to_buf(abd)

void abd_init(void);
void abd_fini(void);

/*
 * Allocations and deallocations
 */
abd_t *abd_alloc_scatter(size_t);
abd_t *abd_alloc_linear(size_t);
void abd_free(abd_t *, size_t);
abd_t *abd_get_offset(abd_t *, size_t);
abd_t *abd_get_from_buf(void *, size_t);
void abd_put(abd_t *);

/*
 * ABD operations
 */
void abd_iterate_rfunc(abd_t *, size_t,
    int (*)(const void *, uint64_t, void *), void *);
void abd_iterate_wfunc(abd_t *, size_t,
    int (*)(void *, uint64_t, void *), void *);
void abd_iterate_func2(abd_t *, abd_t *, size_t, size_t,
    int (*)(void *, void *, uint64_t, uint64_t, void *), void *);
void abd_copy_off(abd_t *, abd_t *, size_t, size_t, size_t);
void abd_copy_from_buf_off(abd_t *, const void *, size_t, size_t);
void abd_copy_to_buf_off(void *, abd_t *, size_t, size_t);
int abd_cmp(abd_t *, abd_t *, size_t);
int abd_cmp_buf_off(abd_t *, const void *, size_t, size_t);
void abd_zero_off(abd_t *, size_t, size_t);
#ifdef _KERNEL
int abd_copy_to_user_off(void __user *, abd_t *, size_t, size_t);
int abd_copy_from_user_off(abd_t *, const void __user *, size_t, size_t);
int abd_uiomove_off(abd_t *, size_t, enum uio_rw, uio_t *, size_t);
int abd_uiocopy_off(abd_t *, size_t, enum uio_rw, uio_t *, size_t *,
    size_t);
unsigned int abd_scatter_bio_map_off(struct bio *, abd_t *, unsigned int,
    size_t);
unsigned long abd_bio_nr_pages_off(abd_t *, unsigned int, size_t);

#define	abd_bio_map_off(bio, abd, size, off)				\
(									\
{									\
	unsigned int ___ret;						\
	if (ABD_IS_LINEAR(abd))						\
		___ret = bio_map(bio, ABD_TO_BUF(abd) + (off), size);	\
	else								\
		___ret = abd_scatter_bio_map_off(bio, abd, size, off);	\
	___ret;								\
}									\
)
#endif	/* _KERNEL */

/* forward declaration for abd_borrow_buf, etc. */
void *zio_buf_alloc(size_t size);
void zio_buf_free(void *buf, size_t size);

/*
 * Borrow a linear buffer for an ABD
 * Will allocate if ABD is scatter
 */
static inline void*
abd_borrow_buf(abd_t *abd, size_t size)
{
	if (!abd)
		return (NULL);
	if (ABD_IS_LINEAR(abd))
		return (ABD_TO_BUF(abd));
	return (zio_buf_alloc(size));
}

/*
 * Borrow a linear buffer for an ABD
 * Will allocate and copy if ABD is scatter
 */
static inline void *
abd_borrow_buf_copy(abd_t *abd, size_t size)
{
	void *buf = abd_borrow_buf(abd, size);
	if (buf && !ABD_IS_LINEAR(abd))
		abd_copy_to_buf_off(buf, abd, size, 0);
	return (buf);
}

/*
 * Return the borrowed linear buffer
 */
static inline void
abd_return_buf(abd_t *abd, void *buf, size_t size)
{
	if (buf) {
		if (ABD_IS_LINEAR(abd))
			ASSERT(buf == ABD_TO_BUF(abd));
		else
			zio_buf_free(buf, size);
	}
}

/*
 * Copy back to ABD and return the borrowed linear buffer
 */
static inline void
abd_return_buf_copy(abd_t *abd, void *buf, size_t size)
{
	if (buf && !ABD_IS_LINEAR(abd))
		abd_copy_from_buf_off(abd, buf, size, 0);
	abd_return_buf(abd, buf, size);
}

/*
 * Wrappers for zero off functions
 */
#define	abd_copy(dabd, sabd, size) \
	abd_copy_off(dabd, sabd, size, 0, 0)

#define	abd_copy_from_buf(abd, buf, size) \
	abd_copy_from_buf_off(abd, buf, size, 0)

#define	abd_copy_to_buf(buf, abd, size) \
	abd_copy_to_buf_off(buf, abd, size, 0)

#define	abd_cmp_buf(abd, buf, size) \
	abd_cmp_buf_off(abd, buf, size, 0)

#define	abd_zero(abd, size) \
	abd_zero_off(abd, size, 0)

#ifdef _KERNEL
#define	abd_copy_to_user(buf, abd, size) \
	abd_copy_to_user_off(buf, abd, size, 0)

#define	abd_copy_from_user(abd, buf, size) \
	abd_copy_from_user_off(abd, buf, size, 0)

#define	abd_uiomove(abd, n, rw, uio) \
	abd_uiomove_off(abd, n, rw, uio, 0)

#define	abd_uiocopy(abd, n, rw, uio, c) \
	abd_uiocopy_off(abd, n, rw, uio, c, 0)
#endif	/* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
