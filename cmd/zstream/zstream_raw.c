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
 * Copyright 2026 Klara, Inc.
 */

#ifdef __FreeBSD__
#include <sys/disk.h>
#endif
#include <sys/dmu.h>
#include <sys/ioctl.h>
#include <sys/spa.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <sys/zstd/zstd.h>
#include <sys/zvol.h>
#include <err.h>
#include <libnvpair.h>
#ifdef __linux__
#include <linux/falloc.h>
#include <linux/fs.h>
#endif
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"
#include "zstream_util.h"

/*
 * Supported feature flags (in drr_versioninfo)
 *
 * Add bits here to allow e.g. compression, large blocks, etc.
 */
#define	SUPPORTED_FEATURES (DMU_BACKUP_FEATURE_EMBED_DATA | \
    DMU_BACKUP_FEATURE_LZ4 | DMU_BACKUP_FEATURE_LARGE_BLOCKS | \
    DMU_BACKUP_FEATURE_COMPRESSED | DMU_BACKUP_FEATURE_ZSTD)

struct raw_context {
	boolean_t inprop;
	boolean_t inzvol;
	uint64_t guid;
};

static int raw_volume;
static size_t raw_volume_size;
static uint64_t raw_volume_sectorsize;
static boolean_t raw_volume_isreg;
static unsigned long raw_volume_freeop;

static long iov_max;
static long buffers_max = 1;
static long pagesize;
static void *zero_page;

/*
 * write_zeros - zero a region.
 */
static void
write_zeros(off_t offset, size_t length)
{
	static struct iovec *iov = NULL;
	int iovcnt = MIN(howmany(length, pagesize), iov_max);

	ASSERT3U(offset + length, >=, offset);

	if (iov == NULL)
		iov = safe_malloc(iov_max * sizeof (*iov));
	if (iovcnt == 0)
		return;

	size_t resid = length;
	while (resid > 0) {
		size_t iovsz = resid;
		int i;

		for (i = 0; i < iovcnt && resid > 0; i++) {
			iov[i].iov_base = zero_page;
			iov[i].iov_len = MIN(resid, pagesize);
			resid -= iov[i].iov_len;
		}
		ssize_t res = pwritev(raw_volume, iov, i, offset);
		if (res < 0)
			err(EXIT_FAILURE, "pwritev");
		iovsz -= resid;
		ASSERT3U(res, ==, iovsz);
		offset += iovsz;
	}
	ASSERT0(resid);
}

/*
 * buffer_write - pwrite with buffer vectoring and error handling
 *
 * Appends buf to a buffer vector, issuing the pending vector if not contiguous.
 * Ownership of buf is taken; it will be freed after issuing the write.
 */
static void
buffer_write(void *buf, size_t nbytes, off_t offset)
{
	static struct iovec *iov = NULL;
	static off_t position = 0;
	static size_t length = 0;
	static int iovcnt = 0;

	if (iov == NULL)
		iov = safe_calloc(buffers_max * sizeof (*iov));
	if (iovcnt == 0)
		position = offset;
	else if (position + length != offset || iovcnt == buffers_max) {
		ASSERT3U(offset + nbytes, >=, offset);
		ASSERT3U(iovcnt, >, 0);
		ssize_t res = pwritev(raw_volume, iov, iovcnt, position);
		if (res < 0)
			err(EXIT_FAILURE, "pwritev");
		ASSERT3U(res, ==, length);
		position = offset;
		length = 0;
		for (int i = 0; i < iovcnt; i++) {
			free(iov[i].iov_base);
			iov[i].iov_base = NULL;
		}
		iovcnt = 0;
	}
	if (buf == NULL) {
		/* Sentinel buf for cleanup. */
		for (int i = 0; i < buffers_max; i++)
			free(iov[i].iov_base);
	} else {
		iov[iovcnt].iov_base = buf;
		iov[iovcnt].iov_len = nbytes;
		length += nbytes;
		iovcnt++;
	}
}

static inline void
buffer_finish(void)
{
	buffer_write(NULL, 0, 0);
	if (fsync(raw_volume) != 0)
		err(EXIT_FAILURE, "fsync");
}

static inline void
resize(size_t size)
{
	if (ftruncate(raw_volume, size) < 0)
		err(EXIT_FAILURE, "ftruncate");
}

static inline void
free_tail(off_t offset)
{
	/*
	 * This style of FREE is frequently a large range covering most of the
	 * volume from the offset to the end.  Truncating the file and extending
	 * it back out works cheaply even on filesystems that do not support
	 * hole punching, avoiding the need to write zeros.
	 */
	resize(offset);
	if (offset < raw_volume_size)
		resize(raw_volume_size);
	else
		/* Volume size unknown, but it must be at least this big. */
		raw_volume_size = offset;
}

static inline boolean_t
punch_hole(off_t offset, size_t length)
{
#if defined(__FreeBSD__)
	struct spacectl_range range = { offset, length };
	return (fspacectl(raw_volume, SPACECTL_DEALLOC, &range, 0, NULL) == 0);
#elif defined(__linux__)
	int mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
	return (fallocate(raw_volume, mode, offset, length) == 0);
#else
	return (B_FALSE);
#endif
}

