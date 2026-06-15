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

#include <arpa/inet.h>
#include <err.h>
#include <errno.h>
#include <libzutil.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/byteorder.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/zfs_ioctl.h>
#include <time.h>
#include <unistd.h>

#include "zstream_chain.h"
#include "zstream_modules.h"
#include "zstream_util.h"

/*
 * Init only the filename; chain functions will prepare the FILE *
 */
typedef struct {
	const char	*ic_filename;
	FILE		*ic_fp;
	boolean_t	ic_for_reading;
	off_t		ic_offset;
} io_context_t;

typedef struct {
	const char	*cc_name;
	double		cc_last_sec;
	double		cc_period_sec;
	uint64_t	cc_last_bytes;
} checkpoint_context_t;

static io_context_t io_contexts[MAX_IO_STREAMS];
static int next_io_context = 0;

static checkpoint_context_t checkpoint_contexts[MAX_IO_STREAMS];
static int next_checkpoint_context = 0;

/*
 * Run from within chain execution to initialize I/O. A NULL filename
 * indicates stdin or stdout.
 */
static void
open_file(io_context_t *context)
{
	if (context->ic_filename) {
		context->ic_fp = fopen(context->ic_filename,
		    context->ic_for_reading ? "rb" : "wb+");
		if (!context->ic_fp) {
			perror(context->ic_filename);
			exit(1);
		}
	} else if (context->ic_for_reading && isatty(STDIN_FILENO)) {
		errx(1, "stream cannot be read from a terminal. "
		    "Name a file or take input from a pipe.");
	} else if (context->ic_for_reading) {
		context->ic_fp = stdin;
	} else if (isatty(STDOUT_FILENO)) {
		errx(1, "stream cannot be written to a terminal. "
		    "Capture output to a file or pipe to another command.");
	} else {
		context->ic_fp = stdout;
	}
}

/*
 * Extract the payload size from a replay record that is potentially
 * byteswapped. We want to leave the bulk of byteswapping to another module,
 * so just take a quick, nondestructive peek.
 *
 * Record-specific macros such as DRR_WRITE_PAYLOAD_SIZE do not seem to be
 * byteswap-aware. However, with the exception of DRR_OBJECT_PAYLOAD_SIZE,
 * they happen to work with post-swapping since they are switching on either
 * a uint8_t value or 0.
 *
 * DRR_WRITE and DRR_SPILL use 64-bit sizes. The other two record types have
 * 32-bit sizes. The drr_payloadlen field shared by all record types (but
 * used only by BEGIN records is also 32 bits.
 */
static size_t
calc_payload_size(dmu_replay_record_t *drr)
{
	struct drr_object *drro		 = &drr->drr_u.drr_object;
	struct drr_write *drrw		 = &drr->drr_u.drr_write;
	struct drr_spill *drrs		 = &drr->drr_u.drr_spill;
	struct drr_write_embedded *drrwe = &drr->drr_u.drr_write_embedded;

	boolean_t swap = ATTR_IS_SET(CA_BYTESWAPPED);
	uint32_t drr_type = swap ? BSWAP_32(drr->drr_type) : drr->drr_type;
	uint64_t size, size64 = 0;
	uint32_t size32 = 0;
	boolean_t round = B_FALSE;

	if (drr_type == DRR_OBJECT) {
		round = drro->drr_raw_bonuslen == 0;
		size32 = round ? drro->drr_bonuslen : drro->drr_raw_bonuslen;
	} else if (drr_type == DRR_WRITE) {
		size64 = DRR_WRITE_PAYLOAD_SIZE(drrw);
	} else if (drr_type == DRR_SPILL) {
		size64 = DRR_SPILL_PAYLOAD_SIZE(drrs);
	} else if (drr_type == DRR_WRITE_EMBEDDED) {
		size32 = drrwe->drr_psize;
		round = B_TRUE;
	} else if (drr_type == DRR_BEGIN) {
		size32 = drr->drr_payloadlen;
	} else {
		return (0);
	}
	if (size32 != 0) {
		size = swap ? BSWAP_32(size32) : size32;
	} else {
		size = swap ? BSWAP_64(size64) : size64;
	}
	return (round ? P2ROUNDUP(size, 8) : size);
}

/*
 * Must be called only with the first record in a stream. Must be a
 * DRR_BEGIN record or we'll terminate with "invalid stream".
 */
