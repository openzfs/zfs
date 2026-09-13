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
 * Copyright 2022 Axcient.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2024, Klara, Inc.
 * Copyright (c) 2026 by Garth Snyder
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
#include <sys/zio_compress.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"
#include "zstream_util.h"

#define	KEYSIZE 64

static disposition_t
chain_decompress_named_writes(void *item_in, void *context)
{
	(void) context;
	drr_packet_t *item = (drr_packet_t *)item_in;

	if (item == NULL) {
		return (D_OK);
	}

	dmu_replay_record_t *drr = &item->dp_drr;
	struct drr_write *drrw = &drr->drr_u.drr_write;
	uint8_t *dcbuff;

	if (drr->drr_type != DRR_WRITE) {
		return (D_OK);
	}

	enum zio_compress ctype;
	boolean_t found = lookup_record_specifier(drrw->drr_object,
	    drrw->drr_offset, &ctype);
	if (!found)
		return (D_OK);
	if (ctype == ZIO_COMPRESS_INHERIT) {
		/* Unspecified */
		ctype = drrw->drr_compressiontype;
	}
	if (ctype_is_uncompressed(ctype)) {
		drrw->drr_compressiontype = 0;
		drrw->drr_logical_size = drrw->drr_compressed_size;
		drrw->drr_compressed_size = 0;
		if (OPTION_ENABLED(CA_VERBOSE)) {
			fprintf(stderr,
			    "Resetting compression type to "
			    "off for ino %llu offset %llu\n",
			    (u_longlong_t)drrw->drr_object,
			    (u_longlong_t)drrw->drr_offset);
		}
		return (D_OK);
	}

	if (write_is_encrypted(drrw)) {
		warnx("the write for ino %llu offset %llu is marked "
		    "as encrypted. Attempting decompression anyway...",
		    (u_longlong_t)drrw->drr_object,
		    (u_longlong_t)drrw->drr_offset);
	}

	dcbuff = decompress_buffer(item->dp_payload, item->dp_payload_size,
	    drrw->drr_logical_size, ctype);

	if (dcbuff == NULL) {
		/*
		 * The block must not be compressed, at least not with this
		 * compression type, possibly because it gets written
		 * multiple times in this stream.
		 */
		warnx("decompression failed for ino %llu offset %llu",
		    (u_longlong_t)drrw->drr_object,
		    (u_longlong_t)drrw->drr_offset);
	} else {
		set_payload(item, dcbuff, drrw->drr_logical_size);
		drrw->drr_compressiontype = 0;
		drrw->drr_compressed_size = 0;
		if (OPTION_ENABLED(CA_VERBOSE)) {
			fprintf(stderr,
			    "Successfully decompressed ino %llu offset %llu\n",
			    (u_longlong_t)drrw->drr_object,
			    (u_longlong_t)drrw->drr_offset);
		}
	}
	return (D_OK);
}

static chain_step_t
serial_decompress_named_writes(void)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = NULL,
		.cs_serial = {
		    .process = chain_decompress_named_writes
		}
	};
	return (step);
}

int
zstream_do_decompress(int argc, char *argv[])
{
	chain_attrs_t attrs = {0};
	struct stat statbuf;
	char *stream_file = NULL;
	int c;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			break;
		case '?':
			fprintf(stderr, "invalid option '%c'\n", optopt);
			zstream_usage();
		}
	}

	argc -= optind;
	argv += optind;

	int num_specifiers = parse_record_specifiers(argc, argv, B_TRUE);
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

	zstream_chain_t decompress_chain = {
		STANDARD_INPUT_STACK(stream_file),
		serial_decompress_named_writes(),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(decompress_chain, &attrs);

	destroy_record_specifier_hash();
	return (0);
}