static boolean_t
free_blocks(off_t offset, size_t length)
{
	/*
	 * Ensure sector alignment in case the source zvol's sector size is
	 * smaller than the target device's.
	 *
	 * TODO: Optional secure erase.
	 */
	uint64_t start = P2ROUNDUP(offset, raw_volume_sectorsize);
	uint64_t limit = offset + length;
	uint64_t end = P2ALIGN_TYPED(limit, raw_volume_sectorsize, uint64_t);
	if (offset < start)
		write_zeros(offset, start - offset);
	if (start < end) {
#ifdef __FreeBSD__
		off_t range[2];
#else
		uint64_t range[2];
#endif

		range[0] = start;
		range[1] = end - start;
		if (ioctl(raw_volume, raw_volume_freeop, range) != 0) {
			ASSERT3U(errno, ==, EOPNOTSUPP);
			raw_volume_freeop = B_FALSE;
			return (B_FALSE);
		}
	}
	if (end < limit)
		write_zeros(end, limit - end);
	return (B_TRUE);
}

static void
free_range(off_t offset, size_t length)
{
	if (raw_volume_isreg) {
		if (length == (size_t)-1) {
			free_tail(offset);
			return;
		}
		static boolean_t punch_holes = B_TRUE;
		if (punch_holes) {
			if (punch_hole(offset, length))
				return;
			punch_holes = B_FALSE;
		}
	}
	if (length == (size_t)-1)
		length = raw_volume_size - offset;
	if (raw_volume_freeop && free_blocks(offset, length))
		return;
	/* If all else fails, the range must be zeroed the slow way. */
	write_zeros(offset, length);
}

/*
 * apply_properties - read the properties zap to adjust file size.
 *
 * Returns the value of the "size" property.
 */
static uint64_t
apply_properties(void *buf, size_t len)
{
	const mzap_phys_t *mzap = buf;

	ASSERT(raw_volume_isreg);
	ASSERT3U(len, >=, sizeof (*mzap));
	ASSERT3U(MZAP_ENT_LEN, ==, sizeof (mzap_ent_phys_t));

	if (mzap->mz_block_type == BSWAP_64(ZBT_MICRO))
		zap_byteswap(buf, len);

	ASSERT3U(mzap->mz_block_type, ==, ZBT_MICRO);
	ASSERT0(strcmp(mzap->mz_chunk[0].mze_name, "size"));

	uint64_t size = mzap->mz_chunk[0].mze_value;
	resize(size);
	return (size);
}

static disposition_t
chain_replay_raw(void *item_in, void *context_in)
{
	drr_packet_t *item = item_in;
	if (item == NULL)
		return (D_OK);

	struct raw_context *context = context_in;
	dmu_replay_record_t *drr = &item->dp_drr;

	switch (drr->drr_type) {
	case DRR_BEGIN: {
		struct drr_begin *drrb = &drr->drr_u.drr_begin;

		uint64_t featureflags, unsupported_features;
		featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);
		unsupported_features = featureflags & ~SUPPORTED_FEATURES;
		if (unsupported_features != 0)
			errx(EXIT_FAILURE, "unsupported stream features: "
			    "%#llx of %#llx, aborting...",
			    (u_longlong_t)unsupported_features,
			    (u_longlong_t)featureflags);

		int hdrtype = DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo);
		if (hdrtype == DMU_SUBSTREAM) {
			uint64_t guid = context->guid;
			if (guid != 0 && drrb->drr_fromguid != guid)
				errx(EXIT_FAILURE, "wrong fromguid: "
				    "%llu != %llu, aborting...",
				    (u_longlong_t)drrb->drr_fromguid,
				    (u_longlong_t)guid);
			context->guid = drrb->drr_toguid;
		}
		break;
	}
	case DRR_OBJECT: {
		struct drr_object *drro = &drr->drr_u.drr_object;

		context->inzvol = drro->drr_object == ZVOL_OBJ &&
		    drro->drr_type == DMU_OT_ZVOL;
		context->inprop = drro->drr_object == ZVOL_ZAP_OBJ &&
		    drro->drr_type == DMU_OT_ZVOL_PROP;
		break;
	}
	case DRR_WRITE: {
		struct drr_write *drrw = &drr->drr_u.drr_write;

		if (context->inzvol) {
			buffer_write(item->dp_payload, item->dp_payload_size,
			    drrw->drr_offset);
			/*
			 * The buffer is no longer owned by the chain.  We will
			 * free it when safe.
			 */
			item->dp_payload = NULL;
		} else if (raw_volume_isreg && context->inprop) {
			ASSERT0(drrw->drr_offset);
			raw_volume_size = apply_properties(item->dp_payload,
			    item->dp_payload_size);
		}
		break;
	}
	case DRR_FREE: {
		if (!context->inzvol)
			break;

		struct drr_free *drrf = &drr->drr_u.drr_free;
		free_range(drrf->drr_offset, drrf->drr_length);
		break;
	}
	case DRR_WRITE_EMBEDDED: {
		struct drr_write_embedded *drrwe =
		    &drr->drr_u.drr_write_embedded;

		if (!ctype_is_uncompressed(drrwe->drr_compression)) {
			uint8_t *buffer = item->dp_payload;
			uint32_t lsize = drrwe->drr_lsize;

			ASSERT3U(item->dp_payload_size, <=, lsize);

			item->dp_payload = decompress_buffer(buffer,
			    item->dp_payload_size, lsize,
			    drrwe->drr_compression);
			if (item->dp_payload == NULL)
				errx(EXIT_FAILURE,
				    "decompression failed at offset %llu",
				    (u_longlong_t)drrwe->drr_offset);
			item->dp_payload_size = lsize;
			free(buffer);
		}
		if (context->inzvol) {
			buffer_write(item->dp_payload, item->dp_payload_size,
			    drrwe->drr_offset);
			/*
			 * The buffer is no longer owned by the chain.  We will
			 * free it when safe.
			 */
			item->dp_payload = NULL;
		} else if (raw_volume_isreg && context->inprop) {
			ASSERT0(drrwe->drr_offset);
			raw_volume_size = apply_properties(item->dp_payload,
			    item->dp_payload_size);
		}
		break;
	}
	default:
		break;
	}
	return (D_OK);
}

