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
 */
#define	SUPPORTED_FEATURES (DMU_BACKUP_FEATURE_EMBED_DATA | \
    DMU_BACKUP_FEATURE_LZ4 | DMU_BACKUP_FEATURE_LARGE_BLOCKS | \
    DMU_BACKUP_FEATURE_COMPRESSED | DMU_BACKUP_FEATURE_ZSTD)

typedef struct {
	struct raw_stream {
		uint64_t	guid;
		boolean_t	inprop;
		boolean_t	inzvol;
	} stream;
	struct raw_volume {
		int		fd;
		size_t		size;
		uint64_t	sectorsize;
		boolean_t	isreg;
		boolean_t	punch_holes;
		unsigned long	freeop;
	} volume;
	struct raw_limits {
		long		iov_max;
		long		buffers_max;
		long		pagesize;
	} limits;
	struct raw_zeros {
		struct iovec	*iov;
	} zeros;
	struct raw_buffer {
		struct iovec	*iov;
		off_t		position;
		size_t		length;
		int		iovcnt;
	} buffer;
} raw_context_t;

static void
write_zeros(raw_context_t *context, off_t offset, size_t length)
{
	ASSERT3U(offset + length, >=, offset);

	struct raw_limits *limits = &context->limits;
	int iovcnt = MIN(howmany(length, limits->pagesize), limits->iov_max);
	if (iovcnt == 0)
		return;

	int fd = context->volume.fd;
	long pagesize = limits->pagesize;
	struct iovec *iov = context->zeros.iov;
	size_t resid = length;
	while (resid > 0) {
		size_t iovsz = resid;
		int i;

		for (i = 0; i < iovcnt && resid > 0; i++) {
			iov[i].iov_len = MIN(resid, pagesize);
			resid -= iov[i].iov_len;
		}
		ssize_t res = pwritev(fd, iov, i, offset);
		if (res < 0)
			err(EXIT_FAILURE, "pwritev");
		iovsz -= resid;
		VERIFY3U(res, ==, iovsz);
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
buffer_write(raw_context_t *context, void *buf, size_t nbytes, off_t offset)
{
	struct raw_limits *limits = &context->limits;
	struct raw_buffer *buffer = &context->buffer;
	struct iovec *iov = buffer->iov;

	if (buffer->iovcnt == 0)
		buffer->position = offset;
	else if (buffer->position + buffer->length != offset ||
	    buffer->iovcnt == limits->buffers_max) {
		ASSERT3U(offset + nbytes, >=, offset);
		ASSERT3U(buffer->iovcnt, >, 0);
		ssize_t res = pwritev(context->volume.fd, iov, buffer->iovcnt,
		    buffer->position);
		if (res < 0)
			err(EXIT_FAILURE, "pwritev");
		VERIFY3U(res, ==, buffer->length);
		buffer->position = offset;
		buffer->length = 0;
		for (int i = 0; i < buffer->iovcnt; i++) {
			free(iov[i].iov_base);
			iov[i].iov_base = NULL;
		}
		buffer->iovcnt = 0;
	}
	if (buf == NULL) {
		/* Sentinel buf for flush. */
		ASSERT0(buffer->iovcnt);
		return;
	}
	iov[buffer->iovcnt].iov_base = buf;
	iov[buffer->iovcnt].iov_len = nbytes;
	buffer->length += nbytes;
	buffer->iovcnt++;
}

static inline void
buffer_finish(raw_context_t *context)
{
	buffer_write(context, NULL, 0, 0);
	if (fsync(context->volume.fd) != 0)
		err(EXIT_FAILURE, "fsync");
}

static inline void
resize(raw_context_t *context, size_t size)
{
	if (ftruncate(context->volume.fd, size) < 0)
		err(EXIT_FAILURE, "ftruncate");
}

static inline void
free_tail(raw_context_t *context, off_t offset)
{
	/*
	 * This style of FREE is frequently a large range covering most of the
	 * volume from the offset to the end.  Truncating the file and extending
	 * it back out works cheaply even on filesystems that do not support
	 * hole punching, avoiding the need to write zeros.
	 */
	resize(context, offset);
	if (offset < context->volume.size)
		resize(context, context->volume.size);
	else
		/* Volume size unknown, but it must be at least this big. */
		context->volume.size = offset;
}

static inline boolean_t
punch_hole(raw_context_t *context, off_t offset, size_t length)
{
	int fd = context->volume.fd;

#if defined(__FreeBSD__)
	struct spacectl_range range = { offset, length };
	return (fspacectl(fd, SPACECTL_DEALLOC, &range, 0, NULL) == 0);
#elif defined(__linux__)
	int mode = FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE;
	return (fallocate(fd, mode, offset, length) == 0);
#else
	(void) fd, (void) offset, (void) length;
	return (B_FALSE);
#endif
}

static boolean_t
free_blocks(raw_context_t *context, off_t offset, size_t length)
{
	/*
	 * Ensure sector alignment in case the source zvol's sector size is
	 * smaller than the target device's.
	 *
	 * TODO: Optional secure erase.
	 */
	uint64_t sectorsize = context->volume.sectorsize;
	uint64_t start = P2ROUNDUP(offset, sectorsize);
	uint64_t limit = offset + length;
	uint64_t end = P2ALIGN_TYPED(limit, sectorsize, uint64_t);
	if (offset < start)
		write_zeros(context, offset, start - offset);
	if (start < end) {
#ifdef __FreeBSD__
		off_t range[2];
#else
		uint64_t range[2];
#endif
		int fd = context->volume.fd;

		range[0] = start;
		range[1] = end - start;
		if (ioctl(fd, context->volume.freeop, range) != 0) {
			ASSERT3U(errno, ==, EOPNOTSUPP);
			context->volume.freeop = B_FALSE;
			return (B_FALSE);
		}
	}
	if (end < limit)
		write_zeros(context, end, limit - end);
	return (B_TRUE);
}

static void
free_range(raw_context_t *context, off_t offset, size_t length)
{
	if (context->volume.isreg) {
		if (length == (size_t)-1) {
			free_tail(context, offset);
			return;
		}
		if (context->volume.punch_holes) {
			if (punch_hole(context, offset, length))
				return;
			context->volume.punch_holes = B_FALSE;
		}
	}
	if (length == (size_t)-1)
		length = context->volume.size - offset;
	if (context->volume.freeop && free_blocks(context, offset, length))
		return;
	/* If all else fails, the range must be zeroed the slow way. */
	write_zeros(context, offset, length);
}

/*
 * apply_properties - read the properties zap to adjust file size.
 *
 * Returns the value of the "size" property.
 */
static uint64_t
apply_properties(raw_context_t *context, void *buf, size_t len)
{
	const mzap_phys_t *mzap = buf;

	ASSERT(context->volume.isreg);
	ASSERT3U(len, >=, sizeof (*mzap));
	ASSERT3U(MZAP_ENT_LEN, ==, sizeof (mzap_ent_phys_t));

	if (mzap->mz_block_type == BSWAP_64(ZBT_MICRO))
		zap_byteswap(buf, len);

	ASSERT3U(mzap->mz_block_type, ==, ZBT_MICRO);
	ASSERT0(strcmp(mzap->mz_chunk[0].mze_name, "size"));

	uint64_t size = mzap->mz_chunk[0].mze_value;
	resize(context, size);
	return (size);
}

static disposition_t
chain_replay_raw(void *item_in, void *context_in)
{
	drr_packet_t *item = item_in;
	if (item == NULL)
		return (D_OK);

	raw_context_t *context = context_in;
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
			uint64_t guid = context->stream.guid;
			if (guid != 0 && drrb->drr_fromguid != guid)
				errx(EXIT_FAILURE, "wrong fromguid: "
				    "%llu != %llu, aborting...",
				    (u_longlong_t)drrb->drr_fromguid,
				    (u_longlong_t)guid);
			context->stream.guid = drrb->drr_toguid;
		}
		break;
	}
	case DRR_OBJECT: {
		struct drr_object *drro = &drr->drr_u.drr_object;

		context->stream.inzvol = drro->drr_object == ZVOL_OBJ &&
		    drro->drr_type == DMU_OT_ZVOL;
		context->stream.inprop = drro->drr_object == ZVOL_ZAP_OBJ &&
		    drro->drr_type == DMU_OT_ZVOL_PROP;
		break;
	}
	case DRR_WRITE: {
		struct drr_write *drrw = &drr->drr_u.drr_write;

		if (context->stream.inzvol) {
			buffer_write(context, item->dp_payload,
			    item->dp_payload_size, drrw->drr_offset);
			/*
			 * The buffer is no longer owned by the chain.  We will
			 * free it when safe.
			 */
			export_payload(item);
		} else if (context->volume.isreg && context->stream.inprop) {
			ASSERT0(drrw->drr_offset);
			context->volume.size = apply_properties(context,
			    item->dp_payload, item->dp_payload_size);
		}
		break;
	}
	case DRR_FREE: {
		if (!context->stream.inzvol)
			break;

		struct drr_free *drrf = &drr->drr_u.drr_free;
		free_range(context, drrf->drr_offset, drrf->drr_length);
		break;
	}
	case DRR_WRITE_EMBEDDED: {
		struct drr_write_embedded *drrwe =
		    &drr->drr_u.drr_write_embedded;

		if (!ctype_is_uncompressed(drrwe->drr_compression)) {
			VERIFY3U(item->dp_payload_size, <=, drrwe->drr_lsize);
			uint8_t *debuff = decompress_buffer(item->dp_payload,
			    item->dp_payload_size, drrwe->drr_lsize,
			    drrwe->drr_compression);
			if (debuff == NULL)
				errx(EXIT_FAILURE,
				    "decompression failed at offset %llu",
				    (u_longlong_t)drrwe->drr_offset);
			set_payload(item, debuff, drrwe->drr_lsize);
		}
		if (context->stream.inzvol) {
			buffer_write(context, item->dp_payload,
			    item->dp_payload_size, drrwe->drr_offset);
			/*
			 * The buffer is no longer owned by the chain.  We will
			 * free it when safe.
			 */
			export_payload(item);
		} else if (context->volume.isreg && context->stream.inprop) {
			ASSERT0(drrwe->drr_offset);
			context->volume.size = apply_properties(context,
			    item->dp_payload, item->dp_payload_size);
		}
		break;
	}
	default:
		break;
	}
	return (D_OK);
}

