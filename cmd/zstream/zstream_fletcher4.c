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

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/byteorder.h>
#include <sys/spa_checksum.h>
#include <sys/stdtypes.h>
#include <sys/types.h>
#include <sys/zfs_ioctl.h>
#include <zfs_fletcher.h>

#include "zstream_modules.h"
#include "zstream_util.h"

#define	CK_OFFSET offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum)
#define	END_CK_OFFSET offsetof(dmu_replay_record_t, drr_u.drr_end.drr_checksum)

typedef enum { F4_SET, F4_VALIDATE } fletcher4_op_t;

typedef zio_cksum_t fletcher4_context_t;

static fletcher4_context_t	fletcher4_contexts[MAX_FLETCHER_4];
static int			next_context = 0;

static inline int
fletcher_4_incremental(boolean_t swap, void *buff, size_t size, void *cksum)
{
	if (swap) {
		return (fletcher_4_incremental_byteswap(buff, size, cksum));
	} else {
		return (fletcher_4_incremental_native(buff, size, cksum));
	}
}

/*
 * Emit or validate (below) a replay record with proper checksums and with
 * proper maintenance of the stream checksum. That is:
 *
 *   1) Update stream checksum with the record header up to drr_checksum.
 *   2) Update drr_checksum field in the record header from stream checksum.
 *   3) Update stream checksum with the checksum field in the record header.
 *   4) Update stream checksum with the contents of the payload.
 *
 * DRR_BEGIN records do not have record checksums. They can't, because the
 * drr_begin struct overlaps with space that would otherwise be used for the
 * end-record checksum.
 */
static disposition_t
chain_add_fletcher4(drr_packet_t *item, zio_cksum_t *stream_cksum)
{
	if (item == NULL)
		return (D_OK);

	dmu_replay_record_t *drr   = &item->dp_drr;
	struct drr_end *drre	   = &item->dp_drr.drr_u.drr_end;
	zio_cksum_t *record_cksum  = &drr->drr_u.drr_checksum.drr_checksum;
	zio_cksum_t *end_cksum	   = &drre->drr_checksum;

	boolean_t swap = OPTION_ENABLED(CA_BYTESWAP_ON_OUTPUT);
	uint32_t drr_type = swap ? BSWAP_32(drr->drr_type) : drr->drr_type;

	if (drr_type == DRR_BEGIN) {
		ZIO_SET_CHECKSUM(stream_cksum, 0, 0, 0, 0);
	} else if (drr_type == DRR_END) {
		*end_cksum = *stream_cksum;
		if (swap)
			ZIO_CHECKSUM_BSWAP(end_cksum);
	}
	fletcher_4_incremental(swap, drr, CK_OFFSET, stream_cksum);
	if (drr_type != DRR_BEGIN && !IS_CONCLUSION(drr, drr_type)) {
		*record_cksum = *stream_cksum;
		if (swap)
			ZIO_CHECKSUM_BSWAP(record_cksum);
	}
	if (drr_type == DRR_END) {
		ZIO_SET_CHECKSUM(stream_cksum, 0, 0, 0, 0);
	} else {
		fletcher_4_incremental(swap, record_cksum,
		    sizeof (drr->drr_u.drr_checksum.drr_checksum),
		    stream_cksum);
		if (item->dp_payload_size > 0) {
			fletcher_4_incremental(swap, item->dp_payload,
			    item->dp_payload_size, stream_cksum);
		}
	}
	return (D_OK);
}

static disposition_t
chain_validate_fletcher4(drr_packet_t *item, zio_cksum_t *stream_cksum)
{
	if (item == NULL || OPTION_ENABLED(CA_IGNORE_CKSUMS)) {
		return (D_OK);
	}

	dmu_replay_record_t *drr   = &item->dp_drr;
	struct drr_end *drre	   = &item->dp_drr.drr_u.drr_end;
	zio_cksum_t *record_cksum  = &drr->drr_u.drr_checksum.drr_checksum;
	zio_cksum_t *end_cksum	   = &drre->drr_checksum;

	boolean_t swap = ATTR_IS_SET(CA_BYTESWAPPED);
	uint32_t drr_type = swap ? BSWAP_32(drr->drr_type) : drr->drr_type;

	if (drr_type == DRR_BEGIN) {
		ZIO_SET_CHECKSUM(stream_cksum, 0, 0, 0, 0);
	} else if (drr_type == DRR_END) {
		off_t stream_offset = item->dp_stream_offset + END_CK_OFFSET;
		validate_or_exit(stream_cksum, end_cksum, swap,
		    "in DRR_END record", stream_offset);
	}
	fletcher_4_incremental(swap, drr, CK_OFFSET, stream_cksum);
	if (drr_type != DRR_BEGIN && !IS_CONCLUSION(drr, drr_type)) {
		off_t stream_offset = item->dp_stream_offset + CK_OFFSET;
		validate_or_exit(stream_cksum, record_cksum,
		    swap, "at DRR record end", stream_offset);
	}
	if (drr_type == DRR_END) {
		ZIO_SET_CHECKSUM(stream_cksum, 0, 0, 0, 0);
	} else {
		fletcher_4_incremental(swap, record_cksum,
		    sizeof (drr->drr_u.drr_checksum.drr_checksum),
		    stream_cksum);
		if (item->dp_payload_size > 0) {
			fletcher_4_incremental(swap, item->dp_payload,
			    item->dp_payload_size, stream_cksum);
		}
	}
	return (D_OK);
}

static chain_step_t
fletcher4_serial_step(fletcher4_op_t operation)
{
	int context_ix = next_context++ % MAX_FLETCHER_4;
	fletcher4_context_t *context = &fletcher4_contexts[context_ix];

	ZIO_SET_CHECKSUM(context, 0, 0, 0, 0);
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = context,
		.cs_serial = {
			.process = (zc_serial_process_f *)
			    ((operation == F4_VALIDATE) ?
			    chain_validate_fletcher4 : chain_add_fletcher4)
		}
	};
	return (step);
}

chain_step_t
serial_add_fletcher4(void)
{
	return (fletcher4_serial_step(F4_SET));
}

chain_step_t
serial_validate_fletcher4(void)
{
	return (fletcher4_serial_step(F4_VALIDATE));
}
