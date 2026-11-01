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
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#include <assert.h>
#include <cityhash.h>
#include <err.h>
#include <errno.h>
#include <libzutil.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/zfs_ioctl.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_hash.h"
#include "zstream_modules.h"
#include "zstream_util.h"

#define	HASH_PHYSMEM_PERCENT			30
#define	SMALLEST_POSSIBLE_HASH_MEMORY_MB	128

typedef struct {
	uint64_t	rk_toguid;
	uint64_t	rk_object;
	uint64_t	rk_offset;
} redup_key_t;

typedef struct {
	redup_key_t	rhe_key;
	uint64_t	rhe_stream_offset;
} redup_hash_entry_t;

typedef struct {
	linear_hash_t	*rc_hash;
	FILE		*rc_fp;
} redup_context_t;

static inline void
hash_insert(linear_hash_t *lh, struct drr_write *drrw, uint64_t stream_offset)
{
	redup_hash_entry_t rhe = {
		.rhe_key = {
			.rk_toguid = drrw->drr_toguid,
			.rk_object = drrw->drr_object,
			.rk_offset = drrw->drr_offset
		},
		.rhe_stream_offset = stream_offset
	};
	if (lh_upsert(lh, &rhe) == LH_UPDATED)
		warnx("duplicate redup hash table entry, overwriting...");
}

static inline uint64_t
hash_lookup(linear_hash_t *lh, struct drr_write_byref *drrwb)
{
	redup_hash_entry_t rhe = {
		.rhe_key = {
			.rk_toguid = drrwb->drr_refguid,
			.rk_object = drrwb->drr_refobject,
			.rk_offset = drrwb->drr_refoffset
		},
	};
	if (lh_find(lh, &rhe) == LH_NOT_FOUND)
		errx(1, "could not find expected redup table entry");
	return (rhe.rhe_stream_offset);
}

static disposition_t
chain_redup_writes(void *item_in, void *context_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	redup_context_t *context = (redup_context_t *)context_in;

	if (item == NULL) {
		return (D_OK);
	}

	dmu_replay_record_t *drr = &item->dp_drr;
	struct drr_write *drrw	 = &drr->drr_u.drr_write;
	struct drr_begin *drrb	 = &drr->drr_u.drr_begin;

	switch (drr->drr_type) {

	case DRR_BEGIN:
	{
		uint64_t flags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
		flags &= ~(DMU_BACKUP_FEATURE_DEDUP |
		    DMU_BACKUP_FEATURE_DEDUPPROPS);
		DMU_SET_FEATUREFLAGS(drrb->drr_versioninfo, flags);
		break;
	}

	case DRR_WRITE_BYREF:
	{
		struct drr_write_byref drrwb = drr->drr_u.drr_write_byref;

		/*
		 * Look up in hash table by drrwb->drr_refguid,
		 * drr_refobject, drr_refoffset. Replace this record with
		 * the found WRITE record, but with drr_object, drr_offset,
		 * and drr_toguid replaced with ours.
		 */
		uint64_t stream_offset = hash_lookup(context->rc_hash, &drrwb);

		if (fseeko(context->rc_fp, stream_offset, SEEK_SET) != 0) {
			err(1, "seek into source file failed, offset %llu",
			    (u_longlong_t)stream_offset);
		}
		if (fread(drr, sizeof (*drr), 1, context->rc_fp) != 1) {
			err(1, "read of prior write failed");
		}
		if (ATTR_IS_SET(CA_BYTESWAPPED)) {
			byteswap_record(drr, BSWAP_32(drr->drr_type));
		}

		VERIFY3U(drr->drr_type,    ==, DRR_WRITE);
		VERIFY3U(drrw->drr_toguid, ==, drrwb.drr_refguid);
		VERIFY3U(drrw->drr_object, ==, drrwb.drr_refobject);
		VERIFY3U(drrw->drr_offset, ==, drrwb.drr_refoffset);

		uint64_t size = DRR_WRITE_PAYLOAD_SIZE(drrw);
		uint8_t *buff = safe_malloc(size);
		size_t n_read = fread(buff, size, 1, context->rc_fp);
		if (n_read != 1)
			err(1, "read of prior payload failed");
		set_payload(item, buff, size);

		drrw->drr_toguid = drrwb.drr_toguid;
		drrw->drr_object = drrwb.drr_object;
		drrw->drr_offset = drrwb.drr_offset;
		break;
	}

	case DRR_WRITE:
		hash_insert(context->rc_hash, drrw, item->dp_stream_offset);
		break;

	default:
		break;
	}
	return (D_OK);
}

static chain_step_t
serial_redup_writes(redup_context_t *context)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = context,
		.cs_serial = {
			.process = chain_redup_writes
		}
	};
	return (step);
}

/*
 * The default memory budget for the reduplication table
 */
static size_t
default_hash_memory(void)
{
#ifdef _ILP32
	return ((size_t)SMALLEST_POSSIBLE_HASH_MEMORY_MB << 20);
#else
	uint64_t physbytes = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
	return (MAX((physbytes * HASH_PHYSMEM_PERCENT) / 100,
	    SMALLEST_POSSIBLE_HASH_MEMORY_MB << 20));
#endif
}

static void
print_redup_summary(chain_attrs_t *attrs, linear_hash_t *lh)
{
	record_stats_t *acsi = attrs->ca_stats_in;
	nicebuff_t b = nicenums(3,
	    attrs->ca_totals_in.rs_num_records,
	    acsi[DRR_WRITE].rs_num_records,
	    acsi[DRR_WRITE_BYREF].rs_num_records);
	fprintf(stderr, "Converted stream with %s total records, "
	    "including %s WRITE records and %s WRITE_BYREF "
	    "records\n", b.nice[0], b.nice[1], b.nice[2]);

	lh_stats_t stats = lh_get_stats(lh);
	b = nicenums(2, stats.ls_total_records, (uint64_t)stats.ls_memory_use);
	fprintf(stderr, "Stored %s hash table entries using %s of memory\n",
	    b.nice[0], b.nice[1]);
}

int
zstream_do_redup(int argc, char *argv[])
{
	int c;
	chain_attrs_t attrs = {0};
	redup_context_t context = {0};
	const char *temp_dir = "/var/tmp";
	size_t hash_memory = default_hash_memory();

	while ((c = getopt(argc, argv, "vd:m:")) != -1) {
		switch (c) {
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			break;
		case 'd':
			temp_dir = optarg;
			break;
		case 'm':
			if (parse_size_specifier(optarg, &hash_memory) != 0)
				errx(1, "invalid memory size '%s'", optarg);
			break;
		case '?':
			warnx("invalid option '%c'", optopt);
			zstream_usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc != 1)
		zstream_usage();

	context.rc_fp = fopen(argv[0], "rb");
	if (context.rc_fp == NULL) {
		err(1, "unable to open %s", argv[0]);
	}

	context.rc_hash = lh_init(sizeof (redup_hash_entry_t),
	    sizeof (redup_key_t), hash_memory, temp_dir);

	zstream_chain_t redup_chain = {
		STANDARD_INPUT_STACK(argv[0]),
		serial_redup_writes(&context),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(redup_chain, &attrs);

	if (attrs.ca_command_opts & CA_VERBOSE)
		print_redup_summary(&attrs, context.rc_hash);

	fclose(context.rc_fp);
	lh_fini(context.rc_hash);
	return (0);
}
