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
#include <sys/sysmacros.h>
#include <sys/string.h>
#include <sys/debug.h>
#include "zfs_valstr.h"

/*
 * Each bit in a bitfield has three possible string representations:
 * - single char
 * - two-char pair
 * - full name
 */
typedef struct {
	const char	vb_bit;
	const char	vb_pair[2];
	const char	*vb_name;
} valstr_bit_t;

/*
 * Emits a character for each bit in `bits`, up to the number of elements
 * in the table. Set bits get the character in vb_bit, clear bits get a
 * space. This results in all strings having the same width, for easier
 * visual comparison.
 */
static size_t
valstr_bitfield_bits(const valstr_bit_t *table, const size_t nelems,
    uint64_t bits, char *out, size_t outlen)
{
	ASSERT(out);
	size_t n = 0;
	for (int b = 0; b < nelems; b++) {
		if (n == outlen)
			break;
		uint64_t mask = (1ULL << b);
		out[n++] = (bits & mask) ? table[b].vb_bit : ' ';
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

/*
 * Emits a two-char pair for each bit set in `bits`, taken from vb_pair, and
 * separated by a `|` character. This gives a concise representation of the
 * whole value.
 */
static size_t
valstr_bitfield_pairs(const valstr_bit_t *table, const size_t nelems,
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
			size_t len = (n > 0) ? 3 : 2;
			if (n > outlen-len)
				break;
			if (n > 0)
				out[n++] = '|';
			out[n++] = table[b].vb_pair[0];
			out[n++] = table[b].vb_pair[1];
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

/*
 * Emits the full name for each bit set in `bits`, taken from vb_name, and
 * separated by a space. This unambiguously shows the entire set of bits, but
 * can get very long.
 */
static size_t
valstr_bitfield_str(const valstr_bit_t *table, const size_t nelems,
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
			size_t len = strlen(table[b].vb_name);
			if (n > 0)
				len++;
			if (n > outlen-len)
				break;
			if (n > 0) {
				out[n++] = ' ';
				len--;
			}
			memcpy(&out[n], table[b].vb_name, len);
			n += len;
		}
	}
	if (n < outlen)
		out[n++] = '\0';
	return (n);
}

/*
 * Emits the name of the given enum value in the table.
 */
static size_t
valstr_enum_str(const char **table, const size_t nelems,
    int v, char *out, size_t outlen)
{
	ASSERT(out);
	ASSERT3U(v, <, nelems);
	if (v >= nelems)
		return (0);
	return (MIN(strlcpy(out, table[v], outlen), outlen));
}

/*
 * These macros create the string tables for the given name, and implement
 * the public functions described in zfs_valstr.h.
 */
#define	_VALSTR_BITFIELD_IMPL(name, ...)				\
static const valstr_bit_t valstr_ ## name ## _table[] = { __VA_ARGS__ };\
size_t									\
zfs_valstr_ ## name ## _bits(uint64_t bits, char *out, size_t outlen)	\
{									\
	return (valstr_bitfield_bits(valstr_ ## name ## _table,		\
	    ARRAY_SIZE(valstr_ ## name ## _table), bits, out, outlen));	\
}									\
									\
size_t									\
zfs_valstr_ ## name ## _pairs(uint64_t bits, char *out, size_t outlen)	\
{									\
	return (valstr_bitfield_pairs(valstr_ ## name ## _table,	\
	    ARRAY_SIZE(valstr_ ## name ## _table), bits, out, outlen));	\
}									\
									\
size_t									\
zfs_valstr_ ## name(uint64_t bits, char *out, size_t outlen)		\
{									\
	return (valstr_bitfield_str(valstr_ ## name ## _table,		\
	    ARRAY_SIZE(valstr_ ## name ## _table), bits, out, outlen));	\
}									\

#define	_VALSTR_ENUM_IMPL(name, ...)					\
static const char *valstr_ ## name ## _table[] = { __VA_ARGS__ };	\
size_t									\
zfs_valstr_ ## name(int v, char *out, size_t outlen)			\
{									\
	return (valstr_enum_str(valstr_ ## name ## _table,		\
	    ARRAY_SIZE(valstr_ ## name ## _table), v, out, outlen));	\
}									\


/* String tables */

/* ZIO flags: zio_flag_t, typically zio->io_flags */
/* BEGIN CSTYLED */
_VALSTR_BITFIELD_IMPL(zio_flag,
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
	{ '?', "??", "[UNUSED 11]" },
	{ '.', "ND", "NODATA" },
	{ '.', "ID", "INDUCE_DAMAGE" },
	{ '.', "AL", "IO_ALLOCATING" },
	{ '.', "RE", "IO_RETRY" },
	{ '.', "PR", "PROBE" },
	{ '.', "TH", "TRYHARD" },
	{ '.', "OP", "OPTIONAL" },
	{ '.', "RD", "DIO_READ" },
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
	{ '.', "DC", "DIO_CHKSUM_ERR" },
)
/* END CSTYLED */

/*
 * ZIO pipeline stage(s): enum zio_stage, typically zio->io_stage or
 *                        zio->io_pipeline.
 */
/* BEGIN CSTYLED */
_VALSTR_BITFIELD_IMPL(zio_stage,
	{ 'O', "O ", "OPEN" },
	{ 'I', "RI", "READ_BP_INIT" },
	{ 'I', "WI", "WRITE_BP_INIT" },
	{ 'I', "FI", "FREE_BP_INIT" },
	{ 'A', "IA", "ISSUE_ASYNC" },
	{ 'W', "WC", "WRITE_COMPRESS" },
	{ 'E', "EN", "ENCRYPT" },
	{ 'C', "CG", "CHECKSUM_GENERATE" },
	{ 'N', "NW", "NOP_WRITE" },
	{ 'B', "BF", "BRT_FREE" },
	{ 'd', "dS", "DDT_READ_START" },
	{ 'd', "dD", "DDT_READ_DONE" },
	{ 'd', "dW", "DDT_WRITE" },
	{ 'd', "dF", "DDT_FREE" },
	{ 'G', "GA", "GANG_ASSEMBLE" },
	{ 'G', "GI", "GANG_ISSUE" },
	{ 'D', "DT", "DVA_THROTTLE" },
	{ 'D', "DA", "DVA_ALLOCATE" },
	{ 'D', "DF", "DVA_FREE" },
	{ 'D', "DC", "DVA_CLAIM" },
	{ 'R', "R ", "READY" },
	{ 'V', "VS", "VDEV_IO_START" },
	{ 'V', "VD", "VDEV_IO_DONE" },
	{ 'V', "VA", "VDEV_IO_ASSESS" },
	{ 'C', "CV", "CHECKSUM_VERIFY" },
	{ 'C', "DC", "DIO_CHECKSUM_VERIFY" },
	{ 'X', "X ", "DONE" },
)
/* END CSTYLED */

/* ZIO priority: zio_priority_t, typically zio->io_priority */
/* BEGIN CSTYLED */
_VALSTR_ENUM_IMPL(zio_priority,
	"SYNC_READ",
	"SYNC_WRITE",
	"ASYNC_READ",
	"ASYNC_WRITE",
	"SCRUB",
	"REMOVAL",
	"INITIALIZING",
	"TRIM",
	"REBUILD",
	"[NUM_QUEUEABLE]",
	"NOW",
)
/* END CSTYLED */

#undef _VALSTR_BITFIELD_IMPL
#undef _VALSTR_ENUM_IMPL
