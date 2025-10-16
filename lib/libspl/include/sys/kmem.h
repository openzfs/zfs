// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 */

#ifndef _SYS_KMEM_H
#define	_SYS_KMEM_H

#include <umem.h>
#include <sys/types.h>

/*
 * Kernel memory
 */
#define	KM_SLEEP		UMEM_NOFAIL
#define	KM_PUSHPAGE		KM_SLEEP
#define	KM_NOSLEEP		UMEM_DEFAULT
#define	KM_NORMALPRI		0	/* not needed with UMEM_DEFAULT */
#define	KMC_NODEBUG		UMC_NODEBUG
#define	KMC_KVMEM		0x0
#define	KMC_RECLAIMABLE		0x0
#define	kmem_alloc(_s, _f)	umem_alloc(_s, _f)
#define	kmem_zalloc(_s, _f)	umem_zalloc(_s, _f)
#define	kmem_free(_b, _s)	umem_free(_b, _s)
#define	vmem_alloc(_s, _f)	kmem_alloc(_s, _f)
#define	vmem_zalloc(_s, _f)	kmem_zalloc(_s, _f)
#define	vmem_free(_b, _s)	kmem_free(_b, _s)
#define	kmem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i) \
	umem_cache_create(_a, _b, _c, _d, _e, _f, _g, _h, _i)
#define	kmem_cache_destroy(_c)	umem_cache_destroy(_c)
#define	kmem_cache_alloc(_c, _f) umem_cache_alloc(_c, _f)
#define	kmem_cache_free(_c, _b)	umem_cache_free(_c, _b)
#define	kmem_debugging()	0
#define	kmem_cache_reap_now(_c)	umem_cache_reap_now(_c);
extern int kmem_cache_reap_active(void);
#define	kmem_cache_set_move(_c, _cb)	/* nothing */
#define	POINTER_INVALIDATE(_pp)		/* nothing */
#define	POINTER_IS_VALID(_p)	0

extern char *kmem_vasprintf(const char *fmt, va_list adx);
extern char *kmem_asprintf(const char *fmt, ...);
#define	kmem_strfree(str) kmem_free((str), strlen(str) + 1)
#define	kmem_strdup(s)  strdup(s)

extern int kmem_scnprintf(char *restrict str, size_t size,
    const char *restrict fmt, ...);

typedef umem_cache_t kmem_cache_t;

typedef enum kmem_cbrc {
	KMEM_CBRC_YES,
	KMEM_CBRC_NO,
	KMEM_CBRC_LATER,
	KMEM_CBRC_DONT_NEED,
	KMEM_CBRC_DONT_KNOW
} kmem_cbrc_t;

typedef int fstrans_cookie_t;

extern fstrans_cookie_t spl_fstrans_mark(void);
extern void spl_fstrans_unmark(fstrans_cookie_t);

#endif	/* _SYS_KMEM_H */
