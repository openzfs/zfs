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
 * Copyright (c) 2026 by Will Rouesnel. All rights reserved.
 */

#include <err.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <string.h>
#include <sys/dmu_send.h>
#include <sys/nvpair.h>
#include <sys/stdtypes.h>
#include <sys/sysmacros.h>
#include <sys/zfs_ioctl.h>
#include <unistd.h>

#include "zstream.h"
#include "zstream_modules.h"
#include "zstream_util.h"

/*
 * zstream resume converts a complete (or partial) saved send stream into the
 * stream that "zfs send -t" would have generated for a given receive resume
 * token. Only the records at or after the resume point are passed through,
 * and the DRR_BEGIN record is marked as resuming so that "zfs receive" will
 * accept it into the partially received dataset.
 *
 * The token's "bytes" field is not used to locate the resume point. It
 * excludes the DRR_BEGIN header, accumulates across successive resumed
 * receives, and can be held back by records the receiver deferred, so it
 * does not reliably correspond to an offset in the saved stream. Instead,
 * records are matched against the token's object and offset, mirroring the
 * logic that the kernel uses when it generates a resumed stream.
 */

/*
 * Stream features that are recorded in the resume token and must agree with
 * the saved stream. See dmu_recv_begin_sync().
 */
#define	RESUME_FEATURES	(DMU_BACKUP_FEATURE_LARGE_BLOCKS |		\
	DMU_BACKUP_FEATURE_EMBED_DATA | DMU_BACKUP_FEATURE_COMPRESSED |	\
	DMU_BACKUP_FEATURE_RAW)

/* The nvh_endian value of a natively packed nvlist from this system */
#if defined(_ZFS_LITTLE_ENDIAN)
#define	NATIVE_NVL_ENDIAN	1
#else
#define	NATIVE_NVL_ENDIAN	0
#endif

typedef struct {
	uint64_t	rc_object;
	uint64_t	rc_offset;
	uint64_t	rc_toguid;
	uint64_t	rc_fromguid;
	uint64_t	rc_features;
	boolean_t	rc_seen_begin;
	boolean_t	rc_seen_end;
	boolean_t	rc_found;	/* saw the record named by the token */
	boolean_t	rc_resumed;	/* passed a record after the BEGIN */
	off_t		rc_resume_stream_offset;
	uint64_t	rc_dropped;	/* records dropped by the chain step */
	uint64_t	rc_skipped;	/* records the reader skipped */
} resume_context_t;

/*
 * Compare a stream position against the resume point.
 */
static inline int
resume_cmp(const resume_context_t *rc, uint64_t object, uint64_t offset)
{
	if (object != rc->rc_object)
		return (object < rc->rc_object ? -1 : 1);
	if (offset != rc->rc_offset)
		return (offset < rc->rc_offset ? -1 : 1);
	return (0);
}

static inline uint64_t
saturating_end(uint64_t start, uint64_t length)
{
	return (length > UINT64_MAX - start ? UINT64_MAX : start + length);
}

static void
parse_resume_token(const char *token, resume_context_t *rc)
{
	require_libzfs();
	nvlist_t *nvl = zfs_send_resume_token_to_nvlist(libzfs_handle, token);
	if (nvl == NULL) {
		errx(1, "unable to parse resume token: %s",
		    libzfs_error_description(libzfs_handle));
	}

	if (nvlist_lookup_uint64(nvl, "object", &rc->rc_object) != 0 ||
	    nvlist_lookup_uint64(nvl, "offset", &rc->rc_offset) != 0 ||
	    nvlist_lookup_uint64(nvl, "toguid", &rc->rc_toguid) != 0) {
		errx(1, "resume token is corrupt");
	}
	rc->rc_fromguid = 0;
	(void) nvlist_lookup_uint64(nvl, "fromguid", &rc->rc_fromguid);

	rc->rc_features = 0;
	if (nvlist_exists(nvl, "largeblockok"))
		rc->rc_features |= DMU_BACKUP_FEATURE_LARGE_BLOCKS;
	if (nvlist_exists(nvl, "embedok"))
		rc->rc_features |= DMU_BACKUP_FEATURE_EMBED_DATA;
	if (nvlist_exists(nvl, "compressok"))
		rc->rc_features |= DMU_BACKUP_FEATURE_COMPRESSED;
	if (nvlist_exists(nvl, "rawok"))
		rc->rc_features |= DMU_BACKUP_FEATURE_RAW;

	fnvlist_free(nvl);
	release_libzfs();
}

