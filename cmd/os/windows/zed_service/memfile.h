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
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <wosix.h>

typedef struct memfile_s {
    char *buf;
    size_t len;
    size_t cap;
    FILE *fp;
} memfile_t;

static int
memfile_write(void *cookie, const char *data, int n)
{
	memfile_t *m = (memfile_t *)cookie;
	if (n <= 0)
		return (0);
	size_t need = m->len + (size_t)n;
	if (need > m->cap) {
		size_t cap = m->cap ? m->cap : 4096;
		while (cap < need) cap *= 2;
		char *np = (char *)HeapReAlloc(GetProcessHeap(), 0, m->buf,
		    cap);
		if (!np) {
			np = (char *)HeapAlloc(GetProcessHeap(), 0, cap);
			if (!np)
				return (-1);
			if (m->buf) {
				memcpy(np, m->buf, m->len);
				HeapFree(GetProcessHeap(), 0, m->buf);
			}
		}
		m->buf = np; m->cap = cap;
	}
	memcpy(m->buf + m->len, data, (size_t)n);
	m->len += (size_t)n;
	return (n);
}

static int
memfile_close(void *cookie)
{
	memfile_t *m = (memfile_t *)cookie;
	if (m->buf) {
		if (m->len == m->cap) {
			char *np = (char *)HeapReAlloc(GetProcessHeap(),
			    0, m->buf, m->cap + 1);
			if (np) {
				m->buf = np;
				m->cap += 1;
			}
		}
		m->buf[m->len] = 0; // NUL terminate for convenience
	}
	return (0);
}

static FILE *
memfile_open(memfile_t *m)
{
	ZeroMemory(m, sizeof (*m));
	// wfunopen(cookie, read, write, seek, close)
	m->fp = funopen(m, NULL, memfile_write, NULL, memfile_close);
	return (m->fp);
}

static char *
memfile_take(memfile_t *m, size_t *len)
{
	if (len)
		*len = m->len;
	char *p = m->buf;
	m->buf = NULL;
	m->len = m->cap = 0;
	return (p); // HeapAlloc’d; caller HeapFree()
}
