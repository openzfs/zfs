// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the Common
 * Development and Distribution License ("CDDL"), version 1.0. You may only use
 * this file in accordance with the terms of version 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this source. A
 * copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2026 by Garth Snyder. All rights reserved.
 */

#ifndef _ZSTREAM_CHAIN_H
#define	_ZSTREAM_CHAIN_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stddef.h>
#include <stdint.h>
#include <sys/zfs_ioctl.h>

/*
 * A chain is a linear series of steps that process packets of data. It's
 * designed to modularize common functionality, reduce code duplication, and
 * separate processing structure from implementation.
 *
 * Some terms:
 *
 * **STEP** - A chain_step_t struct that represents a packet-processing
 * module and any arguments or context that it needs. Chain modules
 * generally define a function named serial_* that produces a chain_step_t
 * that can be incorporated directly into a chain.
 *
 * **CHAIN** - An array of chain_step_t's. It's just data, so you can create
 * the array however you like. But normally you'd just declare the whole
 * thing:
 *
 *	zstream_chain_t dump_chain = {
 *		serial_read_stream(infile),
 *		serial_validate_fletcher4(),
 *		serial_byteswap(BS_INCOMING),
 *		serial_validate_records(),
 *		serial_dump_records(&dump_args),
 *		serial_null_output(),
 *		chain_terminator()
 *	}
 *
 * Or more succinctly:
 *
 *	zstream_chain_t dump_chain = {
 *		STANDARD_INPUT_STACK(infile),
 *		serial_dump_records(&dump_args),
 *		NULL_OUTPUT_STACK()
 *	};
 *
 * Chains must be terminated by a step of type CS_TERMINATE.
 *
 * **ITEMS** - The data packets that flow through a chain. Each step accepts
 * items of one size and emits items of another size, which may be smaller,
 * larger, or the same size. Items will generally be structs that start with
 * a drr_packet_t (defined in zstream_io.h) and may include additional
 * module-specific fields.
 *
 * **PROCESSING FUNCTION** - Each step names a processing function that does
 * the actual work of transforming an input buffer into an output buffer.
 * The transformation happens in place, in a single buffer provided by the
 * chain.
 *
 * The processing function should return a disposition_t, normally D_OK. A
 * function can return D_DROP to remove an item from the stream entirely. It
 * can also return D_EOF to indicate that no more data will be forthcoming.
 * However, only the first step in the chain should ever return D_EOF.
 *
 * Functions are called with a NULL packet pointer when the end of the
 * stream passes by them.
 *
 * **CONTEXT** - An arbitrary void * that the chain passes along to the
 * processing function as an argument.
 *
 * **CHAIN ATTRIBUTES** - A global set of flags available to all steps.
 */

#define	CA_BYTESWAPPED			(1ULL << 0)	/* ca_attrs */
#define	CA_BIG_ENDIAN_INPUT		(1ULL << 1)
#define	CA_LITTLE_ENDIAN_INPUT		(1ULL << 2)

#define	CA_VERBOSE			(1ULL << 0)	/* ca_command_opts */
#define	CA_VERY_VERBOSE			(1ULL << 1)
#define	CA_DUMP_DATA			(1ULL << 2)
#define	CA_IGNORE_CKSUMS		(1ULL << 3)
#define	CA_DO_NOT_VALIDATE		(1ULL << 4)
#define	CA_FORBID_DEDUP			(1ULL << 5)
#define	CA_REQUIRE_DEDUP		(1ULL << 6)
#define	CA_REQUIRE_NATIVE_ENDIAN	(1ULL << 7)
#define	CA_BYTESWAP_ON_OUTPUT		(1ULL << 8)
#define	CA_BIG_ENDIAN_OUT		(1ULL << 9)
#define	CA_LITTLE_ENDIAN_OUT		(1ULL << 10)
#define	CA_OPPOSITE_ENDIAN_OUT		(1ULL << 11)

#define	OPTION_ENABLED(option) (!!(chain_attrs->ca_command_opts & (option)))
#define	STREAM_HAS_FEATURE(feat) (!!(chain_attrs->ca_feature_flags & (feat)))
#define	ATTR_IS_SET(attr) (!!(chain_attrs->ca_attrs & (attr)))

#define	ENABLE_OPTION(attrs, opt) ((attrs)->ca_command_opts |= (opt))
#define	SET_ATTR(attr) (chain_attrs->ca_attrs |= (attr))

typedef struct {
	uint64_t	rs_num_records;
	uint64_t	rs_total_header_bytes;
	uint64_t	rs_total_payload_bytes;
} record_stats_t;

/*
 * Chain attribute flags that describe the stream. Statistics are maintained
 * by the zstream_io modules.
 */
typedef struct {
	uint64_t	ca_feature_flags;	/* From drr_versioninfo */
	uint64_t	ca_attrs;		/* Discovered attributes */
	uint64_t	ca_command_opts;	/* Command line options */
	record_stats_t	ca_totals_in;
	record_stats_t	ca_totals_out;
	record_stats_t	ca_stats_in[DRR_NUMTYPES];
	record_stats_t	ca_stats_out[DRR_NUMTYPES];
} chain_attrs_t;

typedef enum { CS_SERIAL, CS_TERMINATE } step_type_t;
typedef enum { D_OK, D_EOF, D_DROP } disposition_t;

typedef disposition_t
zc_serial_process_f(void *item, void *context);

typedef struct chain_step
{
	step_type_t	cs_type;
	size_t		cs_in_size;
	size_t		cs_out_size;
	void		*cs_context;
	struct {
		zc_serial_process_f	*process;
	} cs_serial;
} chain_step_t;

typedef chain_step_t zstream_chain_t[];

/*
 * Chain attributes accessible to any step on the chain. In theory this
 * could cause a race condition between reading and setting, but all
 * attributes are typically set by the time the first record has been read.
 * Ergo, nobody else will be executing while that first chain_read() runs.
 */
extern chain_attrs_t *chain_attrs;

/*
 * Execute a chain. Returns once execution is complete. You can pass NULL
 * for the attrs if you're not interested in preserving them after the chain
 * has run.
 */
void
zstream_chain_exec(zstream_chain_t chain, chain_attrs_t *attrs);

chain_step_t
serial_null_step(void);

chain_step_t
chain_terminator(void);

#ifdef __cplusplus
}
#endif

#endif	/* _ZSTREAM_CHAIN_H */
