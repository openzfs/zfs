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
 * Copyright 2022 Axcient.  All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2024, Klara, Inc.
 * Copyright (c) 2026 by Garth Snyder
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
#include <sys/zio_compress.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"
#include "zstream_util.h"

#define	KEYSIZE 64

static disposition_t
chain_decompress_named_writes(drr_packet_t *item, void *context)
{
	(void) context;
	if (item == NULL) {
		return (D_OK);
	}

	dmu_replay_record_t *drr = &item->dp_drr;
	struct drr_write *drrw = &drr->drr_u.drr_write;
	char key[KEYSIZE];
	uint8_t *dcbuff;

	if (drr->drr_type != DRR_WRITE) {
		return (D_OK);
	}

	snprintf(key, KEYSIZE, "%llu,%llu",
	    (u_longlong_t)drrw->drr_object, (u_longlong_t)drrw->drr_offset);
	ENTRY e = { .key = key };
	ENTRY *p = hsearch(e, FIND);
	if (p == NULL) {
		return (D_OK);
	}

	enum zio_compress ctype = (enum zio_compress)(intptr_t)p->data;
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
		free(item->dp_payload);
		item->dp_payload = dcbuff;
		item->dp_payload_size = drrw->drr_logical_size;
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
		    .process =
			(zc_serial_process_f *)chain_decompress_named_writes
		}
	};
	return (step);
}

int
zstream_do_decompress(int argc, char *argv[])
{
	chain_attrs_t attrs = {0};
	int c;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			break;
		case '?':
			fprintf(stderr, "invalid option '%c'\n", optopt);
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
		enum zio_compress type = ZIO_COMPRESS_INHERIT;

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
		if (argv[i]) {
			if (0 == strcmp("off", argv[i]))
				type = ZIO_COMPRESS_OFF;
			else if (0 == strcmp("lz4", argv[i]))
				type = ZIO_COMPRESS_LZ4;
			else if (0 == strcmp("lzjb", argv[i]))
				type = ZIO_COMPRESS_LZJB;
			else if (0 == strcmp("gzip", argv[i]))
				type = ZIO_COMPRESS_GZIP_1;
			else if (0 == strcmp("zle", argv[i]))
				type = ZIO_COMPRESS_ZLE;
			else if (0 == strcmp("zstd", argv[i]))
				type = ZIO_COMPRESS_ZSTD;
			else {
				errx(2, "invalid compression type %s. "
				    "Supported types are off, lz4, lzjb, gzip, "
				    "zle, and zstd", argv[i]);
			}
		}

		int n_chars = asprintf(&key, "%llu,%llu", (u_longlong_t)object,
		    (u_longlong_t)offset);
		if (n_chars < 0)
			err(1, "asprintf");
		ENTRY e = { .key = key };
		ENTRY *p;
		p = hsearch(e, ENTER);
		if (p == NULL)
			errx(1, "hsearch failed");
		p->data = (void *)(intptr_t)type;
	}

	ENABLE_OPTION(&attrs, CA_FORBID_DEDUP);

	zstream_chain_t decompress_chain = {
		STANDARD_INPUT_STACK(NULL),
		serial_decompress_named_writes(),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(decompress_chain, &attrs);

	hdestroy();
	return (0);
}
