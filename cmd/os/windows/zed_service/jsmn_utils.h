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