/*
 * Framing of an XDR-encoded packed nvlist (see nvs_xdr_nvlist() and
 * nvs_xdr_nvpair()): a 4-byte stream header, the list version and flags,
 * then a sequence of pairs, each starting with its total encoded length and
 * its decoded length, terminated by a pair whose lengths are both zero.
 * All integers are 4-byte big-endian.
 */
#define	XDR_NVL_HDR_SIZE	(4 + 4 + 4)
#define	XDR_NVP_HDR_SIZE	(4 + 4 + 4)	/* lengths, name length */
#define	XDR_NVL_END_SIZE	(4 + 4)

static inline uint32_t
xdr_get32(const uint8_t *p)
{
	return (((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	    ((uint32_t)p[2] << 8) | (uint32_t)p[3]);
}

/*
 * Return the length of the XDR nvpair at buf[off], or 0 for the list
 * terminator. If name is not NULL, report whether the pair has that name.
 */
static size_t
xdr_nvpair_len(const uint8_t *buf, size_t len, size_t off,
    const char *name, boolean_t *match)
{
	if (off + XDR_NVL_END_SIZE > len)
		errx(1, "DRR_BEGIN payload is truncated");
	uint32_t encode_len = xdr_get32(buf + off);
	if (encode_len == 0)
		return (0);
	if (encode_len < XDR_NVP_HDR_SIZE || encode_len % 4 != 0 ||
	    encode_len > len - off - XDR_NVL_END_SIZE) {
		errx(1, "DRR_BEGIN payload is corrupt");
	}
	if (name != NULL) {
		uint32_t name_len = xdr_get32(buf + off + 8);
		*match = (name_len == strlen(name) &&
		    name_len <= encode_len - XDR_NVP_HDR_SIZE &&
		    memcmp(buf + off + XDR_NVP_HDR_SIZE, name, name_len) == 0);
	}
	return (encode_len);
}

static boolean_t
is_resume_nvpair(const uint8_t *buf, size_t len, size_t off)
{
	boolean_t object, offset;
	(void) xdr_nvpair_len(buf, len, off, BEGINNV_RESUME_OBJECT, &object);
	(void) xdr_nvpair_len(buf, len, off, BEGINNV_RESUME_OFFSET, &offset);
	return (object || offset);
}

/*
 * Insert the pairs of the packed nvlist "add" into the packed DRR_BEGIN
 * payload, replacing any existing resume pairs. The original pairs are
 * copied verbatim rather than being unpacked and repacked: the userland
 * XDR encoder sign-extends the elements of uint8 arrays (such as the wrapped
 * keys of a raw stream), producing an nvlist that the kernel will not
 * decode. The new pairs go before "crypt_keydata", matching the layout of
 * the stream that the kernel generates. As in the kernel, the result is
 * zero-padded to a multiple of 8 bytes.
 */
static char *
splice_resume_pairs(const void *orig_in, size_t orig_len,
    const void *add_in, size_t add_len, size_t *out_len)
{
	const uint8_t *orig = orig_in;
	const uint8_t *add = add_in;

	if (orig_len == 0) {
		*out_len = P2ROUNDUP(add_len, 8);
		char *out = safe_calloc(*out_len);
		memcpy(out, add, add_len);
		return (out);
	}
	if (orig_len < XDR_NVL_HDR_SIZE + XDR_NVL_END_SIZE)
		errx(1, "DRR_BEGIN payload is truncated");
	if (orig[0] != NV_ENCODE_XDR)
		errx(1, "DRR_BEGIN payload is not XDR encoded");

	const uint8_t *add_pairs = add + XDR_NVL_HDR_SIZE;
	size_t add_pairs_len = add_len - XDR_NVL_HDR_SIZE - XDR_NVL_END_SIZE;

	uint8_t *out = safe_calloc(P2ROUNDUP(orig_len + add_pairs_len,
	    8));
	memcpy(out, orig, XDR_NVL_HDR_SIZE);
	size_t pos = XDR_NVL_HDR_SIZE;
	boolean_t inserted = B_FALSE;

	size_t off = XDR_NVL_HDR_SIZE, pair_len;
	boolean_t is_keydata;
	while ((pair_len = xdr_nvpair_len(orig, orig_len, off, "crypt_keydata",
	    &is_keydata)) != 0) {
		if (is_keydata && !inserted) {
			memcpy(out + pos, add_pairs, add_pairs_len);
			pos += add_pairs_len;
			inserted = B_TRUE;
		}
		if (!is_resume_nvpair(orig, orig_len, off)) {
			memcpy(out + pos, orig + off, pair_len);
			pos += pair_len;
		}
		off += pair_len;
	}
	if (!inserted) {
		memcpy(out + pos, add_pairs, add_pairs_len);
		pos += add_pairs_len;
	}
	/* The terminator and padding are already zero */
	*out_len = P2ROUNDUP(pos + XDR_NVL_END_SIZE, 8);
	return ((char *)out);
}

/*
 * Rebuild a natively encoded DRR_BEGIN payload with the token's resume pairs,
 * replacing any existing ones. Released versions of OpenZFS pack the payload
 * in the native encoding, which, unlike XDR, can be safely unpacked and
 * repacked. The pairs are placed as in splice_resume_pairs().
 */
static char *
repack_native_payload(nvlist_t *nvl, const resume_context_t *rc,
    size_t *out_len)
{
	nvlist_t *out = fnvlist_alloc();
	boolean_t inserted = B_FALSE;

	for (nvpair_t *pair = nvlist_next_nvpair(nvl, NULL); pair != NULL;
	    pair = nvlist_next_nvpair(nvl, pair)) {
		const char *name = nvpair_name(pair);

		if (!inserted && strcmp(name, "crypt_keydata") == 0) {
			fnvlist_add_uint64(out, BEGINNV_RESUME_OBJECT,
			    rc->rc_object);
			fnvlist_add_uint64(out, BEGINNV_RESUME_OFFSET,
			    rc->rc_offset);
			inserted = B_TRUE;
		}
		if (strcmp(name, BEGINNV_RESUME_OBJECT) != 0 &&
		    strcmp(name, BEGINNV_RESUME_OFFSET) != 0)
			fnvlist_add_nvpair(out, pair);
	}
	if (!inserted) {
		fnvlist_add_uint64(out, BEGINNV_RESUME_OBJECT, rc->rc_object);
		fnvlist_add_uint64(out, BEGINNV_RESUME_OFFSET, rc->rc_offset);
	}

	char *buf = NULL;
	VERIFY0(nvlist_pack(out, &buf, out_len, NV_ENCODE_NATIVE, 0));
	fnvlist_free(out);
	return (buf);
}

/*
 * Check that the DRR_BEGIN record describes the stream that the token was
 * issued for, then mark it as resuming.
 */
static void
resume_begin(drr_packet_t *item, resume_context_t *rc)
{
	struct drr_begin *drrb = &item->dp_drr.drr_u.drr_begin;
	uint64_t featureflags = DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);

	if (rc->rc_seen_begin || item->dp_stream_offset != 0) {
		errx(1, "unexpected DRR_BEGIN record at offset %llu",
		    (u_longlong_t)item->dp_stream_offset);
	}
	rc->rc_seen_begin = B_TRUE;

	if (DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
	    DMU_COMPOUNDSTREAM) {
		errx(1, "compound (replication) streams cannot be resumed");
	}
	if (drrb->drr_toguid != rc->rc_toguid) {
		errx(1, "stream toguid %llu does not match resume token "
		    "toguid %llu", (u_longlong_t)drrb->drr_toguid,
		    (u_longlong_t)rc->rc_toguid);
	}
	if (drrb->drr_fromguid != rc->rc_fromguid) {
		errx(1, "stream fromguid %llu does not match resume token "
		    "fromguid %llu", (u_longlong_t)drrb->drr_fromguid,
		    (u_longlong_t)rc->rc_fromguid);
	}
	if ((featureflags & RESUME_FEATURES) != rc->rc_features) {
		errx(1, "stream feature flags 0x%llx do not match the "
		    "features recorded in the resume token (0x%llx)",
		    (u_longlong_t)(featureflags & RESUME_FEATURES),
		    (u_longlong_t)rc->rc_features);
	}

	nvlist_t *nvl;
	const uint8_t *nvh = item->dp_payload;
	boolean_t native = (item->dp_payload_size > 0 &&
	    nvh[0] == NV_ENCODE_NATIVE);
	if (item->dp_payload_size > 0) {
		if (nvlist_unpack((char *)item->dp_payload,
		    item->dp_payload_size, &nvl, 0) != 0) {
			if (native && item->dp_payload_size > 1 &&
			    nvh[1] != NATIVE_NVL_ENDIAN) {
				errx(1, "DRR_BEGIN payload was packed in the "
				    "native encoding of a system with the "
				    "opposite byte order, and cannot be "
				    "decoded on this system");
			}
			errx(1, "unable to unpack DRR_BEGIN payload");
		}
	} else {
		nvl = fnvlist_alloc();
	}

	/*
	 * The saved stream may itself be a resumed stream, in which case it
	 * can serve only resume points at or after its own.
	 */
	uint64_t prev_object, prev_offset;
	if ((featureflags & DMU_BACKUP_FEATURE_RESUMING) &&
	    nvlist_lookup_uint64(nvl, BEGINNV_RESUME_OBJECT,
	    &prev_object) == 0 &&
	    nvlist_lookup_uint64(nvl, BEGINNV_RESUME_OFFSET,
	    &prev_offset) == 0 &&
	    resume_cmp(rc, prev_object, prev_offset) > 0) {
		errx(1, "stream was resumed at object %llu offset %llu, "
		    "which is past the token's resume point",
		    (u_longlong_t)prev_object, (u_longlong_t)prev_offset);
	}

	size_t size;
	char *payload;
	if (native) {
		payload = repack_native_payload(nvl, rc, &size);
	} else {
		nvlist_t *resume_nvl = fnvlist_alloc();
		fnvlist_add_uint64(resume_nvl, BEGINNV_RESUME_OBJECT,
		    rc->rc_object);
		fnvlist_add_uint64(resume_nvl, BEGINNV_RESUME_OFFSET,
		    rc->rc_offset);
		char *resume_buf = NULL;
		size_t resume_size = 0;
		VERIFY0(nvlist_pack(resume_nvl, &resume_buf, &resume_size,
		    NV_ENCODE_XDR, 0));
		fnvlist_free(resume_nvl);

		payload = splice_resume_pairs(item->dp_payload,
		    item->dp_payload_size, resume_buf, resume_size, &size);
		free(resume_buf);
	}
	fnvlist_free(nvl);

	set_payload(item, payload, size);
	item->dp_drr.drr_payloadlen = size;

	featureflags |= DMU_BACKUP_FEATURE_RESUMING;
	DMU_SET_FEATUREFLAGS(drrb->drr_versioninfo, featureflags);
}

/*
 * Decide whether a record lies entirely before the resume point, so that it
 * can be dropped. Records that straddle the resume point are kept; the
 * receiver applies them idempotently. This must depend only on the record
 * header and the token, as the stream reader also uses it to seek past
 * records without reading them.
 */
static boolean_t
resume_before_point(const dmu_replay_record_t *drr,
    const resume_context_t *rc)
{
	const struct drr_object *drro = &drr->drr_u.drr_object;
	const struct drr_freeobjects *drrfo = &drr->drr_u.drr_freeobjects;
	const struct drr_write *drrw = &drr->drr_u.drr_write;
	const struct drr_free *drrf = &drr->drr_u.drr_free;
	const struct drr_spill *drrs = &drr->drr_u.drr_spill;
	const struct drr_write_embedded *drrwe =
	    &drr->drr_u.drr_write_embedded;
	const struct drr_object_range *drror = &drr->drr_u.drr_object_range;
	const struct drr_redact *drrr = &drr->drr_u.drr_redact;
	uint64_t end;

	switch (drr->drr_type) {
	case DRR_END:
		return (B_FALSE);

	case DRR_OBJECT:
		return (drro->drr_object < rc->rc_object);

	case DRR_FREEOBJECTS:
		end = saturating_end(drrfo->drr_firstobj, drrfo->drr_numobjs);
		return (end <= rc->rc_object);

	case DRR_OBJECT_RANGE:
		end = saturating_end(drror->drr_firstobj, drror->drr_numslots);
		return (end <= rc->rc_object);

	case DRR_WRITE:
		return (resume_cmp(rc, drrw->drr_object,
		    drrw->drr_offset) < 0);

	case DRR_WRITE_EMBEDDED:
		return (resume_cmp(rc, drrwe->drr_object,
		    drrwe->drr_offset) < 0);

	case DRR_FREE:
		end = saturating_end(drrf->drr_offset, drrf->drr_length);
		return (drrf->drr_object < rc->rc_object ||
		    (drrf->drr_object == rc->rc_object &&
		    end <= rc->rc_offset));

	case DRR_REDACT:
		end = saturating_end(drrr->drr_offset, drrr->drr_length);
		return (drrr->drr_object < rc->rc_object ||
		    (drrr->drr_object == rc->rc_object &&
		    end <= rc->rc_offset));

	case DRR_SPILL:
		return (drrs->drr_object < rc->rc_object);

	default:
		errx(1, "unexpected record type %u in stream",
		    (unsigned)drr->drr_type);
	}
}

/*
 * Skip function for the stream reader. Counts the records it skips, which
 * includes the one that the reader hands on for checksum resynchronization.
 */
static boolean_t
resume_skip_record(const dmu_replay_record_t *drr, void *arg)
{
	resume_context_t *rc = arg;

	if (!resume_before_point(drr, rc))
		return (B_FALSE);
	rc->rc_skipped++;
	return (B_TRUE);
}

/*
 * Decide whether to keep a record, noting whether the record named by the
 * token was seen. FREEOBJECTS records are trimmed, as the kernel never sends
 * frees for objects before the resume object.
 */
static boolean_t
resume_keep_record(dmu_replay_record_t *drr, resume_context_t *rc)
{
	struct drr_freeobjects *drrfo = &drr->drr_u.drr_freeobjects;
	uint64_t end;

	if (resume_before_point(drr, rc))
		return (B_FALSE);

	switch (drr->drr_type) {
	case DRR_END:
		rc->rc_seen_end = B_TRUE;
		break;

	case DRR_OBJECT:
		/* The receiver records offset 0 for OBJECT records */
		if (drr->drr_u.drr_object.drr_object == rc->rc_object &&
		    rc->rc_offset == 0)
			rc->rc_found = B_TRUE;
		break;

	case DRR_FREEOBJECTS:
		end = saturating_end(drrfo->drr_firstobj, drrfo->drr_numobjs);
		if (drrfo->drr_firstobj < rc->rc_object) {
			drrfo->drr_numobjs = end - rc->rc_object;
			drrfo->drr_firstobj = rc->rc_object;
		}
		break;

	case DRR_WRITE:
		if (resume_cmp(rc, drr->drr_u.drr_write.drr_object,
		    drr->drr_u.drr_write.drr_offset) == 0)
			rc->rc_found = B_TRUE;
		break;

	case DRR_WRITE_EMBEDDED:
		if (resume_cmp(rc, drr->drr_u.drr_write_embedded.drr_object,
		    drr->drr_u.drr_write_embedded.drr_offset) == 0)
			rc->rc_found = B_TRUE;
		break;

	default:
		break;
	}
	return (B_TRUE);
}

static disposition_t
chain_resume(void *item_in, void *context_in)
{
	drr_packet_t *item = (drr_packet_t *)item_in;
	resume_context_t *rc = (resume_context_t *)context_in;

	if (item == NULL) {
		if (!rc->rc_seen_begin)
			errx(1, "stream contains no DRR_BEGIN record");
		if (!rc->rc_resumed) {
			errx(1, "stream ends before the resume point "
			    "(object %llu offset %llu)",
			    (u_longlong_t)rc->rc_object,
			    (u_longlong_t)rc->rc_offset);
		}
		if (!rc->rc_found) {
			warnx("warning - no record at the resume point "
			    "(object %llu offset %llu) was found in the "
			    "stream", (u_longlong_t)rc->rc_object,
			    (u_longlong_t)rc->rc_offset);
		}
		if (!rc->rc_seen_end) {
			warnx("warning - input ended without a DRR_END "
			    "record; the resumed stream is incomplete");
		}
		if (OPTION_ENABLED(CA_VERBOSE)) {
			(void) fprintf(stderr, "resumed at object %llu "
			    "offset %llu, input stream offset %llu; "
			    "%llu records skipped\n",
			    (u_longlong_t)rc->rc_object,
			    (u_longlong_t)rc->rc_offset,
			    (u_longlong_t)rc->rc_resume_stream_offset,
			    (u_longlong_t)(rc->rc_dropped + rc->rc_skipped));
		}
		return (D_OK);
	}

	if (item->dp_drr.drr_type == DRR_BEGIN) {
		resume_begin(item, rc);
		return (D_OK);
	}
	if (!rc->rc_seen_begin) {
		errx(1, "stream does not start with a DRR_BEGIN record");
	}

	if (!resume_keep_record(&item->dp_drr, rc)) {
		/* the reader already counted the resync record */
		if (!item->dp_cksum_resync)
			rc->rc_dropped++;
		set_payload(item, NULL, 0);
		return (D_DROP);
	}

	if (!rc->rc_resumed) {
		rc->rc_resumed = B_TRUE;
		rc->rc_resume_stream_offset = item->dp_stream_offset;
	}
	return (D_OK);
}

static chain_step_t
serial_resume(resume_context_t *rc)
{
	chain_step_t step = {
		.cs_type = CS_SERIAL,
		.cs_in_size = sizeof (drr_packet_t),
		.cs_out_size = sizeof (drr_packet_t),
		.cs_context = rc,
		.cs_serial = {
			.process = chain_resume
		}
	};
	return (step);
}

int
zstream_do_resume(int argc, char *argv[])
{
	int c;
	chain_attrs_t attrs = {0};
	resume_context_t rc = {0};

	while ((c = getopt(argc, argv, "v")) != -1) {
		switch (c) {
		case 'v':
			ENABLE_OPTION(&attrs, CA_VERBOSE);
			break;
		case '?':
			warnx("invalid option '%c'", optopt);
			zstream_usage();
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		warnx("missing resume token");
		zstream_usage();
	} else if (argc > 2) {
		warnx("too many arguments");
		zstream_usage();
	}

	parse_resume_token(argv[0], &rc);
	const char *stream_file = (argc > 1) ? argv[1] : NULL;

	ENABLE_OPTION(&attrs, CA_FORBID_DEDUP);

	zstream_chain_t resume_chain = {
		serial_read_stream_skip(stream_file, resume_skip_record, &rc),
		parallel_calc_fletcher4(),
		serial_validate_fletcher4(),
		serial_byteswap(BS_INCOMING),
		serial_validate_records(),
		serial_resume(&rc),
		STANDARD_OUTPUT_STACK(NULL)
	};
	zstream_chain_exec(resume_chain, &attrs);

	return (0);
}
