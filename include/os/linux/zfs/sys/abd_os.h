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
 */

#ifndef _ABD_OS_H
#define	_ABD_OS_H

#ifdef __cplusplus
extern "C" {
#endif

struct abd;

struct abd_scatter {
	uint_t		abd_offset;
	uint_t		abd_nents;
	struct scatterlist *abd_sgl;
};

struct abd_linear {
	void		*abd_buf;
	struct scatterlist *abd_sgl; /* for LINEAR_PAGE */
};

typedef int abd_iter_page_func_t(struct page *, size_t, size_t, void *);
int abd_iterate_page_func(struct abd *, size_t, size_t, abd_iter_page_func_t *,
    void *);

__attribute__((malloc))
struct abd *abd_alloc_from_pages(struct page **, unsigned long, uint64_t);

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
