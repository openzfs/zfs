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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/nvpair.h>
#include <sys/fs/zfs.h>

#include <libzutil.h>

static void
dump_ddt_stat(const ddt_stat_t *dds, int h)
{
	char refcnt[6];
	char blocks[6], lsize[6], psize[6], dsize[6];
	char ref_blocks[6], ref_lsize[6], ref_psize[6], ref_dsize[6];

	if (dds == NULL || dds->dds_blocks == 0)
		return;

	if (h == -1)
		(void) strcpy(refcnt, "Total");
	else
		zfs_nicenum(1ULL << h, refcnt, sizeof (refcnt));

	zfs_nicenum(dds->dds_blocks, blocks, sizeof (blocks));
	zfs_nicebytes(dds->dds_lsize, lsize, sizeof (lsize));
	zfs_nicebytes(dds->dds_psize, psize, sizeof (psize));
	zfs_nicebytes(dds->dds_dsize, dsize, sizeof (dsize));
	zfs_nicenum(dds->dds_ref_blocks, ref_blocks, sizeof (ref_blocks));
	zfs_nicebytes(dds->dds_ref_lsize, ref_lsize, sizeof (ref_lsize));
	zfs_nicebytes(dds->dds_ref_psize, ref_psize, sizeof (ref_psize));
	zfs_nicebytes(dds->dds_ref_dsize, ref_dsize, sizeof (ref_dsize));

	(void) printf("%6s   %6s   %5s   %5s   %5s   %6s   %5s   %5s   %5s\n",
	    refcnt,
	    blocks, lsize, psize, dsize,
	    ref_blocks, ref_lsize, ref_psize, ref_dsize);
}

/*
 * Print the DDT histogram and the column totals.
 */
void
zpool_dump_ddt(const ddt_stat_t *dds_total, const ddt_histogram_t *ddh)
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
		dump_ddt_stat(&ddh->ddh_stat[h], h);

	dump_ddt_stat(dds_total, -1);

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
