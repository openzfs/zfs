// SPDX-License-Identifier: CDDL-1.0
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
chain_drop_records(drr_packet_t *item, void *context)
{
	(void) context;
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
		/*
		 * It really feels like the chain executor ought to be
		 * responsible for freeing this payload. However, it
		 * operates at a more abstract level and knows nothing about
		 * DMU records and their payloads, so this'll have to be
		 * done here when the drop decision is made.
		 *
		 * Fine for now, but if another case like this comes up in
		 * the future, the issue probably needs to be handled
		 * through a more clearly defined path.
		 */
		if (item->dp_payload_size && item->dp_payload != NULL) {
			free(item->dp_payload);
			item->dp_payload = NULL;
			item->dp_payload_size = 0;
		}
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
			.process = (zc_serial_process_f *)chain_drop_records
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
			break;
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
		if (argv[i] == NULL) {
			zstream_usage();
			exit(2);
		}
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
