/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2012 Martin Matuska <martin@matuska.org>
 */

/*
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 */

#include <ctype.h>
#include <libnvpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

#include <sys/dmu.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <zfs_fletcher.h>
#include "zstream.h"

/*
 * If dump mode is enabled, the number of bytes to print per line
 */
#define	BYTES_PER_LINE	16
/*
 * If dump mode is enabled, the number of bytes to group together, separated
 * by newlines or spaces
 */
#define	DUMP_GROUPING	4

uint64_t total_stream_len = 0;
FILE *send_stream = 0;
boolean_t do_byteswap = B_FALSE;
boolean_t do_cksum = B_TRUE;

void *
safe_malloc(size_t size)
{
	void *rv = malloc(size);
	if (rv == NULL) {
		(void) fprintf(stderr, "ERROR; failed to allocate %zu bytes\n",
		    size);
		abort();
	}
	return (rv);
}

/*
 * ssread - send stream read.
 *
 * Read while computing incremental checksum
 */
static size_t
ssread(void *buf, size_t len, zio_cksum_t *cksum)
{
	size_t outlen;

	if ((outlen = fread(buf, len, 1, send_stream)) == 0)
		return (0);

	if (do_cksum) {
		if (do_byteswap)
			fletcher_4_incremental_byteswap(buf, len, cksum);
		else
			fletcher_4_incremental_native(buf, len, cksum);
	}
	total_stream_len += len;
	return (outlen);
}

