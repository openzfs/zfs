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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>
#include <math.h>

#include <libzutil.h>

static void
dump_ddt_stat(const ddt_stat_t *dds, int h, boolean_t parsable)
{
	char refcnt[32];
	char blocks[32], lsize[32], psize[32], dsize[32];
	char ref_blocks[32], ref_lsize[32], ref_psize[32], ref_dsize[32];

	enum zfs_nicenum_format count_format = parsable ?
	    ZFS_NICENUM_RAW : ZFS_NICENUM_1024;
	enum zfs_nicenum_format byte_format = parsable ?
	    ZFS_NICENUM_RAW : ZFS_NICENUM_BYTES;

	if (dds == NULL || dds->dds_blocks == 0)
		return;

	if (h == -1)
		(void) strcpy(refcnt, "Total");
	else
		zfs_nicenum_format(1ULL << h, refcnt, sizeof (refcnt),
		    count_format);


	zfs_nicenum_format(dds->dds_blocks, blocks, sizeof (blocks),
	    count_format);
	zfs_nicenum_format(dds->dds_lsize, lsize, sizeof (lsize),
	    byte_format);
	zfs_nicenum_format(dds->dds_psize, psize, sizeof (psize),
	    byte_format);
	zfs_nicenum_format(dds->dds_dsize, dsize, sizeof (dsize),
	    byte_format);
	zfs_nicenum_format(dds->dds_ref_blocks, ref_blocks,
	    sizeof (ref_blocks), count_format);
	zfs_nicenum_format(dds->dds_ref_lsize, ref_lsize,
	    sizeof (ref_lsize), byte_format);
	zfs_nicenum_format(dds->dds_ref_psize, ref_psize,
	    sizeof (ref_psize), byte_format);
	zfs_nicenum_format(dds->dds_ref_dsize, ref_dsize,
	    sizeof (ref_dsize), byte_format);

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    refcnt,
	    blocks, lsize, psize, dsize,
	    ref_blocks, ref_lsize, ref_psize, ref_dsize);
}

/*
 * Print the DDT histogram and the column totals.
 */
void
zpool_dump_ddt(const ddt_stat_t *dds_total, const ddt_histogram_t *ddh,
    boolean_t parsable)
{
	int h;

	(void) printf("\n");

	(void) printf("bucket   "
	    "           allocated             "
	    "          referenced          \n");
	(void) printf("______   "
	    "______________________________   "
	    "______________________________\n");

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    "refcnt",
	    "blocks", "LSIZE", "PSIZE", "DSIZE",
	    "blocks", "LSIZE", "PSIZE", "DSIZE");

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    "------",
	    "------", "-----", "-----", "-----",
	    "------", "-----", "-----", "-----");

	for (h = 0; h < 64; h++)
		dump_ddt_stat(&ddh->ddh_stat[h], h, parsable);

	dump_ddt_stat(dds_total, -1, parsable);

	(void) printf("\n");
}

/*
 * Process the buffer of nvlists, unpacking and storing each nvlist record
 * into 'records'.  'leftover' is set to the number of bytes that weren't
 * processed as there wasn't a complete record.
 */
int
zpool_history_unpack(char *buf, uint64_t bytes_read, uint64_t *leftover,
    nvlist_t ***records, uint_t *numrecords)
{
	uint64_t reclen;
	nvlist_t *nv;
	int i;
	void *tmp;

	while (bytes_read > sizeof (reclen)) {

		/* get length of packed record (stored as little endian) */
		for (i = 0, reclen = 0; i < sizeof (reclen); i++)
			reclen += (uint64_t)(((uchar_t *)buf)[i]) << (8*i);

		if (bytes_read < sizeof (reclen) + reclen)
			break;

		/* unpack record */
		int err = nvlist_unpack(buf + sizeof (reclen), reclen, &nv, 0);
		if (err != 0)
			return (err);
		bytes_read -= sizeof (reclen) + reclen;
		buf += sizeof (reclen) + reclen;

		/* add record to nvlist array */
		(*numrecords)++;
		if (ISP2(*numrecords + 1)) {
			tmp = realloc(*records,
			    *numrecords * 2 * sizeof (nvlist_t *));
			if (tmp == NULL) {
				nvlist_free(nv);
				(*numrecords)--;
				return (ENOMEM);
			}
			*records = tmp;
		}
		(*records)[*numrecords - 1] = nv;
	}

	*leftover = bytes_read;
	return (0);
}

/*
 * Floating point sleep().  Allows you to pass in a floating point value for
 * seconds.
 */
void
fsleep(float sec)
{
	struct timespec req;
	req.tv_sec = floor(sec);
	req.tv_nsec = (sec - (float)req.tv_sec) * NANOSEC;
	nanosleep(&req, NULL);
}

/*
 * Get environment variable 'env' and return it as an integer.
 * If 'env' is not set, then return 'default_val' instead.
 */
int
zpool_getenv_int(const char *env, int default_val)
{
	char *str;
	int val;
	str = getenv(env);
	if ((str == NULL) || sscanf(str, "%d", &val) != 1 ||
	    val < 0) {
		val = default_val;
	}
	return (val);
}
