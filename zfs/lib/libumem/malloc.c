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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/sysmacros.h>
#include "umem_base.h"
#include "misc.h"

/*
 * malloc_data_t is an 8-byte structure which is located "before" the pointer
 * returned from {m,c,re}alloc and memalign.  The first four bytes give
 * information about the buffer, and the second four bytes are a status byte.
 *
 * See umem_impl.h for the various magic numbers used, and the size
 * encode/decode macros.
 *
 * The 'size' of the buffer includes the tags.  That is, we encode the
 * argument to umem_alloc(), not the argument to malloc().
 */

typedef struct malloc_data {
	uint32_t malloc_size;
	uint32_t malloc_stat; /* = UMEM_MALLOC_ENCODE(state, malloc_size) */
} malloc_data_t;

void *
malloc(size_t size_arg)
{
#ifdef _LP64
	uint32_t high_size = 0;
#endif
	size_t size;

	malloc_data_t *ret;
	size = size_arg + sizeof (malloc_data_t);

#ifdef _LP64
	if (size > UMEM_SECOND_ALIGN) {
		size += sizeof (malloc_data_t);
		high_size = (size >> 32);
	}
#endif
	if (size < size_arg) {
		errno = ENOMEM;			/* overflow */
		return (NULL);
	}
	ret = (malloc_data_t *)_umem_alloc(size, UMEM_DEFAULT);
	if (ret == NULL) {
		if (size <= UMEM_MAXBUF)
			errno = EAGAIN;
		else
			errno = ENOMEM;
		return (NULL);
#ifdef _LP64
	} else if (high_size > 0) {
		uint32_t low_size = (uint32_t)size;

		/*
		 * uses different magic numbers to make it harder to
		 * undetectably corrupt
		 */
		ret->malloc_size = high_size;
		ret->malloc_stat = UMEM_MALLOC_ENCODE(MALLOC_MAGIC, high_size);
		ret++;

		ret->malloc_size = low_size;
		ret->malloc_stat = UMEM_MALLOC_ENCODE(MALLOC_OVERSIZE_MAGIC,
		    low_size);
		ret++;
	} else if (size > UMEM_SECOND_ALIGN) {
		uint32_t low_size = (uint32_t)size;

		ret++; /* leave the first 8 bytes alone */

		ret->malloc_size = low_size;
		ret->malloc_stat = UMEM_MALLOC_ENCODE(MALLOC_SECOND_MAGIC,
		    low_size);
		ret++;
#endif
	} else {
		ret->malloc_size = size;
		ret->malloc_stat = UMEM_MALLOC_ENCODE(MALLOC_MAGIC, size);
		ret++;
	}
	return ((void *)ret);
}

void *
calloc(size_t nelem, size_t elsize)
{
	size_t size = nelem * elsize;
	void *retval;

	if (nelem > 0 && elsize > 0 && size/nelem != elsize) {
		errno = ENOMEM;				/* overflow */
		return (NULL);
	}

	retval = malloc(size);
	if (retval == NULL)
		return (NULL);

	(void) memset(retval, 0, size);
	return (retval);
}

/*
 * memalign uses vmem_xalloc to do its work.
 *
 * in 64-bit, the memaligned buffer always has two tags.  This simplifies the
 * code.
 */

void *
memalign(size_t align, size_t size_arg)
{
	size_t size;
	uintptr_t phase;

	void *buf;
	malloc_data_t *ret;

	size_t overhead;

	if (size_arg == 0 || align == 0 || (align & (align - 1)) != 0) {
		errno = EINVAL;
		return (NULL);
	}

	/*
	 * if malloc provides the required alignment, use it.
	 */
	if (align <= UMEM_ALIGN ||
	    (align <= UMEM_SECOND_ALIGN && size_arg >= UMEM_SECOND_ALIGN))
		return (malloc(size_arg));

#ifdef _LP64
	overhead = 2 * sizeof (malloc_data_t);
#else
	overhead = sizeof (malloc_data_t);
#endif

	ASSERT(overhead <= align);

	size = size_arg + overhead;
	phase = align - overhead;

	if (umem_memalign_arena == NULL && umem_init() == 0) {
		errno = ENOMEM;
		return (NULL);
	}

	if (size < size_arg) {
		errno = ENOMEM;			/* overflow */
		return (NULL);
	}

	buf = vmem_xalloc(umem_memalign_arena, size, align, phase,
	    0, NULL, NULL, VM_NOSLEEP);

	if (buf == NULL) {
		if ((size_arg + align) <= UMEM_MAXBUF)
			errno = EAGAIN;
		else
			errno = ENOMEM;

		return (NULL);
	}

	ret = (malloc_data_t *)buf;
	{
		uint32_t low_size = (uint32_t)size;

#ifdef _LP64
		uint32_t high_size = (uint32_t)(size >> 32);

		ret->malloc_size = high_size;
		ret->malloc_stat = UMEM_MALLOC_ENCODE(MEMALIGN_MAGIC,
		    high_size);
		ret++;
#endif

		ret->malloc_size = low_size;
		ret->malloc_stat = UMEM_MALLOC_ENCODE(MEMALIGN_MAGIC, low_size);
		ret++;
	}

	ASSERT(P2PHASE((uintptr_t)ret, align) == 0);
	ASSERT((void *)((uintptr_t)ret - overhead) == buf);

	return ((void *)ret);
}