static void
set_stream_attributes(drr_packet_t *item)
{
	dmu_replay_record_t *drr  = &item->dp_drr;
	struct drr_begin *drrb    = &drr->drr_u.drr_begin;
	uint64_t magic		  = drrb->drr_magic;
	uint64_t versioninfo	  = drrb->drr_versioninfo;
	boolean_t i_am_big_endian = htonl(0xFF00) == 0xFF00;

	boolean_t swap_on_output, is_deduped;

	if (magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
		SET_ATTR(CA_BYTESWAPPED);
		versioninfo = BSWAP_64(versioninfo);
	} else if (magic != DMU_BACKUP_MAGIC) {
		errx(1, "invalid ZFS stream, bad magic number %llx",
		    (u_longlong_t)magic);
	}
	if (i_am_big_endian == ATTR_IS_SET(CA_BYTESWAPPED)) {
		SET_ATTR(CA_LITTLE_ENDIAN_INPUT);
	} else {
		SET_ATTR(CA_BIG_ENDIAN_INPUT);
	}
	chain_attrs->ca_feature_flags = DMU_GET_FEATUREFLAGS(versioninfo);

	is_deduped =
	    STREAM_HAS_FEATURE(DMU_BACKUP_FEATURE_DEDUP) ||
	    STREAM_HAS_FEATURE(DMU_BACKUP_FEATURE_DEDUPPROPS);

	if (OPTION_ENABLED(CA_FORBID_DEDUP) && is_deduped) {
		errx(1, "input stream is deduplicated, but this subcommand "
		    "does not support deduplicated streams. Use 'zstream "
		    "redup' to reduplicate.");
	}
	boolean_t req_dedup = OPTION_ENABLED(CA_REQUIRE_DEDUP);
	boolean_t is_dedup = STREAM_HAS_FEATURE(DMU_BACKUP_FEATURE_DEDUP);
	if (req_dedup && !is_dedup) {
		errx(1, "this subcommand requires a deduplicated input "
		    "stream, but the stream is not deduplicated");
	}
	boolean_t req_native = OPTION_ENABLED(CA_REQUIRE_NATIVE_ENDIAN);
	boolean_t is_byteswapped = ATTR_IS_SET(CA_BYTESWAPPED);
	if (req_native && is_byteswapped) {
		errx(1, "this subcommand requires a native-endian "
		    "input stream");
	}

	/*
	 * Figure out output endianness. In the absence of explicit byte
	 * order instructions, we default to preserving the input byte
	 * order. Record headers are always converted to native byte order
	 * for processing, but they can be swapped back on output.
	 *
	 * zfs receive inspects the endianness of each DRR record
	 * and assumes, at least in some cases, that payload data has the
	 * same order as the DMU wrappers.
	 */
	if (OPTION_ENABLED(CA_BIG_ENDIAN_OUT))
		swap_on_output = !i_am_big_endian;
	else if (OPTION_ENABLED(CA_LITTLE_ENDIAN_OUT))
		swap_on_output = i_am_big_endian;
	else if (OPTION_ENABLED(CA_OPPOSITE_ENDIAN_OUT))
		swap_on_output = !ATTR_IS_SET(CA_BYTESWAPPED);
	else
		swap_on_output = ATTR_IS_SET(CA_BYTESWAPPED);

	if (swap_on_output) {
		ENABLE_OPTION(chain_attrs, CA_BYTESWAP_ON_OUTPUT);
	}
}

static disposition_t
chain_read(drr_packet_t *item, io_context_t *context)
{
	if (item == NULL)
		return (D_OK);

	dmu_replay_record_t *drr = &item->dp_drr;

	if (!context->ic_fp)
		open_file(context);

	if (fread(drr, sizeof (dmu_replay_record_t), 1, context->ic_fp) != 1) {
		if (ferror(context->ic_fp)) {
			err(1, "error reading record header at offset %llu",
			    (u_longlong_t)context->ic_offset);
		}
		fclose(context->ic_fp);
		return (D_EOF);
	}

	if (context->ic_offset == 0)
		set_stream_attributes(item);

	size_t payload_size = calc_payload_size(&item->dp_drr);
	if (payload_size > UINT32_MAX) {
		errx(1, "stated packet size is greater than uint32_t"
		    "at offset %llu", (u_longlong_t)context->ic_offset);
	}
	item->dp_payload_size = payload_size;
	if (item->dp_payload_size > 0) {
		item->dp_payload = safe_malloc(item->dp_payload_size);
		size_t n_read = fread(item->dp_payload, item->dp_payload_size,
		    1, context->ic_fp);
		if (n_read != 1) {
			if (ferror(context->ic_fp)) {
				err(1, "error reading record payload at "
				    " offset %llu",
				    (u_longlong_t)context->ic_offset);
			} else {
				/*
				 * We can't exit here because the ZFS test
				 * suite depends on being able to process
				 * streams truncated at random places.
				 */
				warnx("input ends mid-record at offset %llu "
				    "- stream is likely corrupt",
				    (u_longlong_t)context->ic_offset);
				fclose(context->ic_fp);
				free(item->dp_payload);
				return (D_EOF);
			}
		}
	} else {
		item->dp_payload = NULL;
	}
	item->dp_stream_offset = context->ic_offset;

	uint32_t drr_type = ATTR_IS_SET(CA_BYTESWAPPED) ?
	    BSWAP_32(drr->drr_type) : drr->drr_type;

	if (drr_type >= DRR_NUMTYPES) {
		err(1, "invalid record type %llu found at offset %llu",
		    (u_longlong_t)drr_type, (u_longlong_t)context->ic_offset);
	}

	context->ic_offset += sizeof (*drr) + item->dp_payload_size;

	record_stats_t *stats = &chain_attrs->ca_stats_in[drr_type];
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	stats = &chain_attrs->ca_totals_in;
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	return (D_OK);
}

