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

#ifndef _ABD_H
#define	_ABD_H

#include <sys/isa_defs.h>
#include <sys/debug.h>
#include <sys/zfs_refcount.h>
#include <sys/uio.h>

#ifdef __cplusplus
extern "C" {
#endif

struct abd; /* forward declaration */
typedef struct abd abd_t;

typedef int abd_iter_func_t(void *buf, size_t len, void *priv);
typedef int abd_iter_func2_t(void *bufa, void *bufb, size_t len, void *priv);

extern int zfs_abd_scatter_enabled;

/*
 * Allocations and deallocations
 */

abd_t *abd_alloc(size_t, boolean_t);
abd_t *abd_alloc_linear(size_t, boolean_t);
abd_t *abd_alloc_gang_abd(void);
abd_t *abd_alloc_for_io(size_t, boolean_t);
abd_t *abd_alloc_sametype(abd_t *, size_t);
void abd_gang_add(abd_t *, abd_t *, boolean_t);
void abd_free(abd_t *);
void abd_put(abd_t *);
abd_t *abd_get_offset(abd_t *, size_t);
abd_t *abd_get_offset_size(abd_t *, size_t, size_t);
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
uint_t abd_get_size(abd_t *);

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
boolean_t abd_is_linear(abd_t *);
boolean_t abd_is_gang(abd_t *);
boolean_t abd_is_linear_page(abd_t *);

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
unsigned long abd_nr_pages_off(abd_t *, unsigned int, size_t);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
