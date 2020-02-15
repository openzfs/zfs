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

#ifndef	ZSTD_KFREEBSD_H
#define	ZSTD_KFREEBSD_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL
#include <sys/param.h>	/* size_t */
#if defined(__FreeBSD__)
#include <sys/systm.h>	/* memcpy, memset */
#elif defined(__linux__)
#include <linux/string.h> /* memcpy, memset */
#endif /* __FreeBSD__ */
#else /* !_KERNEL */
#include <stddef.h> /* size_t, ptrdiff_t */
#include <stdlib.h>
#include <string.h>
#endif /* _KERNEL */

#ifdef __cplusplus
}
#endif

#endif /* ZSTD_KFREEBSD_H */
