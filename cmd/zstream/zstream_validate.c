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
#include <sys/dmu_recv.h>
#include <sys/stdtypes.h>
#include <sys/zfs_ioctl.h>

#include "zstream_modules.h"

/*
 * Validate consistency and well-formedness of the actual DRR records.
 */

#define	MAX_VALIDATIONS 4

typedef struct {
	int	nesting;
	uint64_t featureflags;
	boolean_t begin_spill;
} validate_context_t;

static validate_context_t 	contexts[MAX_VALIDATIONS];
static int			next_context = 0;

static void
validate_fail(int err, const char *msg)
{
	if (err == 0)
		return;
	if (msg != NULL && msg[0] != '\0')
		errx(1, "%s", msg);
	errx(1, "invalid receive stream record (error %d)", err);
}

static boolean_t
validate_stream_has_feature(const validate_context_t *context, uint64_t feature)
{
	/*
	 * STREAM_HAS_FEATURE() describes the first DRR_BEGIN in the input.
	 * Recursive streams can contain later BEGIN records with different
	 * feature flags, so validation must use the current substream flags.
	 */
	return ((context->featureflags & feature) != 0);
}

static disposition_t
chain_validate_records(void *item_in, void *context_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	validate_context_t *context = (validate_context_t *)context_in;

	if (item == NULL)
		return (D_OK);

	struct dmu_replay_record *drr	= &item->dp_drr;
	struct drr_write *drrw		= &drr->drr_u.drr_write;
	struct drr_object *drro		= &drr->drr_u.drr_object;
	struct drr_spill *drrs		= &drr->drr_u.drr_spill;
	struct drr_write_embedded *drrwe = &drr->drr_u.drr_write_embedded;
	struct drr_free *drrf		= &drr->drr_u.drr_free;
	struct drr_freeobjects *drrfo	= &drr->drr_u.drr_freeobjects;
	struct drr_object_range *drror	= &drr->drr_u.drr_object_range;
	char errbuf[RECV_CHECK_ERRBUFLEN];
	int err;
	boolean_t is_raw;

	if (OPTION_ENABLED(CA_DO_NOT_VALIDATE))
		return (D_OK);

	if (item->dp_stream_offset == 0 && drr->drr_type != DRR_BEGIN) {
		warnx("warning - first record is not DRR_BEGIN");
	}

	if (drr->drr_type == DRR_BEGIN) {
		VERIFY0(context->nesting);
		context->nesting++;
		context->featureflags = DMU_GET_FEATUREFLAGS(
		    drr->drr_u.drr_begin.drr_versioninfo);
		context->begin_spill = !!(drr->drr_u.drr_begin.drr_flags &
		    DRR_FLAG_SPILL_BLOCK);
	} else if (drr->drr_type == DRR_END) {
		VERIFY3S(context->nesting, >=, 0);
		if (context->nesting > 0)
			context->nesting--;
	} else if (drr->drr_type >= DRR_NUMTYPES) {
		errx(1, "unknown record type: %d", drr->drr_type);
	} else {
		VERIFY3S(context->nesting, ==, 1);
	}

	is_raw = validate_stream_has_feature(context, DMU_BACKUP_FEATURE_RAW);

	switch (drr->drr_type) {

	case DRR_BEGIN:
		VERIFY3U(item->dp_payload_size, <=, 1UL << 28);
		break;

	case DRR_OBJECT:
		err = recv_check_drr_object(drro, NULL, is_raw,
		    context->begin_spill, context->featureflags, errbuf,
		    sizeof (errbuf));
		validate_fail(err, errbuf);
		break;

	case DRR_WRITE:
		err = recv_check_drr_write(drrw, NULL, is_raw,
		    context->featureflags, errbuf, sizeof (errbuf));
		validate_fail(err, errbuf);
		break;

	case DRR_WRITE_EMBEDDED:
		err = recv_check_drr_write_embedded(drrwe, NULL, is_raw,
		    context->featureflags, errbuf, sizeof (errbuf));
		validate_fail(err, errbuf);
		break;

	case DRR_SPILL:
		err = recv_check_drr_spill(drrs, NULL, is_raw,
		    context->featureflags, errbuf, sizeof (errbuf));
		validate_fail(err, errbuf);
		break;

	case DRR_FREE:
	case DRR_REDACT:
		err = recv_check_drr_free(drrf, errbuf, sizeof (errbuf));
		validate_fail(err, errbuf);
		break;

	case DRR_FREEOBJECTS:
		err = recv_check_drr_freeobjects(drrfo, errbuf,
		    sizeof (errbuf));
		validate_fail(err, errbuf);
		break;

	case DRR_OBJECT_RANGE:
		err = recv_check_drr_object_range(drror, is_raw, errbuf,
		    sizeof (errbuf));
		validate_fail(err, errbuf);
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
	context->featureflags = 0;
	context->begin_spill = B_FALSE;

	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = context,
		.cs_serial = {
		    .process = chain_validate_records,
		}
	};
	return (step);
}
