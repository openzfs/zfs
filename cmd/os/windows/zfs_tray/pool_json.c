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

#include <windows.h>
#include <stdio.h>
#include <string.h>
// #include <libzfs.h>

#include "pool_json.h"

#define	JSMN_STATIC
#include "jsmn.h"

// Returns number of pool names filled, up to maxnames.
// Each name goes into names[i] (UTF-16). `json` is UTF-8 from the service.
int
ParsePoolNames(const char *json, int json_len, wchar_t names[][64],
    int maxnames)
{
	jsmn_parser p;
	jsmn_init(&p);
	jsmntok_t tok[512];
	int r = jsmn_parse(&p, json, json_len, tok, (int)_countof(tok));
	if (r < 0)
		return (0);

	// Find "pools" -> array
	int pools_idx = -1, pools_arr_idx = -1;
	for (int i = 1; i < r; ++i) {
		if (tok[i].type == JSMN_STRING) {
			int klen = tok[i].end - tok[i].start;
			const char *k = json + tok[i].start;
			if (klen == 5 && memcmp(k, "pools", 5) == 0) {
				int val = i+1;
				if (val < r && tok[val].type == JSMN_ARRAY) {
					pools_idx = i;
					pools_arr_idx = val;
					break;
				}
			}
		}
	}
	if (pools_arr_idx < 0)
		return (0);

	int count = 0;
	int elems = tok[pools_arr_idx].size;
	int idx = pools_arr_idx + 1; // first element token
	for (int e = 0; e < elems && count < maxnames && idx < r; ++e) {
		// Accept either string (names-only API) or object with "name"
		if (tok[idx].type == JSMN_STRING) {
			int len = tok[idx].end - tok[idx].start;
			int w = MultiByteToWideChar(CP_UTF8, 0,
			    json + tok[idx].start, len, names[count], 63);
			names[count][w < 0 ? 0:w] = 0;
			count++; idx++;
		} else if (tok[idx].type == JSMN_OBJECT) {
			// scan object for "name"
			int objsz = tok[idx].size;
			int obj = idx;
			idx++;
			for (int j = 0; j < objsz && idx+1 < r; ++j) {
				jsmntok_t *k = &tok[idx], *v = &tok[idx+1];
				if (k->type == JSMN_STRING) {
					int klen = k->end - k->start;
					const char *ks = json + k->start;
					if (klen == 4 &&
					    memcmp(ks, "name", 4) == 0 &&
					    v->type == JSMN_STRING) {
						int len = v->end - v->start;
						int w = MultiByteToWideChar(
						    CP_UTF8, 0,
						    json + v->start, len,
						    names[count], 63);
						names[count][w < 0 ? 0 : w] = 0;
						count++;
					}
				}
				// advance over value token (and its subtree)
				// jsmn tokens are flat;
				// skip subtree by walking until we leave v
				int end = v->end; idx += 2;
				while (idx < r && tok[idx].start < end) idx++;
			}
			// Ensure we’re at the next sibling of the object
			while (idx < r && tok[idx].start < tok[obj].end) idx++;
		} else {
			// skip unknown element
			int end = tok[idx].end; idx++;
			while (idx < r && tok[idx].start < end) idx++;
		}
	}
	return (count);
}

static uint64_t
parse_u64_from_utf8(const char *s, int len)
{
	char tmp[40];
	int n = len < 39 ? len : 39;
	memcpy(tmp, s, n);
	tmp[n] = 0;
	return (_strtoui64(tmp, NULL, 10));
}

// Returns TRUE on success. Expects UTF-8 JSON from OP_GET_STATUS (one pool).
BOOL
GetPoolSummaryFromStatusJSON(const char *json, int json_len, PoolSummary *out)
{
	if (!json || json_len <= 0 || !out)
		return (FALSE);

	ZeroMemory(out, sizeof (*out));

	jsmn_parser p;
	jsmn_init(&p);
	jsmntok_t tok[1024];
	int r = jsmn_parse(&p, json, json_len, tok, (int)_countof(tok));
	if (r < 0)
		return (FALSE);

	// locate pools array
	int pools_arr = -1;
	for (int i = 1; i < r - 1; ++i) {
		if (tok[i].type == JSMN_STRING &&
		    tok[i + 1].type == JSMN_ARRAY) {
			int klen = tok[i].end - tok[i].start;
			if (klen == 5 && memcmp(json + tok[i].start,
			    "pools", 5) == 0) {
				pools_arr = i + 1;
				break;
			}
		}
	}
	if (pools_arr < 0 || tok[pools_arr].size < 1)
		return (FALSE);

	// first element should be the pool object
	int idx = pools_arr + 1;
	if (tok[idx].type != JSMN_OBJECT)
		return (FALSE);
	int obj_end = tok[idx].end; idx++;

	int name_v = -1, guid_v = -1, health_v = -1, cap_v = -1,
	    alloc_v = -1, free_v = -1;

	while (idx + 1 < r && tok[idx].start < obj_end) {
		jsmntok_t *k = &tok[idx], *v = &tok[idx + 1];
		if (k->type == JSMN_STRING) {
			int klen = k->end - k->start;
			const char *ks = json + k->start;
			if (klen == 4 && memcmp(ks, "name", 4) == 0)
				name_v = idx + 1;
			else if (klen == 4 && memcmp(ks, "guid", 4) == 0)
				guid_v = idx + 1;
			else if (klen == 6 && memcmp(ks, "health", 6) == 0)
				health_v = idx + 1;
			else if (klen == 12 &&
			    memcmp(ks, "capacity_pct", 12) == 0)
				cap_v = idx + 1;
			else if (klen == 5 && memcmp(ks, "alloc", 5) == 0)
				alloc_v = idx + 1;
			else if (klen == 4 && memcmp(ks, "free", 4) == 0)
				free_v = idx + 1;
		}
		int vend = v->end; idx += 2;
		while (idx < r && tok[idx].start < vend) idx++;
	}

	if (name_v > 0 && tok[name_v].type == JSMN_STRING) {
		int len = tok[name_v].end - tok[name_v].start;
		MultiByteToWideChar(CP_UTF8, 0, json + tok[name_v].start,
		    len, out->name, 63);
	}
	if (guid_v > 0 && (tok[guid_v].type == JSMN_STRING ||
	    tok[guid_v].type == JSMN_PRIMITIVE)) {
		int len = tok[guid_v].end - tok[guid_v].start;
		out->guid = parse_u64_from_utf8(json + tok[guid_v].start,
		    len);
	}
	if (health_v > 0 && tok[health_v].type == JSMN_STRING) {
		int len = tok[health_v].end - tok[health_v].start;
		MultiByteToWideChar(CP_UTF8, 0, json + tok[health_v].start,
		    len, out->health, 31);
	}
	if (cap_v > 0 && tok[cap_v].type == JSMN_STRING) {
		int len = tok[cap_v].end - tok[cap_v].start;
		MultiByteToWideChar(CP_UTF8, 0, json + tok[cap_v].start,
		    len, out->capacity_pct, 15);
	}
	if (alloc_v > 0 && tok[alloc_v].type == JSMN_STRING) {
		int len = tok[alloc_v].end - tok[alloc_v].start;
		MultiByteToWideChar(CP_UTF8, 0, json + tok[alloc_v].start,
		    len, out->alloc, 63);
	}
	if (free_v > 0 && tok[free_v].type == JSMN_STRING) {
		int len = tok[free_v].end - tok[free_v].start;
		MultiByteToWideChar(CP_UTF8, 0, json + tok[free_v].start,
		    len, out->freeb, 63);
	}
	return (TRUE);
}
