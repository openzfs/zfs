// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */
/*
 * Copyright (c) 2014, Joyent, Inc.
 * Copyright (c) 2017 by Delphix. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "libnvpair.h"

static int
nvjson_file_writer(void *context, const char *str)
{
	FILE *fd = context;
	int ret = fputs(str, fd);
	if (ret < 0)
		return (ret);
	return (0);
}

/*
 * When formatting a string for JSON output we must escape certain characters,
 * as described in RFC4627.  This applies to both member names and
 * DATA_TYPE_STRING values.
 *
 * This function will only operate correctly if the following conditions are
 * met:
 *
 *       1. The input String is encoded in the current locale.
 *
 *       2. The current locale includes the Basic Multilingual Plane (plane 0)
 *          as defined in the Unicode standard.
 *
 * The output will be entirely 7-bit ASCII (as a subset of UTF-8) with all
 * representable Unicode characters included in their escaped numeric form.
 */
static int
nvjson_singlebyte_str_handler(const char *str, nvjson_writer_t w, void *wctx)
{
#define	W(x)				\
	do {				\
		int ret = w(wctx, (x));	\
		if (ret != 0)		\
			return (ret);	\
	} while (0)

	char tmp[8];

	mbstate_t mbr = {0};
	wchar_t c;
	size_t sz;

	W("\"");
	while ((sz = mbrtowc(&c, str, MB_CUR_MAX, &mbr)) > 0) {
		if (sz == (size_t)-1 || sz == (size_t)-2) {
			/*
			 * We last read an invalid multibyte character sequence,
			 * so return an error.
			 */
			return (-1);
		}
		switch (c) {
		case '"':
			W("\\\"");
			break;
		case '\n':
			W("\\n");
			break;
		case '\r':
			W("\\r");
			break;
		case '\\':
			W("\\\\");
			break;
		case '\f':
			W("\\f");
			break;
		case '\t':
			W("\\t");
			break;
		case '\b':
			W("\\b");
			break;
		default:
			if ((c >= 0x00 && c <= 0x1f) ||
			    (c > 0x7f && c <= 0xffff)) {
				/*
				 * Render both Control Characters and Unicode
				 * characters in the Basic Multilingual Plane
				 * as JSON-escaped multibyte characters.
				 */
				snprintf(tmp, sizeof (tmp), "\\u%04x",
				    (int)(0xffff & c));
				W(tmp);
			} else if (c >= 0x20 && c <= 0x7f) {
				/*
				 * Render other 7-bit ASCII characters directly
				 * and drop other, unrepresentable characters.
				 */
				snprintf(tmp, sizeof (tmp), "%c",
				    (int)(0xff & c));
				W(tmp);
			}
			break;
		}
		str += sz;
	}

	W("\"");
	return (0);
}

/*
 * Dump a JSON-formatted representation of an nvlist to the provided FILE *.
 * This routine does not output any new-lines or additional whitespace other
 * than that contained in strings, nor does it call fflush(3C).
 */
int
nvlist_print_json(FILE *fp, nvlist_t *nvl)
{
	nvjson_t nvjson = {
		.buf = NULL,
		.size = 0,
		.writer = nvjson_file_writer,
		.writer_ctx = fp,
		.str_handler = nvjson_singlebyte_str_handler
	};
	return (nvlist_to_json(&nvjson, nvl));
}