static size_t
read_hdr(dmu_replay_record_t *drr, zio_cksum_t *cksum)
{
	ASSERT3U(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    ==, sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	size_t r = ssread(drr, sizeof (*drr) - sizeof (zio_cksum_t), cksum);
	if (r == 0)
		return (0);
	zio_cksum_t saved_cksum = *cksum;
	r = ssread(&drr->drr_u.drr_checksum.drr_checksum,
	    sizeof (zio_cksum_t), cksum);
	if (r == 0)
		return (0);
	if (do_cksum &&
	    !ZIO_CHECKSUM_IS_ZERO(&drr->drr_u.drr_checksum.drr_checksum) &&
	    !ZIO_CHECKSUM_EQUAL(saved_cksum,
	    drr->drr_u.drr_checksum.drr_checksum)) {
		fprintf(stderr, "invalid checksum\n");
		(void) printf("Incorrect checksum in record header.\n");
		(void) printf("Expected checksum = %llx/%llx/%llx/%llx\n",
		    (longlong_t)saved_cksum.zc_word[0],
		    (longlong_t)saved_cksum.zc_word[1],
		    (longlong_t)saved_cksum.zc_word[2],
		    (longlong_t)saved_cksum.zc_word[3]);
		return (0);
	}
	return (sizeof (*drr));
}

/*
 * Print part of a block in ASCII characters
 */
static void
print_ascii_block(char *subbuf, int length)
{
	int i;

	for (i = 0; i < length; i++) {
		char char_print = isprint(subbuf[i]) ? subbuf[i] : '.';
		if (i != 0 && i % DUMP_GROUPING == 0) {
			(void) printf(" ");
		}
		(void) printf("%c", char_print);
	}
	(void) printf("\n");
}

/*
 * print_block - Dump the contents of a modified block to STDOUT
 *
 * Assume that buf has capacity evenly divisible by BYTES_PER_LINE
 */
static void
print_block(char *buf, int length)
{
	int i;
	/*
	 * Start printing ASCII characters at a constant offset, after
	 * the hex prints. Leave 3 characters per byte on a line (2 digit
	 * hex number plus 1 space) plus spaces between characters and
	 * groupings.
	 */
	int ascii_start = BYTES_PER_LINE * 3 +
	    BYTES_PER_LINE / DUMP_GROUPING + 2;

	for (i = 0; i < length; i += BYTES_PER_LINE) {
		int j;
		int this_line_length = MIN(BYTES_PER_LINE, length - i);
		int print_offset = 0;

		for (j = 0; j < this_line_length; j++) {
			int buf_offset = i + j;

			/*
			 * Separate every DUMP_GROUPING bytes by a space.
			 */
			if (buf_offset % DUMP_GROUPING == 0) {
				print_offset += printf(" ");
			}

			/*
			 * Print the two-digit hex value for this byte.
			 */
			unsigned char hex_print = buf[buf_offset];
			print_offset += printf("%02x ", hex_print);
		}

		(void) printf("%*s", ascii_start - print_offset, " ");

		print_ascii_block(buf + i, this_line_length);
	}
}

/*
 * Print an array of bytes to stdout as hexadecimal characters. str must
 * have buf_len * 2 + 1 bytes of space.
 */
static void
sprintf_bytes(char *str, uint8_t *buf, uint_t buf_len)
{
	int i, n;

	for (i = 0; i < buf_len; i++) {
		n = sprintf(str, "%02x", buf[i] & 0xff);
		str += n;
	}

	str[0] = '\0';
}

int
zstream_do_dump(int argc, char *argv[])
{
	char *buf = safe_malloc(SPA_MAXBLOCKSIZE);
	uint64_t drr_record_count[DRR_NUMTYPES] = { 0 };
	uint64_t total_payload_size = 0;
	uint64_t total_overhead_size = 0;
	uint64_t drr_byte_count[DRR_NUMTYPES] = { 0 };
	char salt[ZIO_DATA_SALT_LEN * 2 + 1];
	char iv[ZIO_DATA_IV_LEN * 2 + 1];
	char mac[ZIO_DATA_MAC_LEN * 2 + 1];
	uint64_t total_records = 0;
	uint64_t payload_size;
	dmu_replay_record_t thedrr;
	dmu_replay_record_t *drr = &thedrr;
	struct drr_begin *drrb = &thedrr.drr_u.drr_begin;
	struct drr_end *drre = &thedrr.drr_u.drr_end;
	struct drr_object *drro = &thedrr.drr_u.drr_object;
	struct drr_freeobjects *drrfo = &thedrr.drr_u.drr_freeobjects;
	struct drr_write *drrw = &thedrr.drr_u.drr_write;
	struct drr_write_byref *drrwbr = &thedrr.drr_u.drr_write_byref;
	struct drr_free *drrf = &thedrr.drr_u.drr_free;
	struct drr_spill *drrs = &thedrr.drr_u.drr_spill;
	struct drr_write_embedded *drrwe = &thedrr.drr_u.drr_write_embedded;
	struct drr_object_range *drror = &thedrr.drr_u.drr_object_range;
	struct drr_redact *drrr = &thedrr.drr_u.drr_redact;
	struct drr_checksum *drrc = &thedrr.drr_u.drr_checksum;
	int c;
	boolean_t verbose = B_FALSE;
	boolean_t very_verbose = B_FALSE;
	boolean_t first = B_TRUE;
	/*
	 * dump flag controls whether the contents of any modified data blocks
	 * are printed to the console during processing of the stream. Warning:
	 * for large streams, this can obviously lead to massive prints.
	 */
	boolean_t dump = B_FALSE;
	int err;
	zio_cksum_t zc = { { 0 } };
	zio_cksum_t pcksum = { { 0 } };

	while ((c = getopt(argc, argv, ":vCd")) != -1) {
		switch (c) {
		case 'C':
			do_cksum = B_FALSE;
			break;
		case 'v':
			if (verbose)
				very_verbose = B_TRUE;
			verbose = B_TRUE;
			break;
		case 'd':
			dump = B_TRUE;
			verbose = B_TRUE;
			very_verbose = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr,
			    "missing argument for '%c' option\n", optopt);
			zstream_usage();
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			zstream_usage();
			break;
		}
	}

	if (argc > optind) {
		const char *filename = argv[optind];
		send_stream = fopen(filename, "r");
		if (send_stream == NULL) {
			(void) fprintf(stderr,
			    "Error while opening file '%s': %s\n",
			    filename, strerror(errno));
			exit(1);
		}
	} else {
		if (isatty(STDIN_FILENO)) {
			(void) fprintf(stderr,
			    "Error: The send stream is a binary format "
			    "and can not be read from a\n"
			    "terminal.  Standard input must be redirected, "
			    "or a file must be\n"
			    "specified as a command-line argument.\n");
			exit(1);
		}
		send_stream = stdin;
	}

	fletcher_4_init();
	while (read_hdr(drr, &zc)) {
		uint64_t featureflags = 0;

		/*
		 * If this is the first DMU record being processed, check for
		 * the magic bytes and figure out the endian-ness based on them.
		 */
		if (first) {
			if (drrb->drr_magic == BSWAP_64(DMU_BACKUP_MAGIC)) {
				do_byteswap = B_TRUE;
				if (do_cksum) {
					ZIO_SET_CHECKSUM(&zc, 0, 0, 0, 0);
					/*
					 * recalculate header checksum now
					 * that we know it needs to be
					 * byteswapped.
					 */
					fletcher_4_incremental_byteswap(drr,
					    sizeof (dmu_replay_record_t), &zc);
				}
			} else if (drrb->drr_magic != DMU_BACKUP_MAGIC) {
				(void) fprintf(stderr, "Invalid stream "
				    "(bad magic number)\n");
				exit(1);
			}
			first = B_FALSE;
		}
		if (do_byteswap) {
			drr->drr_type = BSWAP_32(drr->drr_type);
			drr->drr_payloadlen =
			    BSWAP_32(drr->drr_payloadlen);
		}

		/*
		 * At this point, the leading fields of the replay record
		 * (drr_type and drr_payloadlen) have been byte-swapped if
		 * necessary, but the rest of the data structure (the
		 * union of type-specific structures) is still in its
		 * original state.
		 */
		if (drr->drr_type >= DRR_NUMTYPES) {
			(void) printf("INVALID record found: type 0x%x\n",
			    drr->drr_type);
			(void) printf("Aborting.\n");
			exit(1);
		}

		drr_record_count[drr->drr_type]++;
		total_overhead_size += sizeof (*drr);
		total_records++;
		payload_size = 0;

		switch (drr->drr_type) {
		case DRR_BEGIN:
			if (do_byteswap) {
				drrb->drr_magic = BSWAP_64(drrb->drr_magic);
				drrb->drr_versioninfo =
				    BSWAP_64(drrb->drr_versioninfo);
				drrb->drr_creation_time =
				    BSWAP_64(drrb->drr_creation_time);
				drrb->drr_type = BSWAP_32(drrb->drr_type);
				drrb->drr_flags = BSWAP_32(drrb->drr_flags);
				drrb->drr_toguid = BSWAP_64(drrb->drr_toguid);
				drrb->drr_fromguid =
				    BSWAP_64(drrb->drr_fromguid);
			}

			featureflags =
			    DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);

			(void) printf("BEGIN record\n");
			(void) printf("\thdrtype = %lld\n",
			    DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo));
			(void) printf("\tfeatures = %llx\n",
			    DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo));
			(void) printf("\tmagic = %llx\n",
			    (u_longlong_t)drrb->drr_magic);
			(void) printf("\tcreation_time = %llx\n",
			    (u_longlong_t)drrb->drr_creation_time);
			(void) printf("\ttype = %u\n", drrb->drr_type);
			(void) printf("\tflags = 0x%x\n", drrb->drr_flags);
			(void) printf("\ttoguid = %llx\n",
			    (u_longlong_t)drrb->drr_toguid);
			(void) printf("\tfromguid = %llx\n",
			    (u_longlong_t)drrb->drr_fromguid);
			(void) printf("\ttoname = %s\n", drrb->drr_toname);
			(void) printf("\tpayloadlen = %u\n",
			    drr->drr_payloadlen);
			if (verbose)
				(void) printf("\n");

			if (drr->drr_payloadlen != 0) {
				nvlist_t *nv;
				int sz = drr->drr_payloadlen;

				if (sz > SPA_MAXBLOCKSIZE) {
					free(buf);
					buf = safe_malloc(sz);
				}
				(void) ssread(buf, sz, &zc);
				if (ferror(send_stream))
					perror("fread");
				err = nvlist_unpack(buf, sz, &nv, 0);
				if (err) {
					perror(strerror(err));
				} else {
					nvlist_print(stdout, nv);
					nvlist_free(nv);
				}
				payload_size = sz;
			}
			break;

		case DRR_END:
			if (do_byteswap) {
				drre->drr_checksum.zc_word[0] =
				    BSWAP_64(drre->drr_checksum.zc_word[0]);
				drre->drr_checksum.zc_word[1] =
				    BSWAP_64(drre->drr_checksum.zc_word[1]);
				drre->drr_checksum.zc_word[2] =
				    BSWAP_64(drre->drr_checksum.zc_word[2]);
				drre->drr_checksum.zc_word[3] =
				    BSWAP_64(drre->drr_checksum.zc_word[3]);
			}
			/*
			 * We compare against the *previous* checksum
			 * value, because the stored checksum is of
			 * everything before the DRR_END record.
			 */
			if (do_cksum && !ZIO_CHECKSUM_EQUAL(drre->drr_checksum,
			    pcksum)) {
				(void) printf("Expected checksum differs from "
				    "checksum in stream.\n");
				(void) printf("Expected checksum = "
				    "%llx/%llx/%llx/%llx\n",
				    (long long unsigned int)pcksum.zc_word[0],
				    (long long unsigned int)pcksum.zc_word[1],
				    (long long unsigned int)pcksum.zc_word[2],
				    (long long unsigned int)pcksum.zc_word[3]);
			}
			(void) printf("END checksum = %llx/%llx/%llx/%llx\n",
			    (long long unsigned int)
			    drre->drr_checksum.zc_word[0],
			    (long long unsigned int)
			    drre->drr_checksum.zc_word[1],
			    (long long unsigned int)
			    drre->drr_checksum.zc_word[2],
			    (long long unsigned int)
			    drre->drr_checksum.zc_word[3]);

			ZIO_SET_CHECKSUM(&zc, 0, 0, 0, 0);
			break;

		case DRR_OBJECT:
			if (do_byteswap) {
				drro->drr_object = BSWAP_64(drro->drr_object);
				drro->drr_type = BSWAP_32(drro->drr_type);
				drro->drr_bonustype =
				    BSWAP_32(drro->drr_bonustype);
				drro->drr_blksz = BSWAP_32(drro->drr_blksz);
				drro->drr_bonuslen =
				    BSWAP_32(drro->drr_bonuslen);
				drro->drr_raw_bonuslen =
				    BSWAP_32(drro->drr_raw_bonuslen);
				drro->drr_toguid = BSWAP_64(drro->drr_toguid);
				drro->drr_maxblkid =
				    BSWAP_64(drro->drr_maxblkid);
			}

			if (featureflags & DMU_BACKUP_FEATURE_RAW &&
			    drro->drr_bonuslen > drro->drr_raw_bonuslen) {
				(void) fprintf(stderr,
				    "Warning: Object %llu has bonuslen = "
				    "%u > raw_bonuslen = %u\n\n",
				    (u_longlong_t)drro->drr_object,
				    drro->drr_bonuslen, drro->drr_raw_bonuslen);
			}

			payload_size = DRR_OBJECT_PAYLOAD_SIZE(drro);

			if (verbose) {
				(void) printf("OBJECT object = %llu type = %u "
				    "bonustype = %u blksz = %u bonuslen = %u "
				    "dn_slots = %u raw_bonuslen = %u "
				    "flags = %u maxblkid = %llu "
				    "indblkshift = %u nlevels = %u "
				    "nblkptr = %u\n",
				    (u_longlong_t)drro->drr_object,
				    drro->drr_type,
				    drro->drr_bonustype,
				    drro->drr_blksz,
				    drro->drr_bonuslen,
				    drro->drr_dn_slots,
				    drro->drr_raw_bonuslen,
				    drro->drr_flags,
				    (u_longlong_t)drro->drr_maxblkid,
				    drro->drr_indblkshift,
				    drro->drr_nlevels,
				    drro->drr_nblkptr);
			}
			if (drro->drr_bonuslen > 0) {
				(void) ssread(buf, payload_size, &zc);
				if (dump)
					print_block(buf, payload_size);
			}
			break;

		case DRR_FREEOBJECTS:
			if (do_byteswap) {
				drrfo->drr_firstobj =
				    BSWAP_64(drrfo->drr_firstobj);
				drrfo->drr_numobjs =
				    BSWAP_64(drrfo->drr_numobjs);
				drrfo->drr_toguid = BSWAP_64(drrfo->drr_toguid);
			}
			if (verbose) {
				(void) printf("FREEOBJECTS firstobj = %llu "
				    "numobjs = %llu\n",
				    (u_longlong_t)drrfo->drr_firstobj,
				    (u_longlong_t)drrfo->drr_numobjs);
			}
			break;

		case DRR_WRITE:
			if (do_byteswap) {
				drrw->drr_object = BSWAP_64(drrw->drr_object);
				drrw->drr_type = BSWAP_32(drrw->drr_type);
				drrw->drr_offset = BSWAP_64(drrw->drr_offset);
				drrw->drr_logical_size =
				    BSWAP_64(drrw->drr_logical_size);
				drrw->drr_toguid = BSWAP_64(drrw->drr_toguid);
				drrw->drr_key.ddk_prop =
				    BSWAP_64(drrw->drr_key.ddk_prop);
				drrw->drr_compressed_size =
				    BSWAP_64(drrw->drr_compressed_size);
			}

			payload_size = DRR_WRITE_PAYLOAD_SIZE(drrw);

			/*
			 * If this is verbose and/or dump output,
			 * print info on the modified block
			 */
			if (verbose) {
				sprintf_bytes(salt, drrw->drr_salt,
				    ZIO_DATA_SALT_LEN);
				sprintf_bytes(iv, drrw->drr_iv,
				    ZIO_DATA_IV_LEN);
				sprintf_bytes(mac, drrw->drr_mac,
				    ZIO_DATA_MAC_LEN);

				(void) printf("WRITE object = %llu type = %u "
				    "checksum type = %u compression type = %u "
				    "flags = %u offset = %llu "
				    "logical_size = %llu "
				    "compressed_size = %llu "
				    "payload_size = %llu props = %llx "
				    "salt = %s iv = %s mac = %s\n",
				    (u_longlong_t)drrw->drr_object,
				    drrw->drr_type,
				    drrw->drr_checksumtype,
				    drrw->drr_compressiontype,
				    drrw->drr_flags,
				    (u_longlong_t)drrw->drr_offset,
				    (u_longlong_t)drrw->drr_logical_size,
				    (u_longlong_t)drrw->drr_compressed_size,
				    (u_longlong_t)payload_size,
				    (u_longlong_t)drrw->drr_key.ddk_prop,
				    salt,
				    iv,
				    mac);
			}

			/*
			 * Read the contents of the block in from STDIN to buf
			 */
			(void) ssread(buf, payload_size, &zc);
			/*
			 * If in dump mode
			 */
			if (dump) {
				print_block(buf, payload_size);
			}
			break;

		case DRR_WRITE_BYREF:
			if (do_byteswap) {
				drrwbr->drr_object =
				    BSWAP_64(drrwbr->drr_object);
				drrwbr->drr_offset =
				    BSWAP_64(drrwbr->drr_offset);
				drrwbr->drr_length =
				    BSWAP_64(drrwbr->drr_length);
				drrwbr->drr_toguid =
				    BSWAP_64(drrwbr->drr_toguid);
				drrwbr->drr_refguid =
				    BSWAP_64(drrwbr->drr_refguid);
				drrwbr->drr_refobject =
				    BSWAP_64(drrwbr->drr_refobject);
				drrwbr->drr_refoffset =
				    BSWAP_64(drrwbr->drr_refoffset);
				drrwbr->drr_key.ddk_prop =
				    BSWAP_64(drrwbr->drr_key.ddk_prop);
			}
			if (verbose) {
				(void) printf("WRITE_BYREF object = %llu "
				    "checksum type = %u props = %llx "
				    "offset = %llu length = %llu "
				    "toguid = %llx refguid = %llx "
				    "refobject = %llu refoffset = %llu\n",
				    (u_longlong_t)drrwbr->drr_object,
				    drrwbr->drr_checksumtype,
				    (u_longlong_t)drrwbr->drr_key.ddk_prop,
				    (u_longlong_t)drrwbr->drr_offset,
				    (u_longlong_t)drrwbr->drr_length,
				    (u_longlong_t)drrwbr->drr_toguid,
				    (u_longlong_t)drrwbr->drr_refguid,
				    (u_longlong_t)drrwbr->drr_refobject,
				    (u_longlong_t)drrwbr->drr_refoffset);
			}
			break;

		case DRR_FREE:
			if (do_byteswap) {
				drrf->drr_object = BSWAP_64(drrf->drr_object);
				drrf->drr_offset = BSWAP_64(drrf->drr_offset);
				drrf->drr_length = BSWAP_64(drrf->drr_length);
			}
			if (verbose) {
				(void) printf("FREE object = %llu "
				    "offset = %llu length = %lld\n",
				    (u_longlong_t)drrf->drr_object,
				    (u_longlong_t)drrf->drr_offset,
				    (longlong_t)drrf->drr_length);
			}
			break;
		case DRR_SPILL:
			if (do_byteswap) {
				drrs->drr_object = BSWAP_64(drrs->drr_object);
				drrs->drr_length = BSWAP_64(drrs->drr_length);
				drrs->drr_compressed_size =
				    BSWAP_64(drrs->drr_compressed_size);
				drrs->drr_type = BSWAP_32(drrs->drr_type);
			}

			payload_size = DRR_SPILL_PAYLOAD_SIZE(drrs);

			if (verbose) {
				sprintf_bytes(salt, drrs->drr_salt,
				    ZIO_DATA_SALT_LEN);
				sprintf_bytes(iv, drrs->drr_iv,
				    ZIO_DATA_IV_LEN);
				sprintf_bytes(mac, drrs->drr_mac,
				    ZIO_DATA_MAC_LEN);

				(void) printf("SPILL block for object = %llu "
				    "length = %llu flags = %u "
				    "compression type = %u "
				    "compressed_size = %llu "
				    "payload_size = %llu "
				    "salt = %s iv = %s mac = %s\n",
				    (u_longlong_t)drrs->drr_object,
				    (u_longlong_t)drrs->drr_length,
				    drrs->drr_flags,
				    drrs->drr_compressiontype,
				    (u_longlong_t)drrs->drr_compressed_size,
				    (u_longlong_t)payload_size,
				    salt,
				    iv,
				    mac);
			}
			(void) ssread(buf, payload_size, &zc);
			if (dump) {
				print_block(buf, payload_size);
			}
			break;
		case DRR_WRITE_EMBEDDED:
			if (do_byteswap) {
				drrwe->drr_object =
				    BSWAP_64(drrwe->drr_object);
				drrwe->drr_offset =
				    BSWAP_64(drrwe->drr_offset);
				drrwe->drr_length =
				    BSWAP_64(drrwe->drr_length);
				drrwe->drr_toguid =
				    BSWAP_64(drrwe->drr_toguid);
				drrwe->drr_lsize =
				    BSWAP_32(drrwe->drr_lsize);
				drrwe->drr_psize =
				    BSWAP_32(drrwe->drr_psize);
			}
			if (verbose) {
				(void) printf("WRITE_EMBEDDED object = %llu "
				    "offset = %llu length = %llu "
				    "toguid = %llx comp = %u etype = %u "
				    "lsize = %u psize = %u\n",
				    (u_longlong_t)drrwe->drr_object,
				    (u_longlong_t)drrwe->drr_offset,
				    (u_longlong_t)drrwe->drr_length,
				    (u_longlong_t)drrwe->drr_toguid,
				    drrwe->drr_compression,
				    drrwe->drr_etype,
				    drrwe->drr_lsize,
				    drrwe->drr_psize);
			}
			(void) ssread(buf,
			    P2ROUNDUP(drrwe->drr_psize, 8), &zc);
			if (dump) {
				print_block(buf,
				    P2ROUNDUP(drrwe->drr_psize, 8));
			}
			payload_size = P2ROUNDUP(drrwe->drr_psize, 8);
			break;
		case DRR_OBJECT_RANGE:
			if (do_byteswap) {
				drror->drr_firstobj =
				    BSWAP_64(drror->drr_firstobj);
				drror->drr_numslots =
				    BSWAP_64(drror->drr_numslots);
				drror->drr_toguid = BSWAP_64(drror->drr_toguid);
			}
			if (verbose) {
				sprintf_bytes(salt, drror->drr_salt,
				    ZIO_DATA_SALT_LEN);
				sprintf_bytes(iv, drror->drr_iv,
				    ZIO_DATA_IV_LEN);
				sprintf_bytes(mac, drror->drr_mac,
				    ZIO_DATA_MAC_LEN);

				(void) printf("OBJECT_RANGE firstobj = %llu "
				    "numslots = %llu flags = %u "
				    "salt = %s iv = %s mac = %s\n",
				    (u_longlong_t)drror->drr_firstobj,
				    (u_longlong_t)drror->drr_numslots,
				    drror->drr_flags,
				    salt,
				    iv,
				    mac);
			}
			break;
		case DRR_REDACT:
			if (do_byteswap) {
				drrr->drr_object = BSWAP_64(drrr->drr_object);
				drrr->drr_offset = BSWAP_64(drrr->drr_offset);
				drrr->drr_length = BSWAP_64(drrr->drr_length);
				drrr->drr_toguid = BSWAP_64(drrr->drr_toguid);
			}
			if (verbose) {
				(void) printf("REDACT object = %llu offset = "
				    "%llu length = %llu\n",
				    (u_longlong_t)drrr->drr_object,
				    (u_longlong_t)drrr->drr_offset,
				    (u_longlong_t)drrr->drr_length);
			}
			break;
		case DRR_NUMTYPES:
			/* should never be reached */
			exit(1);
		}
		if (drr->drr_type != DRR_BEGIN && very_verbose) {
			(void) printf("    checksum = %llx/%llx/%llx/%llx\n",
			    (longlong_t)drrc->drr_checksum.zc_word[0],
			    (longlong_t)drrc->drr_checksum.zc_word[1],
			    (longlong_t)drrc->drr_checksum.zc_word[2],
			    (longlong_t)drrc->drr_checksum.zc_word[3]);
		}
		pcksum = zc;
		drr_byte_count[drr->drr_type] += payload_size;
		total_payload_size += payload_size;
	}
	free(buf);
	fletcher_4_fini();

	/* Print final summary */

	(void) printf("SUMMARY:\n");
	(void) printf("\tTotal DRR_BEGIN records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_BEGIN],
	    (u_longlong_t)drr_byte_count[DRR_BEGIN]);
	(void) printf("\tTotal DRR_END records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_END],
	    (u_longlong_t)drr_byte_count[DRR_END]);
	(void) printf("\tTotal DRR_OBJECT records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_OBJECT],
	    (u_longlong_t)drr_byte_count[DRR_OBJECT]);
	(void) printf("\tTotal DRR_FREEOBJECTS records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_FREEOBJECTS],
	    (u_longlong_t)drr_byte_count[DRR_FREEOBJECTS]);
	(void) printf("\tTotal DRR_WRITE records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_WRITE],
	    (u_longlong_t)drr_byte_count[DRR_WRITE]);
	(void) printf("\tTotal DRR_WRITE_BYREF records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_WRITE_BYREF],
	    (u_longlong_t)drr_byte_count[DRR_WRITE_BYREF]);
	(void) printf("\tTotal DRR_WRITE_EMBEDDED records = %lld (%llu "
	    "bytes)\n", (u_longlong_t)drr_record_count[DRR_WRITE_EMBEDDED],
	    (u_longlong_t)drr_byte_count[DRR_WRITE_EMBEDDED]);
	(void) printf("\tTotal DRR_FREE records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_FREE],
	    (u_longlong_t)drr_byte_count[DRR_FREE]);
	(void) printf("\tTotal DRR_SPILL records = %lld (%llu bytes)\n",
	    (u_longlong_t)drr_record_count[DRR_SPILL],
	    (u_longlong_t)drr_byte_count[DRR_SPILL]);
	(void) printf("\tTotal records = %lld\n",
	    (u_longlong_t)total_records);
	(void) printf("\tTotal payload size = %lld (0x%llx)\n",
	    (u_longlong_t)total_payload_size, (u_longlong_t)total_payload_size);
	(void) printf("\tTotal header overhead = %lld (0x%llx)\n",
	    (u_longlong_t)total_overhead_size,
	    (u_longlong_t)total_overhead_size);
	(void) printf("\tTotal stream length = %lld (0x%llx)\n",
	    (u_longlong_t)total_stream_len, (u_longlong_t)total_stream_len);
	return (0);
}
