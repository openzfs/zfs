// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright 2026 ConnectWise.  All rights reserved.
 * Use is subject to license terms.
 */

#include <err.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/stdtypes.h>
#include <sys/zfs_ioctl.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"
#include "zstream_util.h"

#define	KEYSIZE 64

static disposition_t
chain_drop_records(void *item_in, void *context)
{
	(void) context;
	drr_packet_t *item = (drr_packet_t *)item_in;

	if (item == NULL)
		return (D_OK);

	dmu_replay_record_t *drr = &item->dp_drr;
	struct drr_write *drrw = &drr->drr_u.drr_write;
	struct drr_write_embedded *drrwe = &drr->drr_u.drr_write_embedded;
	u_longlong_t object, offset;
	const char *record_type;

	if (drr->drr_type == DRR_WRITE) {
		object = drrw->drr_object;
		offset = drrw->drr_offset;
		record_type = "WRITE";
	} else if (drr->drr_type == DRR_WRITE_EMBEDDED) {
		object = drrwe->drr_object;
		offset = drrwe->drr_offset;
		record_type = "WRITE_EMBEDDED";
	} else {
		return (D_OK);
	}

	enum zio_compress ctype;
	if (lookup_record_specifier(object, offset, &ctype)) {
		if (OPTION_ENABLED(CA_VERBOSE)) {
			warnx("dropping %s record for object %llu "
			    "offset %llu", record_type, object, offset);
		}
		set_payload(item, NULL, 0);
		return (D_DROP);
	}

	return (D_OK);
}

static chain_step_t
serial_drop_records(void)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = NULL,
		.cs_serial = {
			.process = chain_drop_records
		}
	};
	return (step);
}

int
zstream_do_drop_records(int argc, char *argv[])
{
	int c;
	chain_attrs_t attrs = {0};
	struct stat statbuf;
	char *stream_file = NULL;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			break;
		case '?':
			warnx("invalid option '%c'\n", optopt);
			zstream_usage();
		}
	}

	argc -= optind;
	argv += optind;

	int num_specifiers = parse_record_specifiers(argc, argv, B_FALSE);
	argc -= num_specifiers;
	argv += num_specifiers;

	if (argc > 1) {
		errx(1, "invalid record specifier '%s'", argv[0]);
	} else if (argc == 1) {
		if (stat(argv[0], &statbuf) == 0) {
			stream_file = argv[0];
		} else {
			err(1, "%s", argv[0]);
		}
	}

	ENABLE_OPTION(&attrs, CA_FORBID_DEDUP);

	zstream_chain_t drop_chain = {
		STANDARD_INPUT_STACK(stream_file),
		serial_drop_records(),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(drop_chain, &attrs);

	destroy_record_specifier_hash();
	return (0);
}
