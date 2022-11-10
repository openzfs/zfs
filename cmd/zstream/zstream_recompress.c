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
 */

/*
 * Copyright (c) 2022 by Delphix. All rights reserved.
 */

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio_checksum.h>
#include <sys/zstd/zstd.h>
#include "zfs_fletcher.h"
#include "zstream.h"

static int
dump_record(dmu_replay_record_t *drr, void *payload, int payload_len,
    zio_cksum_t *zc, int outfd)
{
	assert(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum)
	    == sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	fletcher_4_incremental_native(drr,
	    offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum), zc);
	if (drr->drr_type != DRR_BEGIN) {
		assert(ZIO_CHECKSUM_IS_ZERO(&drr->drr_u.
		    drr_checksum.drr_checksum));
		drr->drr_u.drr_checksum.drr_checksum = *zc;
	}
	fletcher_4_incremental_native(&drr->drr_u.drr_checksum.drr_checksum,
	    sizeof (zio_cksum_t), zc);
	if (write(outfd, drr, sizeof (*drr)) == -1)
		return (errno);
	if (payload_len != 0) {
		fletcher_4_incremental_native(payload, payload_len, zc);
		if (write(outfd, payload, payload_len) == -1)
			return (errno);
	}
	return (0);
}

int
zstream_do_recompress(int argc, char *argv[])
{
	int bufsz = SPA_MAXBLOCKSIZE;
	char *buf = safe_malloc(bufsz);
	dmu_replay_record_t thedrr;
	dmu_replay_record_t *drr = &thedrr;
	zio_cksum_t stream_cksum;
	int c;
	int level = -1;

	while ((c = getopt(argc, argv, "l:")) != -1) {
		switch (c) {
		case 'l':
			if (sscanf(optarg, "%d", &level) != 0) {
				fprintf(stderr,
				    "failed to parse level '%s'\n",
				    optarg);
				zstream_usage();
			}
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			zstream_usage();
			break;
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		zstream_usage();
	int type = 0;
	zio_compress_info_t *cinfo = NULL;
	if (0 == strcmp(argv[0], "off")) {
		type = ZIO_COMPRESS_OFF;
		cinfo = &zio_compress_table[type];
	} else if (0 == strcmp(argv[0], "inherit") ||
	    0 == strcmp(argv[0], "empty") ||
	    0 == strcmp(argv[0], "on")) {
		// Fall through to invalid compression type case
	} else {
		for (int i = 0; i < ZIO_COMPRESS_FUNCTIONS; i++) {
			if (0 == strcmp(zio_compress_table[i].ci_name,
			    argv[0])) {
				cinfo = &zio_compress_table[i];
				type = i;
				break;
			}
		}
	}
	if (cinfo == NULL) {
		fprintf(stderr, "Invalid compression type %s.\n",
		    argv[0]);
		exit(2);
	}

	if (cinfo->ci_compress == NULL) {
		type = 0;
		cinfo = &zio_compress_table[0];
	}

	if (isatty(STDIN_FILENO)) {
		(void) fprintf(stderr,
		    "Error: The send stream is a binary format "
		    "and can not be read from a\n"
		    "terminal.  Standard input must be redirected.\n");
		exit(1);
	}

	fletcher_4_init();
	zio_init();
	zstd_init();
	while (sfread(drr, sizeof (*drr), stdin) != 0) {
		struct drr_write *drrw;
		uint64_t payload_size = 0;

		/*
		 * We need to regenerate the checksum.
		 */
		if (drr->drr_type != DRR_BEGIN) {
			memset(&drr->drr_u.drr_checksum.drr_checksum, 0,
			    sizeof (drr->drr_u.drr_checksum.drr_checksum));
		}


		switch (drr->drr_type) {
		case DRR_BEGIN:
		{
			ZIO_SET_CHECKSUM(&stream_cksum, 0, 0, 0, 0);

			int sz = drr->drr_payloadlen;
			if (sz != 0) {
				if (sz > bufsz) {
					buf = realloc(buf, sz);
					if (buf == NULL)
						err(1, "realloc");
					bufsz = sz;
				}
				(void) sfread(buf, sz, stdin);
			}
			payload_size = sz;
			break;
		}
		case DRR_END:
		{
			struct drr_end *drre = &drr->drr_u.drr_end;
			/*
			 * Use the recalculated checksum, unless this is
			 * the END record of a stream package, which has
			 * no checksum.
			 */
			if (!ZIO_CHECKSUM_IS_ZERO(&drre->drr_checksum))
				drre->drr_checksum = stream_cksum;
			break;
		}

		case DRR_OBJECT:
		{
			struct drr_object *drro = &drr->drr_u.drr_object;

			if (drro->drr_bonuslen > 0) {
				payload_size = DRR_OBJECT_PAYLOAD_SIZE(drro);
				(void) sfread(buf, payload_size, stdin);
			}
			break;
		}

		case DRR_SPILL:
		{
			struct drr_spill *drrs = &drr->drr_u.drr_spill;
			payload_size = DRR_SPILL_PAYLOAD_SIZE(drrs);
			(void) sfread(buf, payload_size, stdin);
			break;
		}

		case DRR_WRITE_BYREF:
			fprintf(stderr,
			    "Deduplicated streams are not supported\n");
			exit(1);
			break;

		case DRR_WRITE:
		{
			drrw = &thedrr.drr_u.drr_write;
			payload_size = DRR_WRITE_PAYLOAD_SIZE(drrw);
			/*
			 * In order to recompress an encrypted block, you have
			 * to decrypt, decompress, recompress, and
			 * re-encrypt. That can be a future enhancement (along
			 * with decryption or re-encryption), but for now we
			 * skip encrypted blocks.
			 */
			boolean_t encrypted = B_FALSE;
			for (int i = 0; i < ZIO_DATA_SALT_LEN; i++) {
				if (drrw->drr_salt[i] != 0) {
					encrypted = B_TRUE;
					break;
				}
			}
			if (encrypted) {
				(void) sfread(buf, payload_size, stdin);
				break;
			}
			if (drrw->drr_compressiontype >=
			    ZIO_COMPRESS_FUNCTIONS) {
				fprintf(stderr, "Invalid compression type in "
				    "stream: %d\n", drrw->drr_compressiontype);
				exit(3);
			}
			zio_compress_info_t *dinfo =
			    &zio_compress_table[drrw->drr_compressiontype];

			/* Set up buffers to minimize memcpys */
			char *cbuf, *dbuf;
			if (cinfo->ci_compress == NULL)
				dbuf = buf;
			else
				dbuf = safe_calloc(bufsz);

			if (dinfo->ci_decompress == NULL)
				cbuf = dbuf;
			else
				cbuf = safe_calloc(payload_size);

			/* Read and decompress the payload */
			(void) sfread(cbuf, payload_size, stdin);
			if (dinfo->ci_decompress != NULL) {
				if (0 != dinfo->ci_decompress(cbuf, dbuf,
				    payload_size, MIN(bufsz,
				    drrw->drr_logical_size), dinfo->ci_level)) {
					warnx("decompression type %d failed "
					    "for ino %llu offset %llu",
					    type,
					    (u_longlong_t)drrw->drr_object,
					    (u_longlong_t)drrw->drr_offset);
					exit(4);
				}
				payload_size = drrw->drr_logical_size;
				free(cbuf);
			}

			/* Recompress the payload */
			if (cinfo->ci_compress != NULL) {
				payload_size = P2ROUNDUP(cinfo->ci_compress(
				    dbuf, buf, drrw->drr_logical_size,
				    MIN(payload_size, bufsz), (level == -1 ?
				    cinfo->ci_level : level)),
				    SPA_MINBLOCKSIZE);
				if (payload_size != drrw->drr_logical_size) {
					drrw->drr_compressiontype = type;
					drrw->drr_compressed_size =
					    payload_size;
				} else {
					memcpy(buf, dbuf, payload_size);
					drrw->drr_compressiontype = 0;
					drrw->drr_compressed_size = 0;
				}
				free(dbuf);
			} else {
				drrw->drr_compressiontype = type;
				drrw->drr_compressed_size = 0;
			}
			break;
		}

		case DRR_WRITE_EMBEDDED:
		{
			struct drr_write_embedded *drrwe =
			    &drr->drr_u.drr_write_embedded;
			payload_size =
			    P2ROUNDUP((uint64_t)drrwe->drr_psize, 8);
			(void) sfread(buf, payload_size, stdin);
			break;
		}

		case DRR_FREEOBJECTS:
		case DRR_FREE:
		case DRR_OBJECT_RANGE:
			break;

		default:
			(void) fprintf(stderr, "INVALID record type 0x%x\n",
			    drr->drr_type);
			/* should never happen, so assert */
			assert(B_FALSE);
		}

		if (feof(stdout)) {
			fprintf(stderr, "Error: unexpected end-of-file\n");
			exit(1);
		}
		if (ferror(stdout)) {
			fprintf(stderr, "Error while reading file: %s\n",
			    strerror(errno));
			exit(1);
		}

		/*
		 * We need to recalculate the checksum, and it needs to be
		 * initially zero to do that.  BEGIN records don't have
		 * a checksum.
		 */
		if (drr->drr_type != DRR_BEGIN) {
			memset(&drr->drr_u.drr_checksum.drr_checksum, 0,
			    sizeof (drr->drr_u.drr_checksum.drr_checksum));
		}
		if (dump_record(drr, buf, payload_size,
		    &stream_cksum, STDOUT_FILENO) != 0)
			break;
		if (drr->drr_type == DRR_END) {
			/*
			 * Typically the END record is either the last
			 * thing in the stream, or it is followed
			 * by a BEGIN record (which also zeros the checksum).
			 * However, a stream package ends with two END
			 * records.  The last END record's checksum starts
			 * from zero.
			 */
			ZIO_SET_CHECKSUM(&stream_cksum, 0, 0, 0, 0);
		}
	}
	free(buf);
	fletcher_4_fini();
	zio_fini();
	zstd_fini();

	return (0);
}
