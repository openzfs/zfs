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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2011 Nexenta Systems, Inc.  All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2012, Joyent, Inc. All rights reserved.
 * Copyright (c) 2026, TrueNAS.
 */

#ifndef _SYS_KMEM_H
#define	_SYS_KMEM_H

#include <sys/types.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define	KM_NOSLEEP		0x0000
#define	KM_SLEEP		0x0100
#define	KM_PUSHPAGE		KM_SLEEP

__attribute__((malloc, alloc_size(1)))
extern void *kmem_alloc(size_t size, int flags);

__attribute__((malloc, alloc_size(1)))
extern void *kmem_zalloc(size_t size, int flags);

extern void kmem_free(const void *ptr, size_t size);

__attribute__((malloc, alloc_size(1)))
extern void *kmem_alloc_aligned(size_t size, size_t align, int flags);

extern void kmem_free_aligned(void *ptr, size_t size);

extern char *kmem_strdup(const char *str);
extern void kmem_strfree(char *str);

#define	kmem_debugging()	0
#define	POINTER_INVALIDATE(_pp)		/* nothing */
#define	POINTER_IS_VALID(_p)	0

extern char *kmem_vasprintf(const char *fmt, va_list adx);
extern char *kmem_asprintf(const char *fmt, ...);
extern int kmem_scnprintf(char *restrict str, size_t size,
    const char *restrict fmt, ...);

typedef int fstrans_cookie_t;

extern fstrans_cookie_t spl_fstrans_mark(void);
extern void spl_fstrans_unmark(fstrans_cookie_t);

#ifdef  __cplusplus
}
#endif

#endif	/* _SYS_KMEM_H */