int
zstream_do_raw(int argc, char *argv[])
{
	struct raw_context context = { 0 };
	chain_attrs_t attrs = { 0 };

	ENABLE_OPTION(&attrs, CA_FORBID_DEDUP);

	int c;
	while ((c = getopt(argc, argv, ":b:g:v")) != -1) {
		switch (c) {
		case 'b':
			buffers_max = strtol(optarg, NULL, 0);
			if (buffers_max <= 0) {
				warnx("invalid number of buffers");
				zstream_usage();
			}
			break;
		case 'g':
			context.guid = strtoull(optarg, NULL, 0);
			if (context.guid == 0) {
				warnx("invalid guid");
				zstream_usage();
			}
			break;
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			ENABLE_OPTION(&attrs, CA_DUMP_ALL_RECORDS);
			ENABLE_OPTION(&attrs, CA_DUMP_CHECKSUMS);
			break;
		case ':':
			warnx("missing argument for '%c' option", optopt);
			zstream_usage();
		case '?':
			warnx("invalid option '%c'", optopt);
			zstream_usage();
		}
	}
	argc -= optind;
	argv += optind;
	if (argc < 1) {
		warnx("missing path to raw volume");
		zstream_usage();
	}

	/*
	 * TODO: O_DIRECT, maybe as a command line flag? Avoid O_CREAT in /dev?
	 */
	const char *raw_path = argv[0];
	raw_volume = open(raw_path, O_WRONLY | O_CREAT, 0666);
	if (raw_volume < 0)
		err(EXIT_FAILURE, "error while opening file '%s'", raw_path);
	struct stat64 st;
	if (fstat64_blk(raw_volume, &st) < 0)
		err(EXIT_FAILURE, "fstat64_blk");
	raw_volume_size = st.st_size;
	raw_volume_isreg = S_ISREG(st.st_mode);
	if (!raw_volume_isreg) {
#if defined(__FreeBSD__)
		uint_t sectorsize;

		if (ioctl(raw_volume, DIOCGSECTORSIZE, &sectorsize) == 0) {
			raw_volume_sectorsize = sectorsize;
			raw_volume_freeop = DIOCGDELETE;
		}
#elif defined(__linux__)
		if (ioctl(raw_volume, BLKSSZGET, &raw_volume_sectorsize) == 0) {
			/* TODO: optional BLKSECDISCARD/BLKZEROOUT */
			raw_volume_freeop = BLKDISCARD;
		}
#endif
	}

	iov_max = sysconf(_SC_IOV_MAX);
	buffers_max = MIN(buffers_max, iov_max);
	pagesize = sysconf(_SC_PAGESIZE);
	zero_page = safe_calloc(pagesize);

	uint32_t drop_mask = DROP_END | DROP_FREEOBJECTS | DROP_OBJECT_RANGE |
	    DROP_REDACT | DROP_SPILL;
	zstream_chain_t raw_chain = {
		STANDARD_INPUT_STACK((argc > 1) ? argv[1] : NULL),
		serial_dump_records(),
		serial_drop_record_types(drop_mask),
		parallel_decompress_writes(NULL),
		{
			.cs_type = CS_SERIAL,
			.cs_in_size = sizeof (drr_packet_t),
			.cs_out_size = sizeof (drr_packet_t),
			.cs_context = &context,
			.cs_serial = {.process = chain_replay_raw},
		},
		NULL_OUTPUT_STACK()
	};
	zstream_chain_exec(raw_chain, &attrs);

	buffer_finish();

	if (OPTION_ENABLED(CA_VERBOSE))
		(void) printf("now at guid %llu\n", (u_longlong_t)context.guid);
	else
		(void) printf("%llu\n", (u_longlong_t)context.guid);

	return (EXIT_SUCCESS);
}
