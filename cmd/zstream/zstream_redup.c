// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
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
#include <sys/bitops.h>
#include <sys/param.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/zfs_ioctl.h>
#include <umem.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"
#include "zstream_util.h"

#define	MAX_RDT_PHYSMEM_PERCENT		20
#define	SMALLEST_POSSIBLE_MAX_RDT_MB	128

typedef struct redup_entry {
	struct redup_entry	*rde_next;
	uint64_t		rde_guid;
	uint64_t		rde_object;
	uint64_t		rde_offset;
	uint64_t		rde_stream_offset;
} redup_entry_t;

typedef struct redup_table {
	redup_entry_t	**redup_hash_array;
	umem_cache_t	*ddecache;
	uint64_t	ddt_count;
	int		numhashbits;
} redup_table_t;

typedef struct {
	redup_table_t	rc_rdt;
	FILE		*rc_fp;
} redup_context_t;

static void
rdt_insert(redup_table_t *rdt,
    uint64_t guid, uint64_t object, uint64_t offset, uint64_t stream_offset)
{
	uint64_t ch = cityhash3(guid, object, offset);
	uint64_t hashcode = BF64_GET(ch, 0, rdt->numhashbits);
	redup_entry_t **rdepp;

	rdepp = &(rdt->redup_hash_array[hashcode]);
	redup_entry_t *rde = umem_cache_alloc(rdt->ddecache, UMEM_NOFAIL);
	rde->rde_next = *rdepp;
	rde->rde_guid = guid;
	rde->rde_object = object;
	rde->rde_offset = offset;
	rde->rde_stream_offset = stream_offset;
	*rdepp = rde;
	rdt->ddt_count++;
}

static void
rdt_lookup(redup_table_t *rdt,
    uint64_t guid, uint64_t object, uint64_t offset,
    uint64_t *stream_offsetp)
{
	uint64_t ch = cityhash3(guid, object, offset);
	uint64_t hashcode = BF64_GET(ch, 0, rdt->numhashbits);

	for (redup_entry_t *rde = rdt->redup_hash_array[hashcode];
	    rde != NULL; rde = rde->rde_next) {
		if (rde->rde_guid == guid &&
		    rde->rde_object == object &&
		    rde->rde_offset == offset) {
			*stream_offsetp = rde->rde_stream_offset;
			return;
		}
	}
	assert(!"could not find expected redup table entry");
}

static disposition_t
chain_redup_writes(drr_packet_t *item, redup_context_t *context)
{
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
		 * drr_refobject, drr_refoffset.  Replace this
		 * record with the found WRITE record, but with
		 * drr_object,drr_offset,drr_toguid replaced with ours.
		 */
		uint64_t stream_offset = 0;
		rdt_lookup(&context->rc_rdt, drrwb.drr_refguid,
		    drrwb.drr_refobject, drrwb.drr_refoffset,
		    &stream_offset);

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

		item->dp_payload_size = DRR_WRITE_PAYLOAD_SIZE(drrw);
		item->dp_payload = safe_malloc(item->dp_payload_size);

		size_t n_read = fread(item->dp_payload, item->dp_payload_size,
		    1, context->rc_fp);
		if (n_read != 1)
			err(1, "read of prior payload failed");

		drrw->drr_toguid = drrwb.drr_toguid;
		drrw->drr_object = drrwb.drr_object;
		drrw->drr_offset = drrwb.drr_offset;
		break;
	}

	case DRR_WRITE:
		rdt_insert(&context->rc_rdt, drrw->drr_toguid, drrw->drr_object,
		    drrw->drr_offset, item->dp_stream_offset);
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
			.process = (zc_serial_process_f *)chain_redup_writes
		}
	};
	return (step);
}

int
zstream_do_redup(int argc, char *argv[])
{
	int c;
	chain_attrs_t attrs = {0};
	redup_context_t context = {0};
	uint64_t numbuckets;

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			break;
		case '?':
			warnx("invalid option '%c'", optopt);
			zstream_usage();
			break;
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

#ifdef _ILP32
	uint64_t max_rde_size = SMALLEST_POSSIBLE_MAX_RDT_MB << 20;
#else
	uint64_t physbytes = sysconf(_SC_PHYS_PAGES) * sysconf(_SC_PAGESIZE);
	uint64_t max_rde_size = MAX((physbytes * MAX_RDT_PHYSMEM_PERCENT) / 100,
	    SMALLEST_POSSIBLE_MAX_RDT_MB << 20);
#endif

	numbuckets = max_rde_size / (sizeof (redup_entry_t));
	if (!ISP2(numbuckets))
		numbuckets = 1ULL << highbit64(numbuckets);

	context.rc_rdt.redup_hash_array =
	    safe_calloc(numbuckets * sizeof (redup_entry_t *));
	context.rc_rdt.ddecache = umem_cache_create("rde",
	    sizeof (redup_entry_t), 0, NULL, NULL, NULL, NULL, NULL, 0);
	context.rc_rdt.numhashbits = highbit64(numbuckets) - 1;
	context.rc_rdt.ddt_count = 0;

	zstream_chain_t redup_chain = {
		STANDARD_INPUT_STACK(argv[0]),
		serial_redup_writes(&context),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(redup_chain, &attrs);

	if (attrs.ca_command_opts & CA_VERBOSE) {
		char mem_str[16];
		record_stats_t *acsi = attrs.ca_stats_in;
		zfs_nicenum(context.rc_rdt.ddt_count * sizeof (redup_entry_t),
		    mem_str, sizeof (mem_str));
		fprintf(stderr, "Converted stream with %llu total records, "
		    "including %llu dedup records, using %sB memory.\n",
		    (u_longlong_t)attrs.ca_totals_in.rs_num_records,
		    (u_longlong_t)acsi[DRR_WRITE_BYREF].rs_num_records,
		    mem_str);
	}

	fclose(context.rc_fp);
	umem_cache_destroy(context.rc_rdt.ddecache);
	free(context.rc_rdt.redup_hash_array);
	return (0);
}
