// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _LIBSPL_UMEM_H
#define	_LIBSPL_UMEM_H

/*
 * XXX: We should use the real portable umem library if it is detected
 * at configure time.  However, if the library is not available, we can
 * use a trivial malloc based implementation.  This obviously impacts
 * performance, but unless you are using a full userspace build of zpool for
 * something other than ztest, you are likely not going to notice or care.
 *
 * https://labs.omniti.com/trac/portableumem
 */
#include <sys/debug.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifdef  __cplusplus
extern "C" {
#endif

typedef void vmem_t;

/*
 * Flags for umem_alloc/umem_free
 */
#define	UMEM_DEFAULT		0x0000  /* normal -- may fail */
#define	UMEM_NOFAIL		0x0100  /* Never fails */

/*
 * Flags for umem_cache_create()
 */
#define	UMC_NODEBUG		0x00020000

#define	UMEM_CACHE_NAMELEN	31

typedef int umem_nofail_callback_t(void);
typedef int umem_constructor_t(void *, void *, int);
typedef void umem_destructor_t(void *, void *);
typedef void umem_reclaim_t(void *);

typedef struct umem_cache {
	char			cache_name[UMEM_CACHE_NAMELEN + 1];
	size_t			cache_bufsize;
	size_t			cache_align;
	umem_constructor_t	*cache_constructor;
	umem_destructor_t	*cache_destructor;
	umem_reclaim_t		*cache_reclaim;
	void			*cache_private;
	void			*cache_arena;
	int			cache_cflags;
} umem_cache_t;

/* Prototypes for functions to provide defaults for umem envvars */
const char *_umem_debug_init(void);
const char *_umem_options_init(void);
const char *_umem_logging_init(void);

__attribute__((malloc, alloc_size(1)))
static inline void *
umem_alloc(size_t size, int flags)
{
	void *ptr = NULL;

	do {
		ptr = malloc(size);
	} while (ptr == NULL && (flags & UMEM_NOFAIL));

	return (ptr);
}

__attribute__((malloc, alloc_size(1)))
static inline void *
umem_alloc_aligned(size_t size, size_t align, int flags)
{
	void *ptr = NULL;
	int rc = EINVAL;

	do {
		rc = posix_memalign(&ptr, align, size);
	} while (rc == ENOMEM && (flags & UMEM_NOFAIL));

	if (rc == EINVAL) {
		fprintf(stderr, "%s: invalid memory alignment (%zd)\n",
		    __func__, align);
		if (flags & UMEM_NOFAIL)
			abort();
		return (NULL);
	}

	return (ptr);
}

__attribute__((malloc, alloc_size(1)))
static inline void *
umem_zalloc(size_t size, int flags)
{
	void *ptr = NULL;

	ptr = umem_alloc(size, flags);
	if (ptr)
		memset(ptr, 0, size);

	return (ptr);
}

static inline void
umem_free(const void *ptr, size_t size __maybe_unused)
{
	free((void *)ptr);
}

/*
 * umem_free_aligned was added for supporting portability
 * with non-POSIX platforms that require a different free
 * to be used with aligned allocations.
 */
static inline void
umem_free_aligned(void *ptr, size_t size __maybe_unused)
{
#ifndef _WIN32
	free((void *)ptr);
#else
	_aligned_free(ptr);
#endif
}

static inline void
umem_nofail_callback(umem_nofail_callback_t *cb __maybe_unused)
{}

static inline umem_cache_t *
umem_cache_create(
    const char *name, size_t bufsize, size_t align,
    umem_constructor_t *constructor,
    umem_destructor_t *destructor,
    umem_reclaim_t *reclaim,
    void *priv, void *vmp, int cflags)
{
	umem_cache_t *cp;

	cp = (umem_cache_t *)umem_alloc(sizeof (umem_cache_t), UMEM_DEFAULT);
	if (cp) {
		strlcpy(cp->cache_name, name, UMEM_CACHE_NAMELEN);
		cp->cache_bufsize = bufsize;
		cp->cache_align = align;
		cp->cache_constructor = constructor;
		cp->cache_destructor = destructor;
		cp->cache_reclaim = reclaim;
		cp->cache_private = priv;
		cp->cache_arena = vmp;
		cp->cache_cflags = cflags;
	}

	return (cp);
}

static inline void
umem_cache_destroy(umem_cache_t *cp)
{
	umem_free(cp, sizeof (umem_cache_t));
}

__attribute__((malloc))
static inline void *
umem_cache_alloc(umem_cache_t *cp, int flags)
{
	void *ptr = NULL;

	if (cp->cache_align != 0)
		ptr = umem_alloc_aligned(
		    cp->cache_bufsize, cp->cache_align, flags);
	else
		ptr = umem_alloc(cp->cache_bufsize, flags);

	if (ptr && cp->cache_constructor)
		cp->cache_constructor(ptr, cp->cache_private, UMEM_DEFAULT);

	return (ptr);
}

static inline void
umem_cache_free(umem_cache_t *cp, void *ptr)
{
	if (cp->cache_destructor)
		cp->cache_destructor(ptr, cp->cache_private);

	if (cp->cache_align != 0)
		umem_free_aligned(ptr, cp->cache_bufsize);
	else
		umem_free(ptr, cp->cache_bufsize);
}

static inline void
umem_cache_reap_now(umem_cache_t *cp __maybe_unused)
{
}

#ifdef  __cplusplus
}
#endif

#endif