/* Keep this small enough to not accidentally run systems out of memory. */
#define	BUFFERS_MAX_DEFAULT 32

int
zstream_do_raw(int argc, char *argv[])
{
	raw_context_t context = { 0 };
	context.limits.buffers_max = BUFFERS_MAX_DEFAULT;

	chain_attrs_t attrs = { 0 };
	ENABLE_OPTION(&attrs, CA_FORBID_DEDUP);

	int c;
	while ((c = getopt(argc, argv, ":b:g:v")) != -1) {
		switch (c) {
		case 'b':
			context.limits.buffers_max = strtol(optarg, NULL, 0);
			if (context.limits.buffers_max <= 0) {
				warnx("invalid number of buffers");
				zstream_usage();
			}
			break;
		case 'g':
			context.stream.guid = strtoull(optarg, NULL, 0);
			if (context.stream.guid == 0) {
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

	const char *raw_path = argv[0];
	int fd = open(raw_path, O_WRONLY | O_CREAT, 0666);
	if (fd < 0)
		err(EXIT_FAILURE, "error while opening file '%s'", raw_path);
	context.volume.fd = fd;
	struct stat64 st;
	if (fstat64_blk(fd, &st) < 0)
		err(EXIT_FAILURE, "fstat64_blk");
	context.volume.size = st.st_size;
	context.volume.isreg = S_ISREG(st.st_mode);
	if (!context.volume.isreg) {
#if defined(__FreeBSD__)
		uint_t sectorsize;

		if (ioctl(fd, DIOCGSECTORSIZE, &sectorsize) == 0) {
			context.volume.sectorsize = sectorsize;
			context.volume.freeop = DIOCGDELETE;
		}
#elif defined(__linux__)
		if (ioctl(fd, BLKSSZGET, &context.volume.sectorsize) == 0) {
			/* TODO: optional BLKSECDISCARD/BLKZEROOUT */
			context.volume.freeop = BLKDISCARD;
		}
#endif
	}

	long iov_max = sysconf(_SC_IOV_MAX);
	long pagesize = sysconf(_SC_PAGESIZE);

	context.limits.iov_max = iov_max;
	context.limits.buffers_max = MIN(context.limits.buffers_max, iov_max);
	context.limits.pagesize = pagesize;

	context.buffer.iov = safe_calloc(context.limits.buffers_max *
	    sizeof (struct iovec));

	context.zeros.iov = safe_malloc(iov_max * sizeof (struct iovec));
	void *zero_page = safe_calloc(pagesize);
	for (int i = 0; i < iov_max; i++)
		context.zeros.iov[i].iov_base = zero_page;

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

	buffer_finish(&context);
	free(zero_page);
	free(context.zeros.iov);
	free(context.buffer.iov);

	if (OPTION_ENABLED(CA_VERBOSE))
		(void) printf("now at guid %llu\n",
		    (u_longlong_t)context.stream.guid);
	else
		(void) printf("%llu\n", (u_longlong_t)context.stream.guid);

	return (EXIT_SUCCESS);
}
