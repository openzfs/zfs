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

#include <libnvpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>

#include <sys/dmu.h>
#include <sys/zfs_ioctl.h>
#include <zfs_fletcher.h>

uint64_t total_write_size = 0;
uint64_t total_stream_len = 0;
FILE *send_stream = 0;
boolean_t do_byteswap = B_FALSE;
boolean_t do_cksum = B_TRUE;
#define	INITIAL_BUFLEN (1<<20)

static void
usage(void)
{
	(void) fprintf(stderr, "usage: zstreamdump [-v] [-C] < file\n");
	(void) fprintf(stderr, "\t -v -- verbose\n");
	(void) fprintf(stderr, "\t -C -- suppress checksum verification\n");
	exit(1);
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

	if (do_cksum && cksum) {
		if (do_byteswap)
			fletcher_4_incremental_byteswap(buf, len, cksum);
		else
			fletcher_4_incremental_native(buf, len, cksum);
	}
	total_stream_len += len;
	return (outlen);
}

int
main(int argc, char *argv[])
{
	char *buf = malloc(INITIAL_BUFLEN);
	uint64_t drr_record_count[DRR_NUMTYPES] = { 0 };
	uint64_t total_records = 0;
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
	char c;
	boolean_t verbose = B_FALSE;
	boolean_t first = B_TRUE;
	int err;
	zio_cksum_t zc = { { 0 } };
	zio_cksum_t pcksum = { { 0 } };

	while ((c = getopt(argc, argv, ":vC")) != -1) {
		switch (c) {
		case 'C':
			do_cksum = B_FALSE;
			break;
		case 'v':
			verbose = B_TRUE;
			break;
		case ':':
			(void) fprintf(stderr,
			    "missing argument for '%c' option\n", optopt);
			usage();
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			usage();
		}
	}

	if (isatty(STDIN_FILENO)) {
		(void) fprintf(stderr,
		    "Error: Backup stream can not be read "
		    "from a terminal.\n"
		    "You must redirect standard input.\n");
		exit(1);
	}

	send_stream = stdin;
	while (ssread(drr, sizeof (dmu_replay_record_t), &zc)) {

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
		total_records++;

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
			if (verbose)
				(void) printf("\n");

			if ((DMU_GET_STREAM_HDRTYPE(drrb->drr_versioninfo) ==
			    DMU_COMPOUNDSTREAM) && drr->drr_payloadlen != 0) {
				nvlist_t *nv;
				int sz = drr->drr_payloadlen;

				if (sz > 1<<20) {
					free(buf);
					buf = malloc(sz);
				}
				(void) ssread(buf, sz, &zc);
				if (ferror(send_stream))
					perror("fread");
				err = nvlist_unpack(buf, sz, &nv, 0);
				if (err)
					perror(strerror(err));
				nvlist_print(stdout, nv);
				nvlist_free(nv);
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
				drro->drr_toguid = BSWAP_64(drro->drr_toguid);
			}
			if (verbose) {
				(void) printf("OBJECT object = %llu type = %u "
				    "bonustype = %u blksz = %u bonuslen = %u\n",
				    (u_longlong_t)drro->drr_object,
				    drro->drr_type,
				    drro->drr_bonustype,
				    drro->drr_blksz,
				    drro->drr_bonuslen);
			}
			if (drro->drr_bonuslen > 0) {
				(void) ssread(buf,
				    P2ROUNDUP(drro->drr_bonuslen, 8), &zc);
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
				drrw->drr_length = BSWAP_64(drrw->drr_length);
				drrw->drr_toguid = BSWAP_64(drrw->drr_toguid);
				drrw->drr_key.ddk_prop =
				    BSWAP_64(drrw->drr_key.ddk_prop);
			}
			if (verbose) {
				(void) printf("WRITE object = %llu type = %u "
				    "checksum type = %u\n"
				    "offset = %llu length = %llu "
				    "props = %llx\n",
				    (u_longlong_t)drrw->drr_object,
				    drrw->drr_type,
				    drrw->drr_checksumtype,
				    (u_longlong_t)drrw->drr_offset,
				    (u_longlong_t)drrw->drr_length,
				    (u_longlong_t)drrw->drr_key.ddk_prop);
			}
			(void) ssread(buf, drrw->drr_length, &zc);
			total_write_size += drrw->drr_length;
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
				    "checksum type = %u props = %llx\n"
				    "offset = %llu length = %llu\n"
				    "toguid = %llx refguid = %llx\n"
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
			}
			if (verbose) {
				(void) printf("SPILL block for object = %llu "
				    "length = %llu\n",
				    (long long unsigned int)drrs->drr_object,
				    (long long unsigned int)drrs->drr_length);
			}
			(void) ssread(buf, drrs->drr_length, &zc);
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
				    "offset = %llu length = %llu\n"
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
			break;
		case DRR_NUMTYPES:
			/* should never be reached */
			exit(1);
		}
		pcksum = zc;
	}
	free(buf);

	/* Print final summary */

	(void) printf("SUMMARY:\n");
	(void) printf("\tTotal DRR_BEGIN records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_BEGIN]);
	(void) printf("\tTotal DRR_END records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_END]);
	(void) printf("\tTotal DRR_OBJECT records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_OBJECT]);
	(void) printf("\tTotal DRR_FREEOBJECTS records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_FREEOBJECTS]);
	(void) printf("\tTotal DRR_WRITE records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_WRITE]);
	(void) printf("\tTotal DRR_WRITE_BYREF records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_WRITE_BYREF]);
	(void) printf("\tTotal DRR_WRITE_EMBEDDED records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_WRITE_EMBEDDED]);
	(void) printf("\tTotal DRR_FREE records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_FREE]);
	(void) printf("\tTotal DRR_SPILL records = %lld\n",
	    (u_longlong_t)drr_record_count[DRR_SPILL]);
	(void) printf("\tTotal records = %lld\n",
	    (u_longlong_t)total_records);
	(void) printf("\tTotal write size = %lld (0x%llx)\n",
	    (u_longlong_t)total_write_size, (u_longlong_t)total_write_size);
	(void) printf("\tTotal stream length = %lld (0x%llx)\n",
	    (u_longlong_t)total_stream_len, (u_longlong_t)total_stream_len);
	return (0);
}
