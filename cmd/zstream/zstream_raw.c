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
 * Portions Copyright 2026 Klara, Inc.
 */

#include <sys/dmu.h>
#include <sys/spa.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/zio.h>
#include <sys/zio_compress.h>
#include <sys/zvol.h>
#include <err.h>
#include <libnvpair.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <zfs_fletcher.h>

#include "zstream.h"
#include "zstream_util.h"

/*
 * Supported feature flags (in drr_versioninfo)
 *
 * Add bits here to allow e.g. compression, large blocks, etc.
 */
#define SUPPORTED_FEATURES (DMU_BACKUP_FEATURE_EMBED_DATA | \
    DMU_BACKUP_FEATURE_LZ4 | DMU_BACKUP_FEATURE_LARGE_BLOCKS | \
    DMU_BACKUP_FEATURE_COMPRESSED | DMU_BACKUP_FEATURE_ZSTD)

static FILE *send_stream;
static int raw_volume;
static boolean_t isreg;
static boolean_t do_byteswap;

/*
 * ssread - send stream read.
 *
 * Read while computing incremental checksum
 */
static boolean_t
ssread(void *buf, size_t len, zio_cksum_t *cksum)
{
	if (fread(buf, len, 1, send_stream) == 0)
		return (B_FALSE);

	if (do_byteswap)
		fletcher_4_incremental_byteswap(buf, len, cksum);
	else
		fletcher_4_incremental_native(buf, len, cksum);
	return (B_TRUE);
}

static inline void
ssread_checked(void *buf, size_t len, zio_cksum_t *cksum)
{
	(void) ssread(buf, len, cksum);
	if (ferror(send_stream))
		err(EXIT_FAILURE, "fread");
}

static inline void
pwrite_checked(void *buf, size_t nbytes, off_t offset)
{
	ASSERT3U(offset + nbytes, >=, offset);
	ssize_t res = pwrite(raw_volume, buf, nbytes, offset);
	if (res < 0)
		err(EXIT_FAILURE, "pwrite");
	ASSERT3U(res, ==, nbytes);
}

static void *zero_page;
static long pagesize;
static long iov_max;

/*
 * write_zeros - zero a region.
 *
 * TODO: Optional secure erase, hole punching with fspacectl/fallocate/discard
 */