void *
valloc(size_t size)
{
	return (memalign(pagesize, size));
}

/*
 * process_free:
 *
 * Pulls information out of a buffer pointer, and optionally free it.
 * This is used by free() and realloc() to process buffers.
 *
 * On failure, calls umem_err_recoverable() with an appropriate message
 * On success, returns the data size through *data_size_arg, if (!is_free).
 *
 * Preserves errno, since free()'s semantics require it.
 */

static int
process_free(void *buf_arg,
    int do_free,		/* free the buffer, or just get its size? */
    size_t *data_size_arg)	/* output: bytes of data in buf_arg */
{
	malloc_data_t *buf;

	void *base;
	size_t size;
	size_t data_size;

	const char *message;
	int old_errno = errno;

	buf = (malloc_data_t *)buf_arg;

	buf--;
	size = buf->malloc_size;

	switch (UMEM_MALLOC_DECODE(buf->malloc_stat, size)) {

	case MALLOC_MAGIC:
		base = (void *)buf;
		data_size = size - sizeof (malloc_data_t);

		if (do_free)
			buf->malloc_stat = UMEM_FREE_PATTERN_32;

		goto process_malloc;

#ifdef _LP64
	case MALLOC_SECOND_MAGIC:
		base = (void *)(buf - 1);
		data_size = size - 2 * sizeof (malloc_data_t);

		if (do_free)
			buf->malloc_stat = UMEM_FREE_PATTERN_32;

		goto process_malloc;

	case MALLOC_OVERSIZE_MAGIC: {
		size_t high_size;

		buf--;
		high_size = buf->malloc_size;

		if (UMEM_MALLOC_DECODE(buf->malloc_stat, high_size) !=
		    MALLOC_MAGIC) {
			message = "invalid or corrupted buffer";
			break;
		}

		size += high_size << 32;

		base = (void *)buf;
		data_size = size - 2 * sizeof (malloc_data_t);

		if (do_free) {
			buf->malloc_stat = UMEM_FREE_PATTERN_32;
			(buf + 1)->malloc_stat = UMEM_FREE_PATTERN_32;
		}

		goto process_malloc;
	}
#endif

	case MEMALIGN_MAGIC: {
		size_t overhead = sizeof (malloc_data_t);

#ifdef _LP64
		size_t high_size;

		overhead += sizeof (malloc_data_t);

		buf--;
		high_size = buf->malloc_size;

		if (UMEM_MALLOC_DECODE(buf->malloc_stat, high_size) !=
		    MEMALIGN_MAGIC) {
			message = "invalid or corrupted buffer";
			break;
		}
		size += high_size << 32;

		/*
		 * destroy the main tag's malloc_stat
		 */
		if (do_free)
			(buf + 1)->malloc_stat = UMEM_FREE_PATTERN_32;
#endif

		base = (void *)buf;
		data_size = size - overhead;

		if (do_free)
			buf->malloc_stat = UMEM_FREE_PATTERN_32;

		goto process_memalign;
	}
	default:
		if (buf->malloc_stat == UMEM_FREE_PATTERN_32)
			message = "double-free or invalid buffer";
		else
			message = "invalid or corrupted buffer";
		break;
	}

	umem_err_recoverable("%s(%p): %s\n",
	    do_free? "free" : "realloc", buf_arg, message);

	errno = old_errno;
	return (0);

process_malloc:
	if (do_free)
		_umem_free(base, size);
	else
		*data_size_arg = data_size;

	errno = old_errno;
	return (1);

process_memalign:
	if (do_free)
		vmem_xfree(umem_memalign_arena, base, size);
	else
		*data_size_arg = data_size;

	errno = old_errno;
	return (1);
}

void
free(void *buf)
{
	if (buf == NULL)
		return;

	/*
	 * Process buf, freeing it if it is not corrupt.
	 */
	(void) process_free(buf, 1, NULL);
}

void *
realloc(void *buf_arg, size_t newsize)
{
	size_t oldsize;
	void *buf;

	if (buf_arg == NULL)
		return (malloc(newsize));

	if (newsize == 0) {
		free(buf_arg);
		return (NULL);
	}

	/*
	 * get the old data size without freeing the buffer
	 */
	if (process_free(buf_arg, 0, &oldsize) == 0) {
		errno = EINVAL;
		return (NULL);
	}

	if (newsize == oldsize)		/* size didn't change */
		return (buf_arg);

	buf = malloc(newsize);
	if (buf == NULL)
		return (NULL);

	(void) memcpy(buf, buf_arg, MIN(newsize, oldsize));
	free(buf_arg);
	return (buf);
}
