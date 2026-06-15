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

#include <err.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/byteorder.h>
#include <sys/spa_checksum.h>
#include <sys/stdtypes.h>
#include <sys/zfs_ioctl.h>

#include "zstream_modules.h"

/*
 * Mostly from dmu_recv.c
 */

#define	DO64(X) (drr->drr_u.X = BSWAP_64(drr->drr_u.X))
#define	DO32(X) (drr->drr_u.X = BSWAP_32(drr->drr_u.X))

typedef byteswap_stage_t byteswap_context_t;

static byteswap_context_t byteswap_contexts[MAX_BYTESWAP];
static int next_context = 0;

static disposition_t
chain_byteswap(drr_packet_t *item, byteswap_context_t *context)
{
	if (item == NULL) {
		return (D_OK);
	}

	struct dmu_replay_record *drr = &item->dp_drr;
	boolean_t input_swapped = *context == BS_INCOMING &&
	    ATTR_IS_SET(CA_BYTESWAPPED);
	boolean_t swap = input_swapped || (*context == BS_OUTGOING &&
	    OPTION_ENABLED(CA_BYTESWAP_ON_OUTPUT));
	uint32_t drr_type =
	    input_swapped ? BSWAP_32(drr->drr_type) : drr->drr_type;

	if (swap) {
		byteswap_record(drr, drr_type);
	}
	return (D_OK);
}

/*
 * Unconditionally byteswap a DMU replay record. drr_type is passed in
 * separately because we don't know whether we're doing input or output
 * swapping.
 */
void
byteswap_record(dmu_replay_record_t *drr, uint32_t drr_type)
{
	drr->drr_type = BSWAP_32(drr->drr_type);
	drr->drr_payloadlen = BSWAP_32(drr->drr_payloadlen);

	switch (drr_type) {

	case DRR_BEGIN:
		DO64(drr_begin.drr_magic);
		DO64(drr_begin.drr_versioninfo);
		DO64(drr_begin.drr_creation_time);
		DO32(drr_begin.drr_type);
		DO32(drr_begin.drr_flags);
		DO64(drr_begin.drr_toguid);
		DO64(drr_begin.drr_fromguid);
		break;

	case DRR_END:
		DO64(drr_end.drr_toguid);
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_end.drr_checksum);
		break;

	case DRR_OBJECT:
		DO64(drr_object.drr_object);
		DO32(drr_object.drr_type);
		DO32(drr_object.drr_bonustype);
		DO32(drr_object.drr_blksz);
		DO32(drr_object.drr_bonuslen);
		DO32(drr_object.drr_raw_bonuslen);
		DO64(drr_object.drr_toguid);
		DO64(drr_object.drr_maxblkid);
		break;

	case DRR_FREEOBJECTS:
		DO64(drr_freeobjects.drr_firstobj);
		DO64(drr_freeobjects.drr_numobjs);
		DO64(drr_freeobjects.drr_toguid);
		break;

	case DRR_WRITE:
		DO64(drr_write.drr_object);
		DO32(drr_write.drr_type);
		DO64(drr_write.drr_offset);
		DO64(drr_write.drr_logical_size);
		DO64(drr_write.drr_toguid);
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_write.drr_key.ddk_cksum);
		DO64(drr_write.drr_key.ddk_prop);
		DO64(drr_write.drr_compressed_size);
		break;

	case DRR_WRITE_BYREF:
		DO64(drr_write_byref.drr_object);
		DO64(drr_write_byref.drr_offset);
		DO64(drr_write_byref.drr_length);
		DO64(drr_write_byref.drr_toguid);
		DO64(drr_write_byref.drr_refguid);
		DO64(drr_write_byref.drr_refobject);
		DO64(drr_write_byref.drr_refoffset);
		ZIO_CHECKSUM_BSWAP(
		    &drr->drr_u.drr_write_byref.drr_key.ddk_cksum);
		DO64(drr_write_byref.drr_key.ddk_prop);
		break;

	case DRR_FREE:
		DO64(drr_free.drr_object);
		DO64(drr_free.drr_offset);
		DO64(drr_free.drr_length);
		/* Note: toguid not byte-swapped in original zstream_dump.c */
		DO64(drr_free.drr_toguid);
		break;

	case DRR_SPILL:
		DO64(drr_spill.drr_object);
		DO64(drr_spill.drr_length);
		/* Note: toguid not byte-swapped in original zstream_dump.c */
		DO64(drr_spill.drr_toguid);
		DO64(drr_spill.drr_compressed_size);
		DO32(drr_spill.drr_type);
		break;

	case DRR_WRITE_EMBEDDED:
		DO64(drr_write_embedded.drr_object);
		DO64(drr_write_embedded.drr_offset);
		DO64(drr_write_embedded.drr_length);
		DO64(drr_write_embedded.drr_toguid);
		DO32(drr_write_embedded.drr_lsize);
		DO32(drr_write_embedded.drr_psize);
		break;

	case DRR_OBJECT_RANGE:
		DO64(drr_object_range.drr_firstobj);
		DO64(drr_object_range.drr_numslots);
		DO64(drr_object_range.drr_toguid);
		break;

	case DRR_REDACT:
		DO64(drr_redact.drr_object);
		DO64(drr_redact.drr_offset);
		DO64(drr_redact.drr_length);
		DO64(drr_redact.drr_toguid);
		break;

	default:
		errx(1, "unknown record type %llu, aborting...",
		    (u_longlong_t)drr_type);
	}

	if (drr_type != DRR_BEGIN) {
		ZIO_CHECKSUM_BSWAP(&drr->drr_u.drr_checksum.drr_checksum);
	}
}

chain_step_t
serial_byteswap(byteswap_stage_t stage)
{
	int context_ix = next_context++ % MAX_BYTESWAP;
	byteswap_context_t *bsc = &byteswap_contexts[context_ix];

	*bsc = stage;
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = bsc,
		.cs_serial = {
			.process = (zc_serial_process_f *)chain_byteswap,
		}
	};
	return (step);
}
