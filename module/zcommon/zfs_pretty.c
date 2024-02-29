/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2024, Klara Inc.
 */

#include <sys/fs/zfs.h>
#include <sys/types.h>
#include <sys/string.h>
#include <sys/debug.h>
#include "zfs_pretty.h"

typedef struct {
	const char	pb_bit;
	const char	pb_pair[2];
	const char	*pb_name;
} pretty_bit_t;

static const pretty_bit_t pretty_zio_flag_table[] = {
    { '.', "DA", "DONT_AGGREGATE" },
    { '.', "RP", "IO_REPAIR" },
    { '.', "SH", "SELF_HEAL" },
    { '.', "RS", "RESILVER" },
    { '.', "SC", "SCRUB" },
    { '.', "ST", "SCAN_THREAD" },
    { '.', "PH", "PHYSICAL" },
    { '.', "CF", "CANFAIL" },
    { '.', "SP", "SPECULATIVE" },
    { '.', "CW", "CONFIG_WRITER" },
    { '.', "DR", "DONT_RETRY" },
    { '.', "ND", "NODATA" },
    { '.', "ID", "INDUCE_DAMAGE" },
    { '.', "AL", "IO_ALLOCATING" },
    { '.', "RE", "IO_RETRY" },
    { '.', "PR", "PROBE" },
    { '.', "TH", "TRYHARD" },
    { '.', "OP", "OPTIONAL" },
    { '.', "DQ", "DONT_QUEUE" },
    { '.', "DP", "DONT_PROPAGATE" },
    { '.', "BY", "IO_BYPASS" },
    { '.', "RW", "IO_REWRITE" },
    { '.', "CM", "RAW_COMPRESS" },
    { '.', "EN", "RAW_ENCRYPT" },
    { '.', "GG", "GANG_CHILD" },
    { '.', "DD", "DDT_CHILD" },
    { '.', "GF", "GODFATHER" },
    { '.', "NP", "NOPWRITE" },
    { '.', "EX", "REEXECUTED" },
    { '.', "DG", "DELEGATED" },
};
static const size_t pretty_zio_flag_table_elems =
    sizeof (pretty_zio_flag_table) / sizeof (pretty_bit_t);

size_t
zfs_pretty_zio_flag_bits(uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = pretty_zio_flag_table_elems; b >= 0; b--) {
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		out[n++] =
		    (bits & mask) ? pretty_zio_flag_table[b].pb_bit : ' ';
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

size_t
zfs_pretty_zio_flag_pairs(uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = pretty_zio_flag_table_elems; b >= 0; b--) {
		ASSERT3U(n, <=, outlen);
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		if (bits & mask) {
			size_t len = (n > 0) ? 3 : 2;
			if (n > outlen-len)
				break;
			if (n > 0)
				out[n++] = '|';
			out[n++] = pretty_zio_flag_table[b].pb_pair[0];
			out[n++] = pretty_zio_flag_table[b].pb_pair[1];
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

size_t
zfs_pretty_zio_flag_str(uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = pretty_zio_flag_table_elems; b >= 0; b--) {
		ASSERT3U(n, <=, outlen);
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		if (bits & mask) {
			size_t len = strlen(pretty_zio_flag_table[b].pb_name);
			if (n > 0)
				len++;
			if (n > outlen-len)
				break;
			if (n > 0) {
				out[n++] = ' ';
				len--;
			}
			memcpy(&out[n], pretty_zio_flag_table[b].pb_name, len);
			n += len;
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

static const pretty_bit_t pretty_abd_flag_table[] = {
    { 'L', "LN", "LINEAR" },
    { 'O', "OW", "OWNER" },
    { 'M', "MT", "META" },
    { 'Z', "MZ", "MULTI_ZONE" },
    { 'C', "MC", "MULTI_CHUNK" },
    { 'P', "LP", "LINEAR_PAGE" },
    { 'G', "GG", "GANG" },
    { 'F', "GF", "GANG_FREE" },
    { 'Z', "ZR", "ZEROS" },
    { 'A', "AL", "ALLOCD" },
};
static const size_t pretty_abd_flag_table_elems =
    sizeof (pretty_abd_flag_table) / sizeof (pretty_bit_t);

size_t
zfs_pretty_abd_flag_bits(uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = pretty_abd_flag_table_elems; b >= 0; b--) {
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		out[n++] =
		    (bits & mask) ? pretty_abd_flag_table[b].pb_bit : ' ';
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

size_t
zfs_pretty_abd_flag_pairs(uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = pretty_abd_flag_table_elems; b >= 0; b--) {
		ASSERT3U(n, <=, outlen);
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		if (bits & mask) {
			size_t len = (n > 0) ? 3 : 2;
			if (n > outlen-len)
				break;
			if (n > 0)
				out[n++] = '|';
			out[n++] = pretty_abd_flag_table[b].pb_pair[0];
			out[n++] = pretty_abd_flag_table[b].pb_pair[1];
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

size_t
zfs_pretty_abd_flag_str(uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = 0; b <= pretty_abd_flag_table_elems; b++) {
		ASSERT3U(n, <=, outlen);
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		if (bits & mask) {
			size_t len = strlen(pretty_abd_flag_table[b].pb_name);
			if (n > 0)
				len++;
			if (n > outlen-len)
				break;
			if (n > 0) {
				out[n++] = ' ';
				len--;
			}
			memcpy(&out[n], pretty_abd_flag_table[b].pb_name, len);
			n += len;
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}
