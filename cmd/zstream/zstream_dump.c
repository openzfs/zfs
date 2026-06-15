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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2012 Martin Matuska <martin@matuska.org>
 * Copyright (c) 2013, 2015 by Delphix. All rights reserved.
 * Portions copyright 2026 by Garth Snyder <garth@garthsnyder.com>
 */

#include <ctype.h>
#include <err.h>
#include <libnvpair.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/nvpair.h>
#include <sys/param.h>
#include <sys/spa_checksum.h>
#include <sys/stdtypes.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"

/*
 * If dump mode is enabled, the number of bytes to print per line
 */
#define	BYTES_PER_LINE	16
/*
 * If dump mode is enabled, the number of bytes to group together, separated
 * by newlines or spaces
 */
#define	DUMP_GROUPING	4

typedef struct {
	uint8_t drr_salt[ZIO_DATA_SALT_LEN];
	uint8_t drr_iv[ZIO_DATA_IV_LEN];
	uint8_t drr_mac[ZIO_DATA_MAC_LEN];
} crypto_fields_t;

typedef void dumper_f(drr_packet_t *item);

typedef struct {
	const char	*rt_typename;
	dumper_f	*rt_dumper;
} record_type_t;

static int stream_error;

/*
 * Print part of a block in ASCII characters
 */
static void
print_ascii_block(uint8_t *subbuf, int length)
{
	int i;

	for (i = 0; i < length; i++) {
		char char_print = isprint(subbuf[i]) ? subbuf[i] : '.';
		if (i != 0 && i % DUMP_GROUPING == 0) {
			printf(" ");
		}
		printf("%c", char_print);
	}
	printf("\n");
}

/*
 * print_block - Dump the contents of a modified block to STDOUT
 *
 * Assumes that buf has capacity evenly divisible by BYTES_PER_LINE
 */
static void
print_block(uint8_t *buf, uint32_t length)
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

static void
maybe_dump_payload(drr_packet_t *item)
{
	if (OPTION_ENABLED(CA_DUMP_DATA)) {
		print_block(item->dp_payload, item->dp_payload_size);
	}
}

static char *
stringify_encryption_fields(void *crypto_in)
{
	crypto_fields_t *crypto = crypto_in;
	char salt[sizeof (crypto->drr_salt) * 2 + 1];
	char iv[sizeof (crypto->drr_iv) * 2 + 1];
	char mac[sizeof (crypto->drr_mac) * 2 + 1];
	static char buff[sizeof (salt) + sizeof (iv) + sizeof (mac) + 32];

	sprintf_bytes(salt, crypto->drr_salt, sizeof (crypto->drr_salt));
	sprintf_bytes(iv, crypto->drr_iv, sizeof (crypto->drr_iv));
	sprintf_bytes(mac, crypto->drr_mac, sizeof (crypto->drr_mac));
	snprintf(buff, sizeof (buff), "salt = %s iv = %s mac = %s",
	    salt, iv, mac);
	return (buff);
}

static void
dump_begin_record(drr_packet_t *item)
{
	dmu_replay_record_t *drr = &item->dp_drr;
	struct drr_begin *drrb = &item->dp_drr.drr_u.drr_begin;

	printf("BEGIN record\n");
	printf("\thdrtype = %llu\n",
	    DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo));
	printf("\tfeatures = %llx\n",
	    DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo));
	printf("\tmagic = %llx\n", (u_longlong_t)drrb->drr_magic);
	printf("\tcreation_time = %llx\n",
	    (u_longlong_t)drrb->drr_creation_time);
	printf("\ttype = %u\n", drrb->drr_type);
	printf("\tflags = 0x%x\n", drrb->drr_flags);
	printf("\ttoguid = %llx\n", (u_longlong_t)drrb->drr_toguid);
	printf("\tfromguid = %llx\n", (u_longlong_t)drrb->drr_fromguid);
	printf("\ttoname = %s\n", drrb->drr_toname);
	printf("\tpayloadlen = %u\n", drr->drr_payloadlen);

	if (OPTION_ENABLED(CA_VERBOSE))
		printf("\n");

	if (drr->drr_payloadlen >= 2) {
		nvlist_t *nv;
		/*
		 * It looks like zfs send or the ioctls it's using are
		 * generating packed nvlists with NV_ENCODE_NATIVE encoding
		 * in some circumstances. I don't think these can be decoded
		 * on an opposite-endian system, even by the core ZFS code.
		 */
		uint8_t *nvlist_header = item->dp_payload;
		uint8_t nvlist_encoding = nvlist_header[0];
		boolean_t big_endian = nvlist_header[1] == 0;
		if (nvlist_encoding == NV_ENCODE_XDR) {
			printf("nvlist encoding = NV_ENCODE_XDR\n");
		} else {
			printf("nvlist encoding = NV_ENCODE_NATIVE (%s)\n",
			    big_endian ? "big-endian" : "little-endian");
		}
		int err = nvlist_unpack((char *)item->dp_payload,
		    drr->drr_payloadlen, &nv, 0);
		if (err) {
			printf("failed to unpack DRR_BEGIN nvlist: %s\n",
			    strerror(err));
			if (!stream_error)
				stream_error = err;
		} else {
			nvlist_print(stdout, nv);
			nvlist_free(nv);
		}
	} else if (drr->drr_payloadlen != 0) {
		printf("unexpected packed nvlist length %d\n",
		    drr->drr_payloadlen);
	}
}

