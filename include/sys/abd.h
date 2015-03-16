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
 */

/*
 * ABD - ARC buffer data
 * ABD is an abstract data structure for ARC. There are two types of ABD:
 * linear for metadata and scatter for data.
 * Their type is determined by the lowest bit of abd_t pointer.
 * The public API will automatically determine the type
 */

#ifndef _ABD_H
#define	_ABD_H

#include <sys/zfs_context.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	ARC_BUF_DATA_MAGIC 0xa7cb0fda

#if defined(ZFS_DEBUG) && !defined(_KERNEL)
#define	DEBUG_ABD
#endif

typedef struct arc_buf_data {
#ifdef DEBUG_ABD
	char		pad[PAGE_SIZE];	/* debug, coredumps when accessed */
#endif
	uint32_t	abd_magic;	/* ARC_BUF_DATA_MAGIC */
	uint32_t	abd_flags;
	size_t		abd_size;	/* buffer size, excluding offset */
	size_t		abd_offset;	/* offset in the first segment */
	int		abd_nents;	/* num of sgl entries */
	union {
		struct scatterlist *abd_sgl;
		void *abd_buf;
	};
	uint64_t __abd_sgl[0];
} abd_t;

#define	ABD_F_SCATTER	(0x0)
#define	ABD_F_LINEAR	(0x1)
#define	ABD_F_OWNER	(0x2)

/*
 * Convert an linear ABD to normal buffer
 */
#define	ABD_TO_BUF(abd)					\
(							\
{							\
	ASSERT((abd)->abd_magic == ARC_BUF_DATA_MAGIC);	\
	ASSERT_ABD_LINEAR(abd);				\
	abd->abd_buf;					\
}							\
)

#define	ABD_IS_SCATTER(abd)	(!((abd)->abd_flags & ABD_F_LINEAR))
#define	ABD_IS_LINEAR(abd)	(!ABD_IS_SCATTER(abd))
#define	ASSERT_ABD_SCATTER(abd)	ASSERT(ABD_IS_SCATTER(abd))
#define	ASSERT_ABD_LINEAR(abd)	ASSERT(ABD_IS_LINEAR(abd))

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

/*
 * Borrow a linear buffer for an ABD
 * Will allocate if ABD is scatter
 */
#define	abd_borrow_buf(a, n)			\
(						\
{						\
	void *___b;				\
	if (ABD_IS_LINEAR(a)) {			\
		___b = ABD_TO_BUF(a);		\
	} else {				\
		___b = zio_buf_alloc(n);	\
	}					\
	___b;					\
}						\
)

/*
 * Borrow a linear buffer for an ABD
 * Will allocate and copy if ABD is scatter
 */
#define	abd_borrow_buf_copy(a, n)		\
(						\
{						\
	void *___b = abd_borrow_buf(a, n);	\
	if (!ABD_IS_LINEAR(a))			\
		abd_copy_to_buf(___b, a, n);	\
	___b;					\
}						\
)

/*
 * Return the borrowed linear buffer
 */
#define	abd_return_buf(a, b, n)			\
do {						\
	if (ABD_IS_LINEAR(a))			\
		ASSERT((b) == ABD_TO_BUF(a));	\
	else					\
		zio_buf_free(b, n);		\
} while (0)

/*
 * Copy back to ABD and return the borrowed linear buffer
 */
#define	abd_return_buf_copy(a, b, n)		\
do {						\
	if (!ABD_IS_LINEAR(a))			\
		abd_copy_from_buf(a, b, n);	\
	abd_return_buf(a, b, n);		\
} while (0)

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
