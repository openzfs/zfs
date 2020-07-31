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

#ifndef _SPL_SEARCH_H
#define	_SPL_SEARCH_H
#include <sys/types.h>

typedef enum {
	FIND,
	ENTER
} ACTION;

typedef struct entry {
	char *key;
	void *data;
} ENTRY;


/* Implement me for zstream decompress */
inline static int
hcreate(size_t n)
{
	return (0);
}

inline static void
hdestroy(void)
{
}

static inline ENTRY *
hsearch(ENTRY entry, ACTION action)
{
	return (NULL);
}

#endif
