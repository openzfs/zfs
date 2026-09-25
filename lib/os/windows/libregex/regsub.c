/*	$NetBSD: regsub.c,v 1.3 2016/02/29 22:10:13 aymeric Exp $	*/

/*
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/param.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <regex.h>

struct str {
	char *s_ptr;
	size_t s_max;
	size_t s_len;
	int s_fixed;
};

#define	REINCR	64

static int
addspace(struct str *s, size_t len)
{
	void *v;

	if (s->s_max - s->s_len > len)
		return (0);

	if (s->s_fixed)
		return (-1);

	s->s_max += len + REINCR;

	v = realloc(s->s_ptr, s->s_max);
	if (v == NULL)
		return (-1);
	s->s_ptr = v;

	return (0);
}

static void
addchar(struct str *s, int c)
{
	if (addspace(s, 1) == -1)
		s->s_len++;
	else
		s->s_ptr[s->s_len++] = c;
	if (c == 0) {
		--s->s_len;
		s->s_ptr[s->s_max - 1] = c;
	}
}

static void
addnstr(struct str *s, const char *buf, size_t len)
{
	if (addspace(s, len) != -1)
		memcpy(s->s_ptr + s->s_len, buf, len);
	s->s_len += len;
}

static int
initstr(struct str *s, char *buf, size_t len)
{
	s->s_max = len;
	s->s_ptr = buf == NULL ? malloc(len) : buf;
	s->s_fixed = buf != NULL;
	s->s_len = 0;
	return (s->s_ptr == NULL ? -1 : 0);
}

static ssize_t
regsub1(char **buf, size_t len, const char *sub,
    const regmatch_t *rm, const char *str)
{
	ssize_t i;
	char c;
	struct str s;

	if (initstr(&s, *buf, len) == -1)
		return (-1);

	while ((c = *sub++) != '\0') {

		switch (c) {
		case '&':
			i = 0;
			break;
		case '\\':
			if (isdigit((unsigned char)*sub))
				i = *sub++ - '0';
			else
				i = -1;
			break;
		default:
			i = -1;
			break;
		}

		if (i == -1) {
			if (c == '\\' && (*sub == '\\' || *sub == '&'))
				c = *sub++;
			addchar(&s, c);
		} else if (rm[i].rm_so != -1 && rm[i].rm_eo != -1) {
			size_t l = (size_t)(rm[i].rm_eo - rm[i].rm_so);
			addnstr(&s, str + rm[i].rm_so, l);
		}
	}

	addchar(&s, '\0');
	if (!s.s_fixed) {
		if (s.s_len >= s.s_max) {
			free(s.s_ptr);
			return (-1);
		}
		*buf = s.s_ptr;
	}
	return (s.s_len);
}

ssize_t
regnsub(char *buf, size_t len, const char *sub, const regmatch_t *rm,
    const char *str)
{
	return (regsub1(&buf, len, sub, rm, str));
}

ssize_t
regasub(char **buf, const char *sub, const regmatch_t *rm, const char *str)
{
	*buf = NULL;
	return (regsub1(buf, REINCR, sub, rm, str));
}
