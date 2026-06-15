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
#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stdtypes.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio_compress.h>

#include "zstream_modules.h"

/*
 * Validate consistency and well-formedness of the actual DRR records. I
 * have swept all the existing validation code into this module, but it's
 * still pretty sparse.
 */

#define	MAX_VALIDATIONS 4

typedef struct {
	int	nesting;
} validate_context_t;

static validate_context_t 	contexts[MAX_VALIDATIONS];
static int			next_context = 0;

static disposition_t
chain_validate_records(drr_packet_t *item, validate_context_t *context)
{
	if (item == NULL)
		return (D_OK);

	struct dmu_replay_record *drr	= &item->dp_drr;
	struct drr_write *drrw		= &drr->drr_u.drr_write;
	struct drr_object *drro		= &drr->drr_u.drr_object;

	if (OPTION_ENABLED(CA_DO_NOT_VALIDATE))
		return (D_OK);

	if (item->dp_stream_offset == 0 && drr->drr_type != DRR_BEGIN) {
		warnx("warning - first record is not DRR_BEGIN");
	}

	if (drr->drr_type == DRR_BEGIN) {
		VERIFY0(context->nesting);
		context->nesting++;
	} else if (drr->drr_type == DRR_END) {
		VERIFY3S(context->nesting, >=, 0);
		if (context->nesting > 0)
			context->nesting--;
	} else if (drr->drr_type >= DRR_NUMTYPES) {
		errx(1, "unknown record type: %d", drr->drr_type);
	} else {
		VERIFY3S(context->nesting, ==, 1);
	}

	switch (drr->drr_type) {

	case DRR_BEGIN:
		VERIFY3U(item->dp_payload_size, <=, 1UL << 28);
		break;

	case DRR_OBJECT:
		{
			boolean_t is_raw = !!(chain_attrs->ca_feature_flags &
			    DMU_BACKUP_FEATURE_RAW);
			boolean_t bonus_gt_raw = drro->drr_bonuslen >
			    drro->drr_raw_bonuslen;
			if (is_raw && bonus_gt_raw) {
				fprintf(stderr,
				    "Warning: object %llu has bonuslen = "
				    "%u > raw_bonuslen = %u\n\n",
				    (u_longlong_t)drro->drr_object,
				    drro->drr_bonuslen,
				    drro->drr_raw_bonuslen);
			}
			break;
		}

	case DRR_WRITE:
		if (drrw->drr_compressiontype >= ZIO_COMPRESS_FUNCTIONS) {
			errx(1, "invalid compression type: %d",
			    drrw->drr_compressiontype);
		}
		break;

	default:
		break;
	}

	return (D_OK);
}

chain_step_t
serial_validate_records(void)
{
	int context_ix = next_context++ % MAX_VALIDATIONS;
	validate_context_t *context = &contexts[context_ix];
	context->nesting = 0;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = context,
		.cs_serial = {
		    .process = (zc_serial_process_f *)chain_validate_records,
		}
	};
	return (step);
}
