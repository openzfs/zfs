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
 * Copyright (c) 2022 by Delphix. All rights reserved.
 * Copyright (c) 2024, Klara, Inc.
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
	int level = 0;

	while ((c = getopt(argc, argv, "l:")) != -1) {
		switch (c) {
		case 'l':
			if (sscanf(optarg, "%d", &level) != 1) {
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

	enum zio_compress ctype;
	if (strcmp(argv[0], "off") == 0) {
		ctype = ZIO_COMPRESS_OFF;
	} else {
		for (ctype = 0; ctype < ZIO_COMPRESS_FUNCTIONS; ctype++) {
			if (strcmp(argv[0],
			    zio_compress_table[ctype].ci_name) == 0)
				break;
		}
		if (ctype == ZIO_COMPRESS_FUNCTIONS ||
		    zio_compress_table[ctype].ci_compress == NULL) {
			fprintf(stderr, "Invalid compression type %s.\n",
			    argv[0]);
			exit(2);
		}
	}

	if (isatty(STDIN_FILENO)) {
		(void) fprintf(stderr,
		    "Error: The send stream is a binary format "
		    "and can not be read from a\n"
		    "terminal.  Standard input must be redirected.\n");
		exit(1);
	}

	abd_init();
	fletcher_4_init();
	zio_init();
	zstd_init();
	int begin = 0;
	boolean_t seen = B_FALSE;
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
			VERIFY0(begin++);
			seen = B_TRUE;

			uint32_t sz = drr->drr_payloadlen;

			VERIFY3U(sz, <=, 1U << 28);

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
			 * We would prefer to just check --begin == 0, but
			 * replication streams have an end of stream END
			 * record, so we must avoid tripping it.
			 */
			VERIFY3B(seen, ==, B_TRUE);
			begin--;
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
			VERIFY3S(begin, ==, 1);

			if (drro->drr_bonuslen > 0) {
				payload_size = DRR_OBJECT_PAYLOAD_SIZE(drro);
				(void) sfread(buf, payload_size, stdin);
			}
			break;
		}

		case DRR_SPILL:
		{
			struct drr_spill *drrs = &drr->drr_u.drr_spill;
			VERIFY3S(begin, ==, 1);
			payload_size = DRR_SPILL_PAYLOAD_SIZE(drrs);
			(void) sfread(buf, payload_size, stdin);
			break;
		}

		case DRR_WRITE_BYREF:
			VERIFY3S(begin, ==, 1);
			fprintf(stderr,
			    "Deduplicated streams are not supported\n");
			exit(1);
			break;

		case DRR_WRITE:
		{
			VERIFY3S(begin, ==, 1);
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
			enum zio_compress dtype = drrw->drr_compressiontype;
			if (dtype >= ZIO_COMPRESS_FUNCTIONS) {
				fprintf(stderr, "Invalid compression type in "
				    "stream: %d\n", dtype);
				exit(3);
			}
			if (zio_compress_table[dtype].ci_decompress == NULL)
				dtype = ZIO_COMPRESS_OFF;

			/* Set up buffers to minimize memcpys */
			char *cbuf, *dbuf;
			if (ctype == ZIO_COMPRESS_OFF)
				dbuf = buf;
			else
				dbuf = safe_calloc(bufsz);

			if (dtype == ZIO_COMPRESS_OFF)
				cbuf = dbuf;
			else
				cbuf = safe_calloc(payload_size);

			/* Read and decompress the payload */
			(void) sfread(cbuf, payload_size, stdin);
			if (dtype != ZIO_COMPRESS_OFF) {
				abd_t cabd, dabd;
				abd_get_from_buf_struct(&cabd,
				    cbuf, payload_size);
				abd_get_from_buf_struct(&dabd, dbuf,
				    MIN(bufsz, drrw->drr_logical_size));
				if (zio_decompress_data(dtype, &cabd, &dabd,
				    payload_size, abd_get_size(&dabd),
				    NULL) != 0) {
					warnx("decompression type %d failed "
					    "for ino %llu offset %llu",
					    dtype,
					    (u_longlong_t)drrw->drr_object,
					    (u_longlong_t)drrw->drr_offset);
					exit(4);
				}
				payload_size = drrw->drr_logical_size;
				abd_free(&dabd);
				abd_free(&cabd);
				free(cbuf);
			}

			/* Recompress the payload */
			if (ctype != ZIO_COMPRESS_OFF) {
				abd_t dabd, abd;
				abd_get_from_buf_struct(&dabd,
				    dbuf, drrw->drr_logical_size);
				abd_t *pabd =
				    abd_get_from_buf_struct(&abd, buf, bufsz);
				size_t csize = zio_compress_data(ctype, &dabd,
				    &pabd, drrw->drr_logical_size, level);
				size_t rounded =
				    P2ROUNDUP(csize, SPA_MINBLOCKSIZE);
				if (rounded >= drrw->drr_logical_size) {
					memcpy(buf, dbuf, payload_size);
					drrw->drr_compressiontype = 0;
					drrw->drr_compressed_size = 0;
				} else {
					abd_zero_off(pabd, csize,
					    rounded - csize);
					drrw->drr_compressiontype = ctype;
					drrw->drr_compressed_size =
					    payload_size = rounded;
				}
				abd_free(&abd);
				abd_free(&dabd);
				free(dbuf);
			} else {
				drrw->drr_compressiontype = 0;
				drrw->drr_compressed_size = 0;
			}
			break;
		}

		case DRR_WRITE_EMBEDDED:
		{
			struct drr_write_embedded *drrwe =
			    &drr->drr_u.drr_write_embedded;
			VERIFY3S(begin, ==, 1);
			payload_size =
			    P2ROUNDUP((uint64_t)drrwe->drr_psize, 8);
			(void) sfread(buf, payload_size, stdin);
			break;
		}

		case DRR_FREEOBJECTS:
		case DRR_FREE:
		case DRR_OBJECT_RANGE:
			VERIFY3S(begin, ==, 1);
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
	abd_fini();

	return (0);
}
