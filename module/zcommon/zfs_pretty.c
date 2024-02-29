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

static size_t
zfs_pretty_bits(const pretty_bit_t *table, const size_t nelems,
    uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = nelems; b >= 0; b--) {
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		out[n++] =
		    (bits & mask) ? table[b].pb_bit : ' ';
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

static size_t
zfs_pretty_pairs(const pretty_bit_t *table, const size_t nelems,
    uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = nelems; b >= 0; b--) {
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
			out[n++] = table[b].pb_pair[0];
			out[n++] = table[b].pb_pair[1];
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

static size_t
zfs_pretty_str(const pretty_bit_t *table, const size_t nelems,
    uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = 0; b < nelems; b++) {
		ASSERT3U(n, <=, outlen);
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		if (bits & mask) {
			size_t len = strlen(table[b].pb_name);
			if (n > 0)
				len++;
			if (n > outlen-len)
				break;
			if (n > 0) {
				out[n++] = ' ';
				len--;
			}
			memcpy(&out[n], table[b].pb_name, len);
			n += len;
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

#define	_PRETTY_BIT_IMPL(name, ...)					\
static const pretty_bit_t pretty_ ## name ## _table[] = { __VA_ARGS__ };\
size_t									\
zfs_pretty_ ## name ## _bits(uint64_t bits, char *out, size_t outlen)	\
{									\
	return (zfs_pretty_bits(pretty_ ## name ## _table,		\
	    sizeof (pretty_ ## name ## _table) / sizeof (pretty_bit_t),	\
	    bits, out, outlen));					\
}									\
									\
size_t									\
zfs_pretty_ ## name ## _pairs(uint64_t bits, char *out, size_t outlen)	\
{									\
	return (zfs_pretty_pairs(pretty_ ## name ## _table,		\
	    sizeof (pretty_ ## name ## _table) / sizeof (pretty_bit_t),	\
	    bits, out, outlen));					\
}									\
									\
size_t									\
zfs_pretty_ ## name ## _str(uint64_t bits, char *out, size_t outlen)	\
{									\
	return (zfs_pretty_str(pretty_ ## name ## _table,		\
	    sizeof (pretty_ ## name ## _table) / sizeof (pretty_bit_t),	\
	    bits, out, outlen));					\
}									\

/* BEGIN CSTYLED */
_PRETTY_BIT_IMPL(zio_flag,
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
)
/* END CSTYLED */

/* BEGIN CSTYLED */
_PRETTY_BIT_IMPL(abd_flag,
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
)
/* END CSTYLED */

/* BEGIN CSTYLED */
_PRETTY_BIT_IMPL(arc_flag,
    { '.', "WT", "WAIT" },
    { '.', "NW", "NOWAIT" },
    { '.', "PF", "PREFETCH" },
    { '.', "1C", "CACHED" },
    { '.', "2C", "L2CACHE" },
    { '.', "UC", "UNCACHED" },
    { '.', "PP", "PRESCIENT_PREFETCH" },
    { '.', "HT", "IN_HASH_TABLE" },
    { '.', "IO", "IO_IN_PROGRESS" },
    { '.', "ER", "IO_ERROR" },
    { '.', "ID", "INDIRECT" },
    { '.', "AS", "PRIO_ASYNC_READ" },
    { '.', "2W", "L2_WRITING" },
    { '.', "2E", "L2_EVICTED" },
    { '.', "2A", "L2_WRITE_HEAD" },
    { '.', "PR", "PROTECTED" },
    { '.', "NA", "NOAUTH" },
    { '.', "MD", "BUFC_METADATA" },
    { '.', "1H", "HAS_L1HDR" },
    { '.', "2H", "HAS_L2HDR" },
    { '.', "CA", "COMPRESSED_ARC" },
    { '.', "SD", "SHARED_DATA" },
    { '.', "CO", "CACHED_ONLY" },
    { '.', "NB", "NO_BUF" },
    { '.', "C0", "COMPRESS_0" },
    { '.', "C1", "COMPRESS_1" },
    { '.', "C2", "COMPRESS_2" },
    { '.', "C3", "COMPRESS_3" },
    { '.', "C4", "COMPRESS_4" },
    { '.', "C5", "COMPRESS_5" },
    { '.', "C6", "COMPRESS_6" },
)
/* END CSTYLED */

#undef _PRETTY_BIT_IMPL
