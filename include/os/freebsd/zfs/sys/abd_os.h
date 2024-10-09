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

#ifndef _ABD_OS_H
#define	_ABD_OS_H

#ifdef _KERNEL
#include <sys/vm.h>
#include <vm/vm_page.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct abd;

struct abd_scatter {
	uint_t		abd_offset;
	void		*abd_chunks[1]; /* actually variable-length */
};

struct abd_linear {
	void		*abd_buf;
#if defined(_KERNEL)
	struct sf_buf 	*sf; /* for LINEAR_PAGE FreeBSD */
#endif
};

#ifdef _KERNEL
__attribute__((malloc))
struct abd *abd_alloc_from_pages(vm_page_t *, unsigned long, uint64_t);
#endif

#ifdef __cplusplus
}
#endif

#endif	/* _ABD_H */