static void
dump_end_record(drr_packet_t *item)
{
	struct drr_end *drre = &item->dp_drr.drr_u.drr_end;

	printf("END checksum = %llx/%llx/%llx/%llx\n",
	    (u_longlong_t)drre->drr_checksum.zc_word[0],
	    (u_longlong_t)drre->drr_checksum.zc_word[1],
	    (u_longlong_t)drre->drr_checksum.zc_word[2],
	    (u_longlong_t)drre->drr_checksum.zc_word[3]);
}

static void
dump_object_record(drr_packet_t *item)
{
	struct drr_object *drro = &item->dp_drr.drr_u.drr_object;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("OBJECT object = %llu type = %u "
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
		maybe_dump_payload(item);
	}
}

static void
dump_freeobjects_record(drr_packet_t *item)
{
	struct drr_freeobjects *drrfo = &item->dp_drr.drr_u.drr_freeobjects;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("FREEOBJECTS firstobj = %llu numobjs = %llu\n",
		    (u_longlong_t)drrfo->drr_firstobj,
		    (u_longlong_t)drrfo->drr_numobjs);
	}
}

static void
dump_write_record(drr_packet_t *item)
{
	struct drr_write *drrw = &item->dp_drr.drr_u.drr_write;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("WRITE object = %llu type = %u "
		    "checksum type = %u compression type = %u "
		    "flags = %u offset = %llu "
		    "logical_size = %llu "
		    "compressed_size = %llu "
		    "payload_size = %u props = %llx "
		    "%s\n",
		    (u_longlong_t)drrw->drr_object,
		    drrw->drr_type,
		    drrw->drr_checksumtype,
		    drrw->drr_compressiontype,
		    drrw->drr_flags,
		    (u_longlong_t)drrw->drr_offset,
		    (u_longlong_t)drrw->drr_logical_size,
		    (u_longlong_t)drrw->drr_compressed_size,
		    item->dp_payload_size,
		    (u_longlong_t)drrw->drr_key.ddk_prop,
		    stringify_encryption_fields(&drrw->drr_salt));
	}
	maybe_dump_payload(item);
}

static void
dump_write_byref_record(drr_packet_t *item)
{
	struct drr_write_byref *drrwbr = &item->dp_drr.drr_u.drr_write_byref;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("WRITE_BYREF object = %llu "
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
}

static void
dump_free_record(drr_packet_t *item)
{
	struct drr_free *drrf = &item->dp_drr.drr_u.drr_free;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("FREE object = %llu "
		    "offset = %llu length = %lld\n",
		    (u_longlong_t)drrf->drr_object,
		    (u_longlong_t)drrf->drr_offset,
		    (longlong_t)drrf->drr_length);
	}
}

static void
dump_spill_record(drr_packet_t *item)
{
	struct drr_spill *drrs = &item->dp_drr.drr_u.drr_spill;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("SPILL block for object = %llu "
		    "length = %llu flags = %u "
		    "compression type = %u "
		    "compressed_size = %llu "
		    "payload_size = %u "
		    "%s\n",
		    (u_longlong_t)drrs->drr_object,
		    (u_longlong_t)drrs->drr_length,
		    drrs->drr_flags,
		    drrs->drr_compressiontype,
		    (u_longlong_t)drrs->drr_compressed_size,
		    item->dp_payload_size,
		    stringify_encryption_fields(&drrs->drr_salt));
	}
	maybe_dump_payload(item);
}

static void
dump_write_embedded_record(drr_packet_t *item)
{
	struct drr_write_embedded *drrwe =
	    &item->dp_drr.drr_u.drr_write_embedded;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("WRITE_EMBEDDED object = %llu "
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
	maybe_dump_payload(item);
}

static void
dump_object_range_record(drr_packet_t *item)
{
	struct drr_object_range *drror = &item->dp_drr.drr_u.drr_object_range;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("OBJECT_RANGE firstobj = %llu "
		    "numslots = %llu flags = %u "
		    "%s\n",
		    (u_longlong_t)drror->drr_firstobj,
		    (u_longlong_t)drror->drr_numslots,
		    drror->drr_flags,
		    stringify_encryption_fields(&drror->drr_salt));
	}
}

static void
dump_redact_record(drr_packet_t *item)
{
	struct drr_redact *drrr = &item->dp_drr.drr_u.drr_redact;

	if (OPTION_ENABLED(CA_VERBOSE)) {
		printf("REDACT object = %llu offset = "
		    "%llu length = %llu\n",
		    (u_longlong_t)drrr->drr_object,
		    (u_longlong_t)drrr->drr_offset,
		    (u_longlong_t)drrr->drr_length);
	}
}

