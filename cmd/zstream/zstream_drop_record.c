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
#include <search.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stdtypes.h>
#include <sys/zfs_ioctl.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"

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
	char key[KEYSIZE];
	u_longlong_t object, offset;
	const char *record_type;
	ENTRY e = {.key = key};

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

	snprintf(key, KEYSIZE, "%llu,%llu", object, offset);
	if (hsearch(e, FIND) != NULL) {
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
zstream_do_drop_record(int argc, char *argv[])
{
	int c;
	chain_attrs_t attrs = {0};

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

	if (argc < 0)
		zstream_usage();
	if (hcreate(argc) == 0)
		errx(1, "hcreate failed");

	for (int i = 0; i < argc; i++) {

		uint64_t object, offset;
		char *obj_str;
		char *offset_str;
		char *key;
		char *end;

		obj_str = strsep(&argv[i], ",");
		if (argv[i] == NULL)
			zstream_usage();
		errno = 0;
		object = strtoull(obj_str, &end, 0);
		if (errno || *end != '\0')
			errx(1, "invalid value for object");
		offset_str = strsep(&argv[i], ",");
		offset = strtoull(offset_str, &end, 0);
		if (errno || *end != '\0')
			errx(1, "invalid value for offset");

		if (asprintf(&key, "%llu,%llu", (u_longlong_t)object,
		    (u_longlong_t)offset) < 0) {
			err(1, "asprintf");
		}
		ENTRY e = {.key = key};
		ENTRY *p;
		p = hsearch(e, ENTER);
		if (p == NULL)
			errx(1, "hsearch");
		p->data = (void *)(intptr_t)B_TRUE;
	}

	ENABLE_OPTION(&attrs, CA_FORBID_DEDUP);

	zstream_chain_t drop_chain = {
		STANDARD_INPUT_STACK(NULL),
		serial_drop_records(),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(drop_chain, &attrs);

	hdestroy();
	return (0);
}