static void
write_zeros(off_t offset, size_t len)
{
	static struct iovec *iov = NULL;
	static int have_iovcnt = 0;
	int iovcnt = MIN(howmany(len, pagesize), iov_max);

	ASSERT3U(offset + len, >=, offset);

	if (iovcnt == 0)
		return;
	if (have_iovcnt < iovcnt) {
		iov = safe_realloc(iov, iovcnt * sizeof(*iov));
		have_iovcnt = iovcnt;
	}

	size_t resid = len;
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

static inline void
extend(size_t size)
{
	if (ftruncate(raw_volume, size) < 0)
		err(EXIT_FAILURE, "ftruncate");
}

/*
 * apply_properties - read the properties zap to adjust file size.
 *
 * Returns the value of the "size" property.
 */
static uint64_t
apply_properties(char *buf, size_t len)
{
	const mzap_phys_t *mzap = (const mzap_phys_t *)buf;

	ASSERT3U(len, >=, sizeof (*mzap));
	ASSERT3U(MZAP_ENT_LEN, == , sizeof (mzap_ent_phys_t));

	if (mzap->mz_block_type == BSWAP_64(ZBT_MICRO))
		zap_byteswap(buf, len);

	ASSERT3U(mzap->mz_block_type, ==, ZBT_MICRO);
	ASSERT0(strcmp(mzap->mz_chunk[0].mze_name, "size"));

	uint64_t size = mzap->mz_chunk[0].mze_value;
	extend(size);
	return (size);
}

static boolean_t
read_hdr(dmu_replay_record_t *drr, zio_cksum_t *cksum)
{
	ASSERT3U(offsetof(dmu_replay_record_t, drr_u.drr_checksum.drr_checksum),
	    ==, sizeof (dmu_replay_record_t) - sizeof (zio_cksum_t));
	if (!ssread(drr, sizeof (*drr) - sizeof (zio_cksum_t), cksum))
		return (B_FALSE);
	zio_cksum_t saved_cksum = *cksum;
	if (!ssread(&drr->drr_u.drr_checksum.drr_checksum, sizeof (zio_cksum_t),
	    cksum))
		return (B_FALSE);
	if (!ZIO_CHECKSUM_IS_ZERO(&drr->drr_u.drr_checksum.drr_checksum) &&
	    !ZIO_CHECKSUM_EQUAL(saved_cksum,
	    drr->drr_u.drr_checksum.drr_checksum)) {
		(void) fprintf(stderr, "invalid checksum\n");
		(void) printf("Incorrect checksum in record header.\n");
		(void) printf("Expected checksum = %llx/%llx/%llx/%llx\n",
		    (longlong_t)saved_cksum.zc_word[0],
		    (longlong_t)saved_cksum.zc_word[1],
		    (longlong_t)saved_cksum.zc_word[2],
		    (longlong_t)saved_cksum.zc_word[3]);
		(void) printf("Aborting.\n");
		exit(EXIT_FAILURE);
	}
	return (B_TRUE);
}

int
zstream_do_raw(int argc, char *argv[])
{
	char *buf = safe_malloc(SPA_MAXBLOCKSIZE);
	char *p, *lbuf = NULL;
	size_t lsize, lbufsize = 0;
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
	uint64_t guid = 0;
	drr_headertype_t hdrtype;
	enum zio_compress compression;
	boolean_t verbose = B_FALSE;
	boolean_t first = B_TRUE;
	boolean_t inzvol = B_FALSE;
	boolean_t inprop = B_FALSE;
	int error;
	zio_cksum_t zc = { { 0 } };
	zio_cksum_t pcksum = { { 0 } };

	int c;
	while ((c = getopt(argc, argv, ":g:v")) != -1) {
		switch (c) {
		case 'g':
			guid = strtoull(optarg, NULL, 0);
			if (guid == 0) {
				(void) fprintf(stderr, "invalid guid\n");
				zstream_usage();
			}
			break;
		case 'v':
			verbose = B_TRUE;
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
	argc -= optind;
	argv += optind;

	if (argc < 1) {
		(void) fprintf(stderr, "missing path to raw volume\n");
		zstream_usage();
		exit(EXIT_FAILURE);
	}
	/*
	 * TODO: O_DIRECT, maybe as a command line flag? Avoid O_CREAT in /dev?
	 */
	const char *raw_path = argv[0];
	raw_volume = open(raw_path, O_WRONLY | O_CREAT, 0666);
	if (raw_volume < 0) {
		(void) fprintf(stderr, "Error while opening file '%s': %s\n",
		    raw_path, strerror(errno));
		exit(EXIT_FAILURE);
	}
	struct stat64 st;
	if (fstat64_blk(raw_volume, &st) < 0)
		err(EXIT_FAILURE, "fstat64_blk");
	isreg = S_ISREG(st.st_mode);
	if (argc > 1) {
		const char *filename = argv[1];
		send_stream = fopen(filename, "r");
		if (send_stream == NULL) {
			(void) fprintf(stderr,
			    "Error while opening file '%s': %s\n",
			    filename, strerror(errno));
			exit(EXIT_FAILURE);
		}
	} else {
		if (isatty(STDIN_FILENO)) {
			(void) fprintf(stderr,
			    "Error: The send stream is a binary format "
			    "and can not be read from a\n"
			    "terminal.  Standard input must be redirected, "
			    "or a file must be\n"
			    "specified as a command-line argument.\n");
			exit(EXIT_FAILURE);
		}
		send_stream = stdin;
	}

	pagesize = sysconf(_SC_PAGESIZE);
	iov_max = sysconf(_SC_IOV_MAX);
	zero_page = safe_calloc(pagesize);

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
				ZIO_SET_CHECKSUM(&zc, 0, 0, 0, 0);
				/*
				 * recalculate header checksum now
				 * that we know it needs to be
				 * byteswapped.
				 */
				fletcher_4_incremental_byteswap(drr,
				    sizeof (dmu_replay_record_t), &zc);
			} else if (drrb->drr_magic != DMU_BACKUP_MAGIC) {
				(void) fprintf(stderr, "Invalid stream "
				    "(bad magic number)\n");
				exit(EXIT_FAILURE);
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
			exit(EXIT_FAILURE);
		}

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

			hdrtype = DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo);
			featureflags =
			    DMU_GET_FEATUREFLAGS(drrb->drr_versioninfo);

			if (verbose) {
				(void) printf("BEGIN record\n");
				(void) printf("\thdrtype = %d\n", hdrtype);
				(void) printf("\tfeatures = %llx\n",
				    (u_longlong_t)featureflags);
				(void) printf("\tmagic = %llx\n",
				    (u_longlong_t)drrb->drr_magic);
				(void) printf("\tcreation_time = %llx\n",
				    (u_longlong_t)drrb->drr_creation_time);
				(void) printf("\ttype = %u\n", drrb->drr_type);
				(void) printf("\tflags = 0x%x\n",
				    drrb->drr_flags);
				(void) printf("\ttoguid = %llx\n",
				    (u_longlong_t)drrb->drr_toguid);
				(void) printf("\tfromguid = %llx\n",
				    (u_longlong_t)drrb->drr_fromguid);
				(void) printf("\ttoname = %s\n",
				    drrb->drr_toname);
				(void) printf("\tpayloadlen = %u\n",
				    drr->drr_payloadlen);
				(void) printf("\n");
			}

			uint64_t unsupported_features =
			    featureflags & ~SUPPORTED_FEATURES;
			if (unsupported_features != 0) {
				(void) fprintf(stderr,
				    "Unsupported stream features: "
				    "%#llx of %#llx\n",
				    (u_longlong_t)unsupported_features,
				    (u_longlong_t)featureflags);
				(void) printf("Aborting.\n");
				exit(EXIT_FAILURE);
			}
			if (hdrtype == DMU_SUBSTREAM) {
				if (guid != 0 && drrb->drr_fromguid != guid) {
					(void) fprintf(stderr,
					    "Wrong fromguid: %llu != %llu\n",
					    (u_longlong_t)drrb->drr_fromguid,
					    (u_longlong_t)guid);
					(void) printf("Aborting.\n");
					exit(EXIT_FAILURE);
				}
				guid = drrb->drr_toguid;
			}

			if (drr->drr_payloadlen != 0) {
				nvlist_t *nv;
				int sz = drr->drr_payloadlen;

				if (sz > SPA_MAXBLOCKSIZE) {
					free(buf);
					buf = safe_malloc(sz);
				}
				ssread_checked(buf, sz, &zc);
				error = nvlist_unpack(buf, sz, &nv, 0);
				if (error) {
					errno = error;
					err(EXIT_FAILURE, "nvlist_unpack");
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
			if (!ZIO_CHECKSUM_EQUAL(drre->drr_checksum, pcksum)) {
				(void) printf("Expected checksum differs from "
				    "checksum in stream.\n");
				(void) printf("Expected checksum = "
				    "%llx/%llx/%llx/%llx\n",
				    (long long unsigned int)pcksum.zc_word[0],
				    (long long unsigned int)pcksum.zc_word[1],
				    (long long unsigned int)pcksum.zc_word[2],
				    (long long unsigned int)pcksum.zc_word[3]);
				(void) printf("Aborting.\n");
				exit(EXIT_FAILURE);
			}
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
			if (drro->drr_bonuslen > 0)
				ssread_checked(buf, payload_size, &zc);
			inzvol = drro->drr_object == ZVOL_OBJ &&
			    drro->drr_type == DMU_OT_ZVOL;
			inprop = drro->drr_object == ZVOL_ZAP_OBJ &&
			    drro->drr_type == DMU_OT_ZVOL_PROP;
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
			 * If this is verbose output,
			 * print info on the modified block
			 */
			if (verbose) {
				(void) printf("WRITE object = %llu type = %u "
				    "checksum type = %u compression type = %u "
				    "flags = %u offset = %llu "
				    "logical_size = %llu "
				    "compressed_size = %llu "
				    "payload_size = %llu props = %llx\n",
				    (u_longlong_t)drrw->drr_object,
				    drrw->drr_type,
				    drrw->drr_checksumtype,
				    drrw->drr_compressiontype,
				    drrw->drr_flags,
				    (u_longlong_t)drrw->drr_offset,
				    (u_longlong_t)drrw->drr_logical_size,
				    (u_longlong_t)drrw->drr_compressed_size,
				    (u_longlong_t)payload_size,
				    (u_longlong_t)drrw->drr_key.ddk_prop);
			}

			/*
			 * Read the contents of the block in to buf
			 */
			ssread_checked(buf, payload_size, &zc);

			lsize = drrw->drr_logical_size;
			ASSERT3U(payload_size, <=, lsize);

			compression = drrw->drr_compressiontype;
			if (compression == 0 ||
			    compression == ZIO_COMPRESS_OFF) {
				p = buf;
			} else {
				if (lbufsize < lsize) {
					lbuf = safe_realloc(lbuf, lsize);
					lbufsize = lsize;
				}

				abd_t sabd, dabd;
				abd_get_from_buf_struct(&sabd, buf,
				    payload_size);
				abd_get_from_buf_struct(&dabd, lbuf, lsize);
				error = zio_decompress_data(compression, &sabd,
				    &dabd, payload_size, lsize, NULL);
				abd_free(&dabd);
				abd_free(&sabd);

				if (error != 0) {
					(void) fprintf(stderr,
					    "Decompression failed "
					    "at offset %llu: %s\n",
					    (u_longlong_t)drrw->drr_offset,
					    strerror(error));
					(void) printf("Aborting.\n");
					exit(EXIT_FAILURE);
				}
				p = lbuf;
			}

			if (inprop && isreg) {
				ASSERT0(drrw->drr_offset);
				st.st_size = apply_properties(p, lsize);
			}

			if (!inzvol)
				break;

			pwrite_checked(p, lsize, drrw->drr_offset);
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

			if (!inzvol)
				break;

			off_t off = drrf->drr_offset;
			size_t len = drrf->drr_length;
			if (len == (size_t)-1) {
				if (isreg) {
					extend(off);
					if (off < st.st_size)
						extend(st.st_size);
					else
						st.st_size = off;
					break;
				}
				len = st.st_size - off;
			}
			write_zeros(off, len);
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
				(void) printf("SPILL block for object = %llu "
				    "length = %llu flags = %u "
				    "compression type = %u "
				    "compressed_size = %llu "
				    "payload_size = %llu\n",
				    (u_longlong_t)drrs->drr_object,
				    (u_longlong_t)drrs->drr_length,
				    drrs->drr_flags,
				    drrs->drr_compressiontype,
				    (u_longlong_t)drrs->drr_compressed_size,
				    (u_longlong_t)payload_size);
			}
			ssread_checked(buf, payload_size, &zc);
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

			payload_size = P2ROUNDUP(drrwe->drr_psize, 8);

			ssread_checked(buf, payload_size, &zc);

			lsize = drrwe->drr_lsize;
			ASSERT3U(payload_size, <=, lsize);

			compression = drrwe->drr_compression;
			if (compression == 0 ||
			    compression == ZIO_COMPRESS_OFF) {
				p = buf;
			} else {
				if (lbufsize < lsize) {
					lbuf = safe_realloc(lbuf, lsize);
					lbufsize = lsize;
				}

				abd_t sabd, dabd;
				abd_get_from_buf_struct(&sabd, buf,
				    payload_size);
				abd_get_from_buf_struct(&dabd, lbuf, lsize);
				error = zio_decompress_data(compression, &sabd,
				    &dabd, payload_size, lsize, NULL);
				abd_free(&dabd);
				abd_free(&sabd);

				if (error != 0) {
					(void) fprintf(stderr,
					    "Decompression failed "
					    "at offset %llu: %s\n",
					    (u_longlong_t)drrwe->drr_offset,
					    strerror(error));
					(void) printf("Aborting.\n");
					exit(EXIT_FAILURE);
				}
				p = lbuf;
			}

			if (inprop && isreg) {
				ASSERT0(drrwe->drr_offset);
				st.st_size = apply_properties(p, lsize);
			}

			if (!inzvol)
				break;

			pwrite_checked(p, lsize, drrwe->drr_offset);
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
				(void) printf("OBJECT_RANGE firstobj = %llu "
				    "numslots = %llu flags = %u\n",
				    (u_longlong_t)drror->drr_firstobj,
				    (u_longlong_t)drror->drr_numslots,
				    drror->drr_flags);
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
			exit(EXIT_FAILURE);
		}
		if (verbose && drr->drr_type != DRR_BEGIN) {
			(void) printf("    checksum = %llx/%llx/%llx/%llx\n",
			    (longlong_t)drrc->drr_checksum.zc_word[0],
			    (longlong_t)drrc->drr_checksum.zc_word[1],
			    (longlong_t)drrc->drr_checksum.zc_word[2],
			    (longlong_t)drrc->drr_checksum.zc_word[3]);
		}
		pcksum = zc;
	}
	free(buf);
	free(lbuf);
	fletcher_4_fini();

	if (fsync(raw_volume) != 0)
		err(EXIT_FAILURE, "fsync");

	if (verbose)
		(void) printf("now at guid %llu\n", (u_longlong_t)guid);
	else
		(void) printf("%llu\n", (u_longlong_t)guid);
	return (EXIT_SUCCESS);
}