static disposition_t
chain_write(drr_packet_t *item, io_context_t *context)
{
	if (item == NULL) {
		if (context->ic_fp) {
			if (fclose(context->ic_fp) != 0)
				err(1, "error closing output stream");
			context->ic_fp = NULL;
		}
		return (D_OK);
	}

	if (!context->ic_fp) {
		open_file(context);
	}

	dmu_replay_record_t *drr = &item->dp_drr;

	if (fwrite(drr, sizeof (dmu_replay_record_t), 1, context->ic_fp) != 1) {
		err(1, "error writing record header");
	} else if (item->dp_payload_size > 0) {
		size_t n_written = fwrite(item->dp_payload,
		    item->dp_payload_size, 1, context->ic_fp);
		if (n_written != 1) {
			err(1, "error writing payload");
		} else {
			free(item->dp_payload);
			item->dp_payload = NULL;
		}
	}

	uint32_t drr_type = OPTION_ENABLED(CA_BYTESWAP_ON_OUTPUT) ?
	    BSWAP_32(drr->drr_type) : drr->drr_type;

	record_stats_t *stats = &chain_attrs->ca_stats_out[drr_type];
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	stats = &chain_attrs->ca_totals_out;
	stats->rs_num_records++;
	stats->rs_total_header_bytes += sizeof (dmu_replay_record_t);
	stats->rs_total_payload_bytes += item->dp_payload_size;

	return (D_OK);
}

/*
 * Even if the chain doesn't write out a stream, payloads still need freed.
 */
static disposition_t
chain_null_output(drr_packet_t *item, void *context)
{
	(void) context;
	if (item && item->dp_payload != NULL && item->dp_payload_size > 0) {
		free(item->dp_payload);
		item->dp_payload = NULL;
		item->dp_payload_size = 0;
	}
	return (D_OK);
}

/*
 * Storage for the filename must remain valid during chain execution
 */
static chain_step_t
setup_io(const char *filename, boolean_t for_reading)
{
	int context_num = next_io_context++ % MAX_IO_STREAMS;

	io_context_t context = {
		.ic_filename = filename,
		.ic_for_reading = for_reading
	};
	io_contexts[context_num] = context;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = 0,
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = &io_contexts[context_num],
		.cs_serial = {
			.process = (zc_serial_process_f *)
			    (for_reading ? chain_read : chain_write),
		}
	};
	return (step);
}

chain_step_t
serial_read_stream(const char *filename)
{
	return (setup_io(filename, B_TRUE));
}

chain_step_t
serial_write_stream(const char *filename)
{
	return (setup_io(filename, B_FALSE));
}

chain_step_t
serial_null_output(void)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = 0,
		.cs_context = NULL,
		.cs_serial = {
			.process = (zc_serial_process_f *)chain_null_output
		}
	};
	return (step);
}

static disposition_t
chain_checkpoint(drr_packet_t *item, checkpoint_context_t *ctxt)
{
	struct timespec now;
	char buff[32];
	uint64_t delta_b, dbdt;
	double now_sec, delta_t;

	if (item == NULL)
		return (D_OK);

	clock_gettime(CLOCK_MONOTONIC, &now);
	now_sec = now.tv_sec + (double)now.tv_nsec / 1E9;
	if (ctxt->cc_last_sec > 1E-9) {
		delta_t = now_sec - ctxt->cc_last_sec;
		if (delta_t < ctxt->cc_period_sec)
			return (D_OK);
		delta_b = item->dp_stream_offset - ctxt->cc_last_bytes;
		dbdt = delta_b / delta_t;
		zfs_nicenum(dbdt, buff, sizeof (buff));
		fprintf(stderr, "Checkpoint %s: %s/s\n", ctxt->cc_name, buff);
	}
	ctxt->cc_last_sec = now_sec;
	ctxt->cc_last_bytes = item->dp_stream_offset;
	return (D_OK);
}

/*
 * Storage for name must remain valid throughout chain execution
 */
chain_step_t
serial_checkpoint(const char *name)
{
	int context_no = next_checkpoint_context++ % MAX_IO_STREAMS;

	checkpoint_context_t context = {
		.cc_name = name,
		.cc_period_sec = 1.0
	};
	checkpoint_contexts[context_no] = context;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = &checkpoint_contexts[context_no],
		.cs_serial = {
			.process = (zc_serial_process_f *)chain_checkpoint
		},
	};
	return (step);
}
