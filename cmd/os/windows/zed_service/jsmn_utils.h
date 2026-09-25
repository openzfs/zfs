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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#pragma once
#include <stddef.h>
#include <string.h>
#include "jsmn.h"

/*
 * Compare a token (string) with a C string literal.
 * Returns 1 if equal, 0 otherwise.
 */
static inline int
jsmn_eq(const char *js, const jsmntok_t *tok, const char *s)
{
	if (!js || !tok || !s)
		return (0);
	if (tok->type != JSMN_STRING)
		return (0);
	size_t tlen = (size_t)(tok->end - tok->start);
	size_t slen = strlen(s);
	if (tlen != slen)
		return (0);
	return (memcmp(js + tok->start, s, tlen) == 0);
}

static void
jsmn_copy_string(const char *json, const jsmntok_t *t,
    char *dst, size_t dstsz)
{
	if (!dst || dstsz == 0)
		return;
	size_t tl = (size_t)(t->end - t->start);
	size_t n = (tl < dstsz - 1) ? tl : (dstsz - 1);
	if (n > 0) memcpy(dst, json + t->start, n);
	dst[n] = '\0';
}

/* Optional: strdup the token (caller frees). Returns NULL on alloc failure. */
static inline char *
jsmn_tostrdup(const char *js, const jsmntok_t *tok)
{
	if (!js || !tok || tok->type != JSMN_STRING)
		return (NULL);
	size_t tlen = (size_t)(tok->end - tok->start);
	char *s = (char *)malloc(tlen + 1);
	if (!s)
		return (NULL);
	memcpy(s, js + tok->start, tlen);
	s[tlen] = '\0';
	return (s);
}