static disposition_t
chain_dump_record(drr_packet_t *item, record_type_t *context)
{
	if (item == NULL) {
		return (D_OK);
	}

	dmu_replay_record_t *drr = &item->dp_drr;
	zio_cksum_t *cksum = &drr->drr_u.drr_checksum.drr_checksum;
	int type = (int)drr->drr_type;

	context[type].rt_dumper(item);

	if (type != DRR_BEGIN && OPTION_ENABLED(CA_VERY_VERBOSE)) {
		printf("    checksum = %llx/%llx/%llx/%llx\n",
		    (u_longlong_t)cksum->zc_word[0],
		    (u_longlong_t)cksum->zc_word[1],
		    (u_longlong_t)cksum->zc_word[2],
		    (u_longlong_t)cksum->zc_word[3]);
	}

	return (D_OK);
}

static chain_step_t
serial_dump_records(record_type_t *context)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = context,
		.cs_serial = {
			.process = (zc_serial_process_f *)chain_dump_record
		}
	};
	return (step);
}

int
zstream_do_dump(int argc, char *argv[])
{
	chain_attrs_t attrs = {0};
	const char *input_file = NULL;
	int c;

	record_type_t record_types[] = {
		{ "DRR_BEGIN", 		dump_begin_record },
		{ "DRR_OBJECT", 	dump_object_record },
		{ "DRR_FREEOBJECTS", 	dump_freeobjects_record },
		{ "DRR_WRITE", 		dump_write_record },
		{ "DRR_FREE", 		dump_free_record },
		{ "DRR_END", 		dump_end_record },
		{ "DRR_WRITE_BYREF", 	dump_write_byref_record },
		{ "DRR_SPILL", 		dump_spill_record },
		{ "DRR_WRITE_EMBEDDED",	dump_write_embedded_record },
		{ "DRR_OBJECT_RANGE",	dump_object_range_record },
		{ "DRR_REDACT",		dump_redact_record }
	};

	while ((c = getopt(argc, argv, ":vCd")) != -1) {
		switch (c) {
		case 'C':
			ENABLE_OPTION(&attrs, CA_IGNORE_CKSUMS);
			break;
		case 'v':
			if (attrs.ca_command_opts & CA_VERBOSE) {
				ENABLE_OPTION(&attrs, CA_VERY_VERBOSE);
			} else {
				ENABLE_OPTION(&attrs, CA_VERBOSE);
			}
			break;
		case 'd':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			ENABLE_OPTION(&attrs, CA_VERY_VERBOSE);
			ENABLE_OPTION(&attrs, CA_DUMP_DATA);
			break;
		case ':':
			warnx("missing argument for '%c' option\n", optopt);
			zstream_usage();
			break;
		case '?':
			warnx("invalid option '%c'\n", optopt);
			zstream_usage();
			break;
		}
	}

	if (argc > optind) {
		input_file = argv[optind];
	}

	zstream_chain_t dump_chain = {
		STANDARD_INPUT_STACK(input_file),
		serial_dump_records(record_types),
		NULL_OUTPUT_STACK()
	};

	stream_error = 0;
	zstream_chain_exec(dump_chain, &attrs);

	/*
	 * Match previous zstream dump summary order
	 */
	int print_order[] = {
		DRR_BEGIN, DRR_END, DRR_OBJECT, DRR_FREEOBJECTS,
		DRR_WRITE, DRR_WRITE_BYREF, DRR_WRITE_EMBEDDED,
		DRR_FREE, DRR_SPILL, DRR_OBJECT_RANGE, DRR_REDACT
	};

	printf("SUMMARY:\n");
	for (int i = 0; i < DRR_NUMTYPES; i++) {
		int type = print_order[i];
		record_type_t *rec = &record_types[type];
		record_stats_t *stats = &attrs.ca_stats_in[type];
		printf("\tTotal %s records = %llu (%llu bytes)\n",
		    rec->rt_typename,
		    (u_longlong_t)stats->rs_num_records,
		    (u_longlong_t)stats->rs_total_payload_bytes);
	}

	uint64_t total_payload =
	    attrs.ca_totals_in.rs_total_payload_bytes;
	uint64_t total_header =
	    attrs.ca_totals_in.rs_total_header_bytes;

	printf("\tTotal records = %llu\n",
	    (u_longlong_t)attrs.ca_totals_in.rs_num_records);
	printf("\tTotal payload size = %llu (0x%llx)\n",
	    (u_longlong_t)total_payload, (u_longlong_t)total_payload);
	printf("\tTotal header overhead = %llu (0x%llx)\n",
	    (u_longlong_t)total_header, (u_longlong_t)total_header);
	printf("\tTotal stream length = %llu (0x%llx)\n",
	    (u_longlong_t)(total_header + total_payload),
	    (u_longlong_t)(total_header + total_payload));

	if (stream_error) {
		fflush(stdout);
		fprintf(stderr, "\nzstream dump completed with errors (first "
		    "error code %d)\n", stream_error);
		exit(stream_error);
	}
	return (0);
}
