/*
 * Copyright (c) 2014-2019 Allan Jude <allanjude@freebsd.org>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#ifndef _ZSTD_FREEBSD_STDLIB_H_
#define	_ZSTD_FREEBSD_STDLIB_H_

#ifdef _KERNEL
#include <sys/param.h>  /* size_t */
#endif

#if defined(__FreeBSD__) && defined(_KERNEL)

#include <sys/malloc.h>
MALLOC_DECLARE(M_ZSTD);
#undef malloc
#define	malloc(x)	(malloc)((x), M_ZSTD, M_WAITOK)
#define	free(x)		(free)((x), M_ZSTD)
#define	calloc(a, b)	(mallocarray)((a), (b), M_ZSTD, M_WAITOK | M_ZERO)

#elif defined(__linux__) && defined(_KERNEL)

#undef	GCC_VERSION
extern void *spl_kmem_alloc(size_t sz, int fl, const char *func, int line);
extern void *spl_kmem_zalloc(size_t sz, int fl, const char *func, int line);
extern void spl_kmem_free(const void *ptr, size_t sz);
#define	KM_SLEEP	0x0000  /* can block for memory; success guaranteed */
#define	KM_NOSLEEP	0x0001  /* cannot block for memory; may fail */
#define	KM_ZERO		0x1000  /* zero the allocation */
#undef malloc
#define	malloc(sz)	spl_kmem_alloc((sz), KM_SLEEP, __func__, __LINE__)
#define	free(ptr)	spl_kmem_free((ptr), 0)
#define	calloc(n, sz)	\
    spl_kmem_zalloc((n) * (sz), KM_SLEEP, __func__, __LINE__)

#endif /* _KERNEL */

#endif /* _ZSTD_FREEBSD_STDLIB_H_ */
