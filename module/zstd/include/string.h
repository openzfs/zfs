/*
 * BSD 3-Clause New License (https://spdx.org/licenses/BSD-3-Clause.html)
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its
 * contributors may be used to endorse or promote products derived from this
 * software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Copyright (c) 2014-2019, Allan Jude
 * Copyright (c) 2020, Brian Behlendorf
 * Copyright (c) 2020, Michael Niewöhner
 */

#ifndef	_ZSTD_STRING_H
#define	_ZSTD_STRING_H

#ifdef __cplusplus
extern "C" {
#endif

#ifdef _KERNEL

#if defined(__FreeBSD__)
#include <sys/systm.h>    /* memcpy, memset */
#elif defined(__linux__)
#include <linux/string.h> /* memcpy, memset */
#else
#error "Unsupported platform"
#endif

#else /* !_KERNEL */
#include_next <string.h>
#endif /* _KERNEL */

/*
 * GCC/Clang doesn't know that the kernel's memcpy (etc)
 * are standards-compliant so it can't be sure that it
 * can inline its own optimized copies without altering
 * behavior.  This is particularly significant for
 * zstd which, for example, expects a fast inline memcpy
 * in its inner loops.  Explicitly using the __builtin
 * versions permits the compiler to inline where it
 * considers this profitable, falling back to the
 * kernel's memcpy (etc) otherwise.
 */
#if defined(_KERNEL) && defined(__GNUC__) && __GNUC__ >= 4
#define	ZSTD_memcpy(d, s, l) __builtin_memcpy((d), (s), (l))
#define	ZSTD_memmove(d, s, l) __builtin_memmove((d), (s), (l))
#define	ZSTD_memset(p, v, l) __builtin_memset((p), (v), (l))
#else
#define	ZSTD_memcpy(d, s, l) memcpy((d), (s), (l))
#define	ZSTD_memmove(d, s, l) memmove((d), (s), (l))
#define	ZSTD_memset(p, v, l) memset((p), (v), (l))
#endif

#ifdef __cplusplus
}
#endif

#endif /* _ZSTD_STRING_H */
