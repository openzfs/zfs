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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011, 2014 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <ctype.h>
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_sa.h>
#include <sys/sa.h>
#include <sys/sa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dbuf.h>
#include <sys/zil.h>
#include <sys/zil_impl.h>
#include <sys/stat.h>
#include <sys/resource.h>
#include <sys/dmu_traverse.h>
#include <sys/zio_checksum.h>
#include <sys/zio_compress.h>
#include <sys/zfs_fuid.h>
#include <sys/arc.h>
#include <sys/ddt.h>
#include <sys/zfeature.h>
#include <zfs_comutil.h>
#undef ZFS_MAXNAMELEN
#include <libzfs.h>

#define	ZDB_COMPRESS_NAME(idx) ((idx) < ZIO_COMPRESS_FUNCTIONS ?	\
	zio_compress_table[(idx)].ci_name : "UNKNOWN")
#define	ZDB_CHECKSUM_NAME(idx) ((idx) < ZIO_CHECKSUM_FUNCTIONS ?	\
	zio_checksum_table[(idx)].ci_name : "UNKNOWN")
#define	ZDB_OT_NAME(idx) ((idx) < DMU_OT_NUMTYPES ?	\
	dmu_ot[(idx)].ot_name : DMU_OT_IS_VALID(idx) ?	\
	dmu_ot_byteswap[DMU_OT_BYTESWAP(idx)].ob_name : "UNKNOWN")
#define	ZDB_OT_TYPE(idx) ((idx) < DMU_OT_NUMTYPES ? (idx) :		\
	(((idx) == DMU_OTN_ZAP_DATA || (idx) == DMU_OTN_ZAP_METADATA) ?	\
	DMU_OT_ZAP_OTHER : DMU_OT_NUMTYPES))

#ifndef lint
extern int zfs_recover;
#else
int zfs_recover;
#endif

const char cmdname[] = "zdb";
uint8_t dump_opt[256];

typedef void object_viewer_t(objset_t *, uint64_t, void *data, size_t size);

extern void dump_intent_log(zilog_t *);
uint64_t *zopt_object = NULL;
int zopt_objects = 0;
libzfs_handle_t *g_zfs;
uint64_t max_inflight = 200;

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */
const char *
_umem_debug_init(void)
{
	return ("default,verbose"); /* $UMEM_DEBUG setting */
}

const char *
_umem_logging_init(void)
{
	return ("fail,contents"); /* $UMEM_LOGGING setting */
}

static void
usage(void)
{
	(void) fprintf(stderr,
	    "Usage: %s [-CumMdibcsDvhLXFPA] [-t txg] [-e [-p path...]] "
	    "[-U config] [-I inflight I/Os] poolname [object...]\n"
	    "       %s [-divPA] [-e -p path...] [-U config] dataset "
	    "[object...]\n"
	    "       %s -mM [-LXFPA] [-t txg] [-e [-p path...]] [-U config] "
	    "poolname [vdev [metaslab...]]\n"
	    "       %s -R [-A] [-e [-p path...]] poolname "
	    "vdev:offset:size[:flags]\n"
	    "       %s -S [-PA] [-e [-p path...]] [-U config] poolname\n"
	    "       %s -l [-uA] device\n"
	    "       %s -C [-A] [-U config]\n\n",
	    cmdname, cmdname, cmdname, cmdname, cmdname, cmdname, cmdname);

	(void) fprintf(stderr, "    Dataset name must include at least one "
	    "separator character '/' or '@'\n");
	(void) fprintf(stderr, "    If dataset name is specified, only that "
	    "dataset is dumped\n");
	(void) fprintf(stderr, "    If object numbers are specified, only "
	    "those objects are dumped\n\n");
	(void) fprintf(stderr, "    Options to control amount of output:\n");
	(void) fprintf(stderr, "        -u uberblock\n");
	(void) fprintf(stderr, "        -d dataset(s)\n");
	(void) fprintf(stderr, "        -i intent logs\n");
	(void) fprintf(stderr, "        -C config (or cachefile if alone)\n");
	(void) fprintf(stderr, "        -h pool history\n");
	(void) fprintf(stderr, "        -b block statistics\n");
	(void) fprintf(stderr, "        -m metaslabs\n");
	(void) fprintf(stderr, "        -M metaslab groups\n");
	(void) fprintf(stderr, "        -c checksum all metadata (twice for "
	    "all data) blocks\n");
	(void) fprintf(stderr, "        -s report stats on zdb's I/O\n");
	(void) fprintf(stderr, "        -D dedup statistics\n");
	(void) fprintf(stderr, "        -S simulate dedup to measure effect\n");
	(void) fprintf(stderr, "        -v verbose (applies to all others)\n");
	(void) fprintf(stderr, "        -l dump label contents\n");
	(void) fprintf(stderr, "        -L disable leak tracking (do not "
	    "load spacemaps)\n");
	(void) fprintf(stderr, "        -R read and display block from a "
	    "device\n\n");
	(void) fprintf(stderr, "    Below options are intended for use "
	    "with other options (except -l):\n");
	(void) fprintf(stderr, "        -A ignore assertions (-A), enable "
	    "panic recovery (-AA) or both (-AAA)\n");
	(void) fprintf(stderr, "        -F attempt automatic rewind within "
	    "safe range of transaction groups\n");
	(void) fprintf(stderr, "        -U <cachefile_path> -- use alternate "
	    "cachefile\n");
	(void) fprintf(stderr, "        -X attempt extreme rewind (does not "
	    "work with dataset)\n");
	(void) fprintf(stderr, "        -e pool is exported/destroyed/"
	    "has altroot/not in a cachefile\n");
	(void) fprintf(stderr, "        -p <path> -- use one or more with "
	    "-e to specify path to vdev dir\n");
	(void) fprintf(stderr, "        -P print numbers in parseable form\n");
	(void) fprintf(stderr, "        -t <txg> -- highest txg to use when "
	    "searching for uberblocks\n");
	(void) fprintf(stderr, "        -I <number of inflight I/Os> -- "
	    "specify the maximum number of checksumming I/Os "
	    "[default is 200]\n");
	(void) fprintf(stderr, "Specify an option more than once (e.g. -bb) "
	    "to make only that option verbose\n");
	(void) fprintf(stderr, "Default is to dump everything non-verbosely\n");
	exit(1);
}

/*
 * Called for usage errors that are discovered after a call to spa_open(),
 * dmu_bonus_hold(), or pool_match().  abort() is called for other errors.
 */

static void
fatal(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	(void) fprintf(stderr, "%s: ", cmdname);
	(void) vfprintf(stderr, fmt, ap);
	va_end(ap);
	(void) fprintf(stderr, "\n");

	exit(1);
}

/* ARGSUSED */
static void
dump_packed_nvlist(objset_t *os, uint64_t object, void *data, size_t size)
{
	nvlist_t *nv;
	size_t nvsize = *(uint64_t *)data;
	char *packed = umem_alloc(nvsize, UMEM_NOFAIL);

	VERIFY(0 == dmu_read(os, object, 0, nvsize, packed, DMU_READ_PREFETCH));

	VERIFY(nvlist_unpack(packed, nvsize, &nv, 0) == 0);

	umem_free(packed, nvsize);

	dump_nvlist(nv, 8);

	nvlist_free(nv);
}

/* ARGSUSED */
static void
dump_history_offsets(objset_t *os, uint64_t object, void *data, size_t size)
{
	spa_history_phys_t *shp = data;

	if (shp == NULL)
		return;

	(void) printf("\t\tpool_create_len = %llu\n",
	    (u_longlong_t)shp->sh_pool_create_len);
	(void) printf("\t\tphys_max_off = %llu\n",
	    (u_longlong_t)shp->sh_phys_max_off);
	(void) printf("\t\tbof = %llu\n",
	    (u_longlong_t)shp->sh_bof);
	(void) printf("\t\teof = %llu\n",
	    (u_longlong_t)shp->sh_eof);
	(void) printf("\t\trecords_lost = %llu\n",
	    (u_longlong_t)shp->sh_records_lost);
}

static void
zdb_nicenum(uint64_t num, char *buf)
{
	if (dump_opt['P'])
		(void) sprintf(buf, "%llu", (longlong_t)num);
	else
		nicenum(num, buf);
}

const char histo_stars[] = "****************************************";
const int histo_width = sizeof (histo_stars) - 1;

static void
dump_histogram(const uint64_t *histo, int size, int offset)
{
	int i;
	int minidx = size - 1;
	int maxidx = 0;
	uint64_t max = 0;

	for (i = 0; i < size; i++) {
		if (histo[i] > max)
			max = histo[i];
		if (histo[i] > 0 && i > maxidx)
			maxidx = i;
		if (histo[i] > 0 && i < minidx)
			minidx = i;
	}

	if (max < histo_width)
		max = histo_width;

	for (i = minidx; i <= maxidx; i++) {
		(void) printf("\t\t\t%3u: %6llu %s\n",
		    i + offset, (u_longlong_t)histo[i],
		    &histo_stars[(max - histo[i]) * histo_width / max]);
	}
}

static void
dump_zap_stats(objset_t *os, uint64_t object)
{
	int error;
	zap_stats_t zs;

	error = zap_get_stats(os, object, &zs);
	if (error)
		return;

	if (zs.zs_ptrtbl_len == 0) {
		ASSERT(zs.zs_num_blocks == 1);
		(void) printf("\tmicrozap: %llu bytes, %llu entries\n",
		    (u_longlong_t)zs.zs_blocksize,
		    (u_longlong_t)zs.zs_num_entries);
		return;
	}

	(void) printf("\tFat ZAP stats:\n");

	(void) printf("\t\tPointer table:\n");
	(void) printf("\t\t\t%llu elements\n",
	    (u_longlong_t)zs.zs_ptrtbl_len);
	(void) printf("\t\t\tzt_blk: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_zt_blk);
	(void) printf("\t\t\tzt_numblks: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_zt_numblks);
	(void) printf("\t\t\tzt_shift: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_zt_shift);
	(void) printf("\t\t\tzt_blks_copied: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_blks_copied);
	(void) printf("\t\t\tzt_nextblk: %llu\n",
	    (u_longlong_t)zs.zs_ptrtbl_nextblk);

	(void) printf("\t\tZAP entries: %llu\n",
	    (u_longlong_t)zs.zs_num_entries);
	(void) printf("\t\tLeaf blocks: %llu\n",
	    (u_longlong_t)zs.zs_num_leafs);
	(void) printf("\t\tTotal blocks: %llu\n",
	    (u_longlong_t)zs.zs_num_blocks);
	(void) printf("\t\tzap_block_type: 0x%llx\n",
	    (u_longlong_t)zs.zs_block_type);
	(void) printf("\t\tzap_magic: 0x%llx\n",
	    (u_longlong_t)zs.zs_magic);
	(void) printf("\t\tzap_salt: 0x%llx\n",
	    (u_longlong_t)zs.zs_salt);

	(void) printf("\t\tLeafs with 2^n pointers:\n");
	dump_histogram(zs.zs_leafs_with_2n_pointers, ZAP_HISTOGRAM_SIZE, 0);

	(void) printf("\t\tBlocks with n*5 entries:\n");
	dump_histogram(zs.zs_blocks_with_n5_entries, ZAP_HISTOGRAM_SIZE, 0);

	(void) printf("\t\tBlocks n/10 full:\n");
	dump_histogram(zs.zs_blocks_n_tenths_full, ZAP_HISTOGRAM_SIZE, 0);

	(void) printf("\t\tEntries with n chunks:\n");
	dump_histogram(zs.zs_entries_using_n_chunks, ZAP_HISTOGRAM_SIZE, 0);

	(void) printf("\t\tBuckets with n entries:\n");
	dump_histogram(zs.zs_buckets_with_n_entries, ZAP_HISTOGRAM_SIZE, 0);
}

/*ARGSUSED*/
static void
dump_none(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_unknown(objset_t *os, uint64_t object, void *data, size_t size)
{
	(void) printf("\tUNKNOWN OBJECT TYPE\n");
}

/*ARGSUSED*/
void
dump_uint8(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_uint64(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_zap(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;
	void *prop;
	int i;

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = ", attr.za_name);
		if (attr.za_num_integers == 0) {
			(void) printf("\n");
			continue;
		}
		prop = umem_zalloc(attr.za_num_integers *
		    attr.za_integer_length, UMEM_NOFAIL);
		(void) zap_lookup(os, object, attr.za_name,
		    attr.za_integer_length, attr.za_num_integers, prop);
		if (attr.za_integer_length == 1) {
			(void) printf("%s", (char *)prop);
		} else {
			for (i = 0; i < attr.za_num_integers; i++) {
				switch (attr.za_integer_length) {
				case 2:
					(void) printf("%u ",
					    ((uint16_t *)prop)[i]);
					break;
				case 4:
					(void) printf("%u ",
					    ((uint32_t *)prop)[i]);
					break;
				case 8:
					(void) printf("%lld ",
					    (u_longlong_t)((int64_t *)prop)[i]);
					break;
				}
			}
		}
		(void) printf("\n");
		umem_free(prop, attr.za_num_integers * attr.za_integer_length);
	}
	zap_cursor_fini(&zc);
}

/*ARGSUSED*/
static void
dump_ddt_zap(objset_t *os, uint64_t object, void *data, size_t size)
{
	dump_zap_stats(os, object);
	/* contents are printed elsewhere, properly decoded */
}

/*ARGSUSED*/
static void
dump_sa_attrs(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = ", attr.za_name);
		if (attr.za_num_integers == 0) {
			(void) printf("\n");
			continue;
		}
		(void) printf(" %llx : [%d:%d:%d]\n",
		    (u_longlong_t)attr.za_first_integer,
		    (int)ATTR_LENGTH(attr.za_first_integer),
		    (int)ATTR_BSWAP(attr.za_first_integer),
		    (int)ATTR_NUM(attr.za_first_integer));
	}
	zap_cursor_fini(&zc);
}

/*ARGSUSED*/
static void
dump_sa_layouts(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;
	uint16_t *layout_attrs;
	int i;

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = [", attr.za_name);
		if (attr.za_num_integers == 0) {
			(void) printf("\n");
			continue;
		}

		VERIFY(attr.za_integer_length == 2);
		layout_attrs = umem_zalloc(attr.za_num_integers *
		    attr.za_integer_length, UMEM_NOFAIL);

		VERIFY(zap_lookup(os, object, attr.za_name,
		    attr.za_integer_length,
		    attr.za_num_integers, layout_attrs) == 0);

		for (i = 0; i != attr.za_num_integers; i++)
			(void) printf(" %d ", (int)layout_attrs[i]);
		(void) printf("]\n");
		umem_free(layout_attrs,
		    attr.za_num_integers * attr.za_integer_length);
	}
	zap_cursor_fini(&zc);
}

/*ARGSUSED*/
static void
dump_zpldir(objset_t *os, uint64_t object, void *data, size_t size)
{
	zap_cursor_t zc;
	zap_attribute_t attr;
	const char *typenames[] = {
		/* 0 */ "not specified",
		/* 1 */ "FIFO",
		/* 2 */ "Character Device",
		/* 3 */ "3 (invalid)",
		/* 4 */ "Directory",
		/* 5 */ "5 (invalid)",
		/* 6 */ "Block Device",
		/* 7 */ "7 (invalid)",
		/* 8 */ "Regular File",
		/* 9 */ "9 (invalid)",
		/* 10 */ "Symbolic Link",
		/* 11 */ "11 (invalid)",
		/* 12 */ "Socket",
		/* 13 */ "Door",
		/* 14 */ "Event Port",
		/* 15 */ "15 (invalid)",
	};

	dump_zap_stats(os, object);
	(void) printf("\n");

	for (zap_cursor_init(&zc, os, object);
	    zap_cursor_retrieve(&zc, &attr) == 0;
	    zap_cursor_advance(&zc)) {
		(void) printf("\t\t%s = %lld (type: %s)\n",
		    attr.za_name, ZFS_DIRENT_OBJ(attr.za_first_integer),
		    typenames[ZFS_DIRENT_TYPE(attr.za_first_integer)]);
	}
	zap_cursor_fini(&zc);
}

int
get_dtl_refcount(vdev_t *vd)
{
	int refcount = 0;
	int c;

	if (vd->vdev_ops->vdev_op_leaf) {
		space_map_t *sm = vd->vdev_dtl_sm;

		if (sm != NULL &&
		    sm->sm_dbuf->db_size == sizeof (space_map_phys_t))
			return (1);
		return (0);
	}

	for (c = 0; c < vd->vdev_children; c++)
		refcount += get_dtl_refcount(vd->vdev_child[c]);
	return (refcount);
}

int
get_metaslab_refcount(vdev_t *vd)
{
	int refcount = 0;
	int c, m;

	if (vd->vdev_top == vd && !vd->vdev_removing) {
		for (m = 0; m < vd->vdev_ms_count; m++) {
			space_map_t *sm = vd->vdev_ms[m]->ms_sm;

			if (sm != NULL &&
			    sm->sm_dbuf->db_size == sizeof (space_map_phys_t))
				refcount++;
		}
	}
	for (c = 0; c < vd->vdev_children; c++)
		refcount += get_metaslab_refcount(vd->vdev_child[c]);

	return (refcount);
}

static int
verify_spacemap_refcounts(spa_t *spa)
{
	uint64_t expected_refcount = 0;
	uint64_t actual_refcount;

	(void) feature_get_refcount(spa,
	    &spa_feature_table[SPA_FEATURE_SPACEMAP_HISTOGRAM],
	    &expected_refcount);
	actual_refcount = get_dtl_refcount(spa->spa_root_vdev);
	actual_refcount += get_metaslab_refcount(spa->spa_root_vdev);

	if (expected_refcount != actual_refcount) {
		(void) printf("space map refcount mismatch: expected %lld != "
		    "actual %lld\n",
		    (longlong_t)expected_refcount,
		    (longlong_t)actual_refcount);
		return (2);
	}
	return (0);
}

static void
dump_spacemap(objset_t *os, space_map_t *sm)
{
	uint64_t alloc, offset, entry;
	char *ddata[] = { "ALLOC", "FREE", "CONDENSE", "INVALID",
			    "INVALID", "INVALID", "INVALID", "INVALID" };

	if (sm == NULL)
		return;

	/*
	 * Print out the freelist entries in both encoded and decoded form.
	 */
	alloc = 0;
	for (offset = 0; offset < space_map_length(sm);
	    offset += sizeof (entry)) {
		uint8_t mapshift = sm->sm_shift;

		VERIFY0(dmu_read(os, space_map_object(sm), offset,
		    sizeof (entry), &entry, DMU_READ_PREFETCH));
		if (SM_DEBUG_DECODE(entry)) {

			(void) printf("\t    [%6llu] %s: txg %llu, pass %llu\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    ddata[SM_DEBUG_ACTION_DECODE(entry)],
			    (u_longlong_t)SM_DEBUG_TXG_DECODE(entry),
			    (u_longlong_t)SM_DEBUG_SYNCPASS_DECODE(entry));
		} else {
			(void) printf("\t    [%6llu]    %c  range:"
			    " %010llx-%010llx  size: %06llx\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    SM_TYPE_DECODE(entry) == SM_ALLOC ? 'A' : 'F',
			    (u_longlong_t)((SM_OFFSET_DECODE(entry) <<
			    mapshift) + sm->sm_start),
			    (u_longlong_t)((SM_OFFSET_DECODE(entry) <<
			    mapshift) + sm->sm_start +
			    (SM_RUN_DECODE(entry) << mapshift)),
			    (u_longlong_t)(SM_RUN_DECODE(entry) << mapshift));
			if (SM_TYPE_DECODE(entry) == SM_ALLOC)
				alloc += SM_RUN_DECODE(entry) << mapshift;
			else
				alloc -= SM_RUN_DECODE(entry) << mapshift;
		}
	}
	if (alloc != space_map_allocated(sm)) {
		(void) printf("space_map_object alloc (%llu) INCONSISTENT "
		    "with space map summary (%llu)\n",
		    (u_longlong_t)space_map_allocated(sm), (u_longlong_t)alloc);
	}
}

static void
dump_metaslab_stats(metaslab_t *msp)
{
	char maxbuf[32];
	range_tree_t *rt = msp->ms_tree;
	avl_tree_t *t = &msp->ms_size_tree;
	int free_pct = range_tree_space(rt) * 100 / msp->ms_size;

	zdb_nicenum(metaslab_block_maxsize(msp), maxbuf);

	(void) printf("\t %25s %10lu   %7s  %6s   %4s %4d%%\n",
	    "segments", avl_numnodes(t), "maxsize", maxbuf,
	    "freepct", free_pct);
	(void) printf("\tIn-memory histogram:\n");
	dump_histogram(rt->rt_histogram, RANGE_TREE_HISTOGRAM_SIZE, 0);
}

static void
dump_metaslab(metaslab_t *msp)
{
	vdev_t *vd = msp->ms_group->mg_vd;
	spa_t *spa = vd->vdev_spa;
	space_map_t *sm = msp->ms_sm;
	char freebuf[32];

	zdb_nicenum(msp->ms_size - space_map_allocated(sm), freebuf);

	(void) printf(
	    "\tmetaslab %6llu   offset %12llx   spacemap %6llu   free    %5s\n",
	    (u_longlong_t)msp->ms_id, (u_longlong_t)msp->ms_start,
	    (u_longlong_t)space_map_object(sm), freebuf);

	if (dump_opt['m'] > 2 && !dump_opt['L']) {
		mutex_enter(&msp->ms_lock);
		metaslab_load_wait(msp);
		if (!msp->ms_loaded) {
			VERIFY0(metaslab_load(msp));
			range_tree_stat_verify(msp->ms_tree);
		}
		dump_metaslab_stats(msp);
		metaslab_unload(msp);
		mutex_exit(&msp->ms_lock);
	}

	if (dump_opt['m'] > 1 && sm != NULL &&
	    spa_feature_is_active(spa, SPA_FEATURE_SPACEMAP_HISTOGRAM)) {
		/*
		 * The space map histogram represents free space in chunks
		 * of sm_shift (i.e. bucket 0 refers to 2^sm_shift).
		 */
		(void) printf("\tOn-disk histogram:\t\tfragmentation %llu\n",
		    (u_longlong_t)msp->ms_fragmentation);
		dump_histogram(sm->sm_phys->smp_histogram,
		    SPACE_MAP_HISTOGRAM_SIZE, sm->sm_shift);
	}

	if (dump_opt['d'] > 5 || dump_opt['m'] > 3) {
		ASSERT(msp->ms_size == (1ULL << vd->vdev_ms_shift));

		mutex_enter(&msp->ms_lock);
		dump_spacemap(spa->spa_meta_objset, msp->ms_sm);
		mutex_exit(&msp->ms_lock);
	}
}

static void
print_vdev_metaslab_header(vdev_t *vd)
{
	(void) printf("\tvdev %10llu\n\t%-10s%5llu   %-19s   %-15s   %-10s\n",
	    (u_longlong_t)vd->vdev_id,
	    "metaslabs", (u_longlong_t)vd->vdev_ms_count,
	    "offset", "spacemap", "free");
	(void) printf("\t%15s   %19s   %15s   %10s\n",
	    "---------------", "-------------------",
	    "---------------", "-------------");
}

static void
dump_metaslab_groups(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	metaslab_class_t *mc = spa_normal_class(spa);
	uint64_t fragmentation;
	int c;

	metaslab_class_histogram_verify(mc);

	for (c = 0; c < rvd->vdev_children; c++) {
		vdev_t *tvd = rvd->vdev_child[c];
		metaslab_group_t *mg = tvd->vdev_mg;

		if (mg->mg_class != mc)
			continue;

		metaslab_group_histogram_verify(mg);
		mg->mg_fragmentation = metaslab_group_fragmentation(mg);

		(void) printf("\tvdev %10llu\t\tmetaslabs%5llu\t\t"
		    "fragmentation",
		    (u_longlong_t)tvd->vdev_id,
		    (u_longlong_t)tvd->vdev_ms_count);
		if (mg->mg_fragmentation == ZFS_FRAG_INVALID) {
			(void) printf("%3s\n", "-");
		} else {
			(void) printf("%3llu%%\n",
			    (u_longlong_t)mg->mg_fragmentation);
		}
		dump_histogram(mg->mg_histogram, RANGE_TREE_HISTOGRAM_SIZE, 0);
	}

	(void) printf("\tpool %s\tfragmentation", spa_name(spa));
	fragmentation = metaslab_class_fragmentation(mc);
	if (fragmentation == ZFS_FRAG_INVALID)
		(void) printf("\t%3s\n", "-");
	else
		(void) printf("\t%3llu%%\n", (u_longlong_t)fragmentation);
	dump_histogram(mc->mc_histogram, RANGE_TREE_HISTOGRAM_SIZE, 0);
}

static void
dump_metaslabs(spa_t *spa)
{
	vdev_t *vd, *rvd = spa->spa_root_vdev;
	uint64_t m, c = 0, children = rvd->vdev_children;

	(void) printf("\nMetaslabs:\n");

	if (!dump_opt['d'] && zopt_objects > 0) {
		c = zopt_object[0];

		if (c >= children)
			(void) fatal("bad vdev id: %llu", (u_longlong_t)c);

		if (zopt_objects > 1) {
			vd = rvd->vdev_child[c];
			print_vdev_metaslab_header(vd);

			for (m = 1; m < zopt_objects; m++) {
				if (zopt_object[m] < vd->vdev_ms_count)
					dump_metaslab(
					    vd->vdev_ms[zopt_object[m]]);
				else
					(void) fprintf(stderr, "bad metaslab "
					    "number %llu\n",
					    (u_longlong_t)zopt_object[m]);
			}
			(void) printf("\n");
			return;
		}
		children = c + 1;
	}
	for (; c < children; c++) {
		vd = rvd->vdev_child[c];
		print_vdev_metaslab_header(vd);

		for (m = 0; m < vd->vdev_ms_count; m++)
			dump_metaslab(vd->vdev_ms[m]);
		(void) printf("\n");
	}
}

static void
dump_dde(const ddt_t *ddt, const ddt_entry_t *dde, uint64_t index)
{
	const ddt_phys_t *ddp = dde->dde_phys;
	const ddt_key_t *ddk = &dde->dde_key;
	char *types[4] = { "ditto", "single", "double", "triple" };
	char blkbuf[BP_SPRINTF_LEN];
	blkptr_t blk;
	int p;

	for (p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
		if (ddp->ddp_phys_birth == 0)
			continue;
		ddt_bp_create(ddt->ddt_checksum, ddk, ddp, &blk);
		snprintf_blkptr(blkbuf, sizeof (blkbuf), &blk);
		(void) printf("index %llx refcnt %llu %s %s\n",
		    (u_longlong_t)index, (u_longlong_t)ddp->ddp_refcnt,
		    types[p], blkbuf);
	}
}

static void
dump_dedup_ratio(const ddt_stat_t *dds)
{
	double rL, rP, rD, D, dedup, compress, copies;

	if (dds->dds_blocks == 0)
		return;

	rL = (double)dds->dds_ref_lsize;
	rP = (double)dds->dds_ref_psize;
	rD = (double)dds->dds_ref_dsize;
	D = (double)dds->dds_dsize;

	dedup = rD / D;
	compress = rL / rP;
	copies = rD / rP;

	(void) printf("dedup = %.2f, compress = %.2f, copies = %.2f, "
	    "dedup * compress / copies = %.2f\n\n",
	    dedup, compress, copies, dedup * compress / copies);
}

static void
dump_ddt(ddt_t *ddt, enum ddt_type type, enum ddt_class class)
{
	char name[DDT_NAMELEN];
	ddt_entry_t dde;
	uint64_t walk = 0;
	dmu_object_info_t doi;
	uint64_t count, dspace, mspace;
	int error;

	error = ddt_object_info(ddt, type, class, &doi);

	if (error == ENOENT)
		return;
	ASSERT(error == 0);

	error = ddt_object_count(ddt, type, class, &count);
	ASSERT(error == 0);
	if (count == 0)
		return;

	dspace = doi.doi_physical_blocks_512 << 9;
	mspace = doi.doi_fill_count * doi.doi_data_block_size;

	ddt_object_name(ddt, type, class, name);

	(void) printf("%s: %llu entries, size %llu on disk, %llu in core\n",
	    name,
	    (u_longlong_t)count,
	    (u_longlong_t)(dspace / count),
	    (u_longlong_t)(mspace / count));

	if (dump_opt['D'] < 3)
		return;

	zpool_dump_ddt(NULL, &ddt->ddt_histogram[type][class]);

	if (dump_opt['D'] < 4)
		return;

	if (dump_opt['D'] < 5 && class == DDT_CLASS_UNIQUE)
		return;

	(void) printf("%s contents:\n\n", name);

	while ((error = ddt_object_walk(ddt, type, class, &walk, &dde)) == 0)
		dump_dde(ddt, &dde, walk);

	ASSERT(error == ENOENT);

	(void) printf("\n");
}

static void
dump_all_ddts(spa_t *spa)
{
	ddt_histogram_t ddh_total;
	ddt_stat_t dds_total;
	enum zio_checksum c;
	enum ddt_type type;
	enum ddt_class class;

	bzero(&ddh_total, sizeof (ddt_histogram_t));
	bzero(&dds_total, sizeof (ddt_stat_t));

	for (c = 0; c < ZIO_CHECKSUM_FUNCTIONS; c++) {
		ddt_t *ddt = spa->spa_ddt[c];
		for (type = 0; type < DDT_TYPES; type++) {
			for (class = 0; class < DDT_CLASSES;
			    class++) {
				dump_ddt(ddt, type, class);
			}
		}
	}

	ddt_get_dedup_stats(spa, &dds_total);

	if (dds_total.dds_blocks == 0) {
		(void) printf("All DDTs are empty\n");
		return;
	}

	(void) printf("\n");

	if (dump_opt['D'] > 1) {
		(void) printf("DDT histogram (aggregated over all DDTs):\n");
		ddt_get_dedup_histogram(spa, &ddh_total);
		zpool_dump_ddt(&dds_total, &ddh_total);
	}

	dump_dedup_ratio(&dds_total);
}

static void
dump_dtl_seg(void *arg, uint64_t start, uint64_t size)
{
	char *prefix = arg;

	(void) printf("%s [%llu,%llu) length %llu\n",
	    prefix,
	    (u_longlong_t)start,
	    (u_longlong_t)(start + size),
	    (u_longlong_t)(size));
}

static void
dump_dtl(vdev_t *vd, int indent)
{
	spa_t *spa = vd->vdev_spa;
	boolean_t required;
	char *name[DTL_TYPES] = { "missing", "partial", "scrub", "outage" };
	char prefix[256];
	int c, t;

	spa_vdev_state_enter(spa, SCL_NONE);
	required = vdev_dtl_required(vd);
	(void) spa_vdev_state_exit(spa, NULL, 0);

	if (indent == 0)
		(void) printf("\nDirty time logs:\n\n");

	(void) printf("\t%*s%s [%s]\n", indent, "",
	    vd->vdev_path ? vd->vdev_path :
	    vd->vdev_parent ? vd->vdev_ops->vdev_op_type : spa_name(spa),
	    required ? "DTL-required" : "DTL-expendable");

	for (t = 0; t < DTL_TYPES; t++) {
		range_tree_t *rt = vd->vdev_dtl[t];
		if (range_tree_space(rt) == 0)
			continue;
		(void) snprintf(prefix, sizeof (prefix), "\t%*s%s",
		    indent + 2, "", name[t]);
		mutex_enter(rt->rt_lock);
		range_tree_walk(rt, dump_dtl_seg, prefix);
		mutex_exit(rt->rt_lock);
		if (dump_opt['d'] > 5 && vd->vdev_children == 0)
			dump_spacemap(spa->spa_meta_objset,
			    vd->vdev_dtl_sm);
	}

	for (c = 0; c < vd->vdev_children; c++)
		dump_dtl(vd->vdev_child[c], indent + 4);
}

static void
dump_history(spa_t *spa)
{
	nvlist_t **events = NULL;
	char buf[SPA_MAXBLOCKSIZE];
	uint64_t resid, len, off = 0;
	uint_t num = 0;
	int error;
	time_t tsec;
	struct tm t;
	char tbuf[30];
	char internalstr[MAXPATHLEN];
	int i;

	do {
		len = sizeof (buf);

		if ((error = spa_history_get(spa, &off, &len, buf)) != 0) {
			(void) fprintf(stderr, "Unable to read history: "
			    "error %d\n", error);
			return;
		}

		if (zpool_history_unpack(buf, len, &resid, &events, &num) != 0)
			break;

		off -= resid;
	} while (len != 0);

	(void) printf("\nHistory:\n");
	for (i = 0; i < num; i++) {
		uint64_t time, txg, ievent;
		char *cmd, *intstr;
		boolean_t printed = B_FALSE;

		if (nvlist_lookup_uint64(events[i], ZPOOL_HIST_TIME,
		    &time) != 0)
			goto next;
		if (nvlist_lookup_string(events[i], ZPOOL_HIST_CMD,
		    &cmd) != 0) {
			if (nvlist_lookup_uint64(events[i],
			    ZPOOL_HIST_INT_EVENT, &ievent) != 0)
				goto next;
			verify(nvlist_lookup_uint64(events[i],
			    ZPOOL_HIST_TXG, &txg) == 0);
			verify(nvlist_lookup_string(events[i],
			    ZPOOL_HIST_INT_STR, &intstr) == 0);
			if (ievent >= ZFS_NUM_LEGACY_HISTORY_EVENTS)
				goto next;

			(void) snprintf(internalstr,
			    sizeof (internalstr),
			    "[internal %s txg:%lld] %s",
			    zfs_history_event_names[ievent],
			    (longlong_t)txg, intstr);
			cmd = internalstr;
		}
		tsec = time;
		(void) localtime_r(&tsec, &t);
		(void) strftime(tbuf, sizeof (tbuf), "%F.%T", &t);
		(void) printf("%s %s\n", tbuf, cmd);
		printed = B_TRUE;

next:
		if (dump_opt['h'] > 1) {
			if (!printed)
				(void) printf("unrecognized record:\n");
			dump_nvlist(events[i], 2);
		}
	}
}

/*ARGSUSED*/
static void
dump_dnode(objset_t *os, uint64_t object, void *data, size_t size)
{
}

static uint64_t
blkid2offset(const dnode_phys_t *dnp, const blkptr_t *bp,
    const zbookmark_phys_t *zb)
{
	if (dnp == NULL) {
		ASSERT(zb->zb_level < 0);
		if (zb->zb_object == 0)
			return (zb->zb_blkid);
		return (zb->zb_blkid * BP_GET_LSIZE(bp));
	}

	ASSERT(zb->zb_level >= 0);

	return ((zb->zb_blkid <<
	    (zb->zb_level * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT))) *
	    dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
}

static void
snprintf_blkptr_compact(char *blkbuf, size_t buflen, const blkptr_t *bp)
{
	const dva_t *dva = bp->blk_dva;
	int ndvas = dump_opt['d'] > 5 ? BP_GET_NDVAS(bp) : 1;
	int i;

	if (dump_opt['b'] >= 6) {
		snprintf_blkptr(blkbuf, buflen, bp);
		return;
	}

	if (BP_IS_EMBEDDED(bp)) {
		(void) sprintf(blkbuf,
		    "EMBEDDED et=%u %llxL/%llxP B=%llu",
		    (int)BPE_GET_ETYPE(bp),
		    (u_longlong_t)BPE_GET_LSIZE(bp),
		    (u_longlong_t)BPE_GET_PSIZE(bp),
		    (u_longlong_t)bp->blk_birth);
		return;
	}

	blkbuf[0] = '\0';

	for (i = 0; i < ndvas; i++)
		(void) snprintf(blkbuf + strlen(blkbuf),
		    buflen - strlen(blkbuf), "%llu:%llx:%llx ",
		    (u_longlong_t)DVA_GET_VDEV(&dva[i]),
		    (u_longlong_t)DVA_GET_OFFSET(&dva[i]),
		    (u_longlong_t)DVA_GET_ASIZE(&dva[i]));

	if (BP_IS_HOLE(bp)) {
		(void) snprintf(blkbuf + strlen(blkbuf),
		    buflen - strlen(blkbuf), "B=%llu",
		    (u_longlong_t)bp->blk_birth);
	} else {
		(void) snprintf(blkbuf + strlen(blkbuf),
		    buflen - strlen(blkbuf),
		    "%llxL/%llxP F=%llu B=%llu/%llu",
		    (u_longlong_t)BP_GET_LSIZE(bp),
		    (u_longlong_t)BP_GET_PSIZE(bp),
		    (u_longlong_t)BP_GET_FILL(bp),
		    (u_longlong_t)bp->blk_birth,
		    (u_longlong_t)BP_PHYSICAL_BIRTH(bp));
	}
}

static void
print_indirect(blkptr_t *bp, const zbookmark_phys_t *zb,
    const dnode_phys_t *dnp)
{
	char blkbuf[BP_SPRINTF_LEN];
	int l;

	if (!BP_IS_EMBEDDED(bp)) {
		ASSERT3U(BP_GET_TYPE(bp), ==, dnp->dn_type);
		ASSERT3U(BP_GET_LEVEL(bp), ==, zb->zb_level);
	}

	(void) printf("%16llx ", (u_longlong_t)blkid2offset(dnp, bp, zb));

	ASSERT(zb->zb_level >= 0);

	for (l = dnp->dn_nlevels - 1; l >= -1; l--) {
		if (l == zb->zb_level) {
			(void) printf("L%llx", (u_longlong_t)zb->zb_level);
		} else {
			(void) printf(" ");
		}
	}

	snprintf_blkptr_compact(blkbuf, sizeof (blkbuf), bp);
	(void) printf("%s\n", blkbuf);
}

static int
visit_indirect(spa_t *spa, const dnode_phys_t *dnp,
    blkptr_t *bp, const zbookmark_phys_t *zb)
{
	int err = 0;

	if (bp->blk_birth == 0)
		return (0);

	print_indirect(bp, zb, dnp);

	if (BP_GET_LEVEL(bp) > 0 && !BP_IS_HOLE(bp)) {
		uint32_t flags = ARC_WAIT;
		int i;
		blkptr_t *cbp;
		int epb = BP_GET_LSIZE(bp) >> SPA_BLKPTRSHIFT;
		arc_buf_t *buf;
		uint64_t fill = 0;

		err = arc_read(NULL, spa, bp, arc_getbuf_func, &buf,
		    ZIO_PRIORITY_ASYNC_READ, ZIO_FLAG_CANFAIL, &flags, zb);
		if (err)
			return (err);
		ASSERT(buf->b_data);

		/* recursively visit blocks below this */
		cbp = buf->b_data;
		for (i = 0; i < epb; i++, cbp++) {
			zbookmark_phys_t czb;

			SET_BOOKMARK(&czb, zb->zb_objset, zb->zb_object,
			    zb->zb_level - 1,
			    zb->zb_blkid * epb + i);
			err = visit_indirect(spa, dnp, cbp, &czb);
			if (err)
				break;
			fill += BP_GET_FILL(cbp);
		}
		if (!err)
			ASSERT3U(fill, ==, BP_GET_FILL(bp));
		(void) arc_buf_remove_ref(buf, &buf);
	}

	return (err);
}

/*ARGSUSED*/
static void
dump_indirect(dnode_t *dn)
{
	dnode_phys_t *dnp = dn->dn_phys;
	int j;
	zbookmark_phys_t czb;

	(void) printf("Indirect blocks:\n");

	SET_BOOKMARK(&czb, dmu_objset_id(dn->dn_objset),
	    dn->dn_object, dnp->dn_nlevels - 1, 0);
	for (j = 0; j < dnp->dn_nblkptr; j++) {
		czb.zb_blkid = j;
		(void) visit_indirect(dmu_objset_spa(dn->dn_objset), dnp,
		    &dnp->dn_blkptr[j], &czb);
	}

	(void) printf("\n");
}

/*ARGSUSED*/
static void
dump_dsl_dir(objset_t *os, uint64_t object, void *data, size_t size)
{
	dsl_dir_phys_t *dd = data;
	time_t crtime;
	char nice[32];

	if (dd == NULL)
		return;

	ASSERT3U(size, >=, sizeof (dsl_dir_phys_t));

	crtime = dd->dd_creation_time;
	(void) printf("\t\tcreation_time = %s", ctime(&crtime));
	(void) printf("\t\thead_dataset_obj = %llu\n",
	    (u_longlong_t)dd->dd_head_dataset_obj);
	(void) printf("\t\tparent_dir_obj = %llu\n",
	    (u_longlong_t)dd->dd_parent_obj);
	(void) printf("\t\torigin_obj = %llu\n",
	    (u_longlong_t)dd->dd_origin_obj);
	(void) printf("\t\tchild_dir_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_child_dir_zapobj);
	zdb_nicenum(dd->dd_used_bytes, nice);
	(void) printf("\t\tused_bytes = %s\n", nice);
	zdb_nicenum(dd->dd_compressed_bytes, nice);
	(void) printf("\t\tcompressed_bytes = %s\n", nice);
	zdb_nicenum(dd->dd_uncompressed_bytes, nice);
	(void) printf("\t\tuncompressed_bytes = %s\n", nice);
	zdb_nicenum(dd->dd_quota, nice);
	(void) printf("\t\tquota = %s\n", nice);
	zdb_nicenum(dd->dd_reserved, nice);
	(void) printf("\t\treserved = %s\n", nice);
	(void) printf("\t\tprops_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_props_zapobj);
	(void) printf("\t\tdeleg_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_deleg_zapobj);
	(void) printf("\t\tflags = %llx\n",
	    (u_longlong_t)dd->dd_flags);

#define	DO(which) \
	zdb_nicenum(dd->dd_used_breakdown[DD_USED_ ## which], nice); \
	(void) printf("\t\tused_breakdown[" #which "] = %s\n", nice)
	DO(HEAD);
	DO(SNAP);
	DO(CHILD);
	DO(CHILD_RSRV);
	DO(REFRSRV);
#undef DO
}

/*ARGSUSED*/
static void
dump_dsl_dataset(objset_t *os, uint64_t object, void *data, size_t size)
{
	dsl_dataset_phys_t *ds = data;
	time_t crtime;
	char used[32], compressed[32], uncompressed[32], unique[32];
	char blkbuf[BP_SPRINTF_LEN];

	if (ds == NULL)
		return;

	ASSERT(size == sizeof (*ds));
	crtime = ds->ds_creation_time;
	zdb_nicenum(ds->ds_referenced_bytes, used);
	zdb_nicenum(ds->ds_compressed_bytes, compressed);
	zdb_nicenum(ds->ds_uncompressed_bytes, uncompressed);
	zdb_nicenum(ds->ds_unique_bytes, unique);
	snprintf_blkptr(blkbuf, sizeof (blkbuf), &ds->ds_bp);

	(void) printf("\t\tdir_obj = %llu\n",
	    (u_longlong_t)ds->ds_dir_obj);
	(void) printf("\t\tprev_snap_obj = %llu\n",
	    (u_longlong_t)ds->ds_prev_snap_obj);
	(void) printf("\t\tprev_snap_txg = %llu\n",
	    (u_longlong_t)ds->ds_prev_snap_txg);
	(void) printf("\t\tnext_snap_obj = %llu\n",
	    (u_longlong_t)ds->ds_next_snap_obj);
	(void) printf("\t\tsnapnames_zapobj = %llu\n",
	    (u_longlong_t)ds->ds_snapnames_zapobj);
	(void) printf("\t\tnum_children = %llu\n",
	    (u_longlong_t)ds->ds_num_children);
	(void) printf("\t\tuserrefs_obj = %llu\n",
	    (u_longlong_t)ds->ds_userrefs_obj);
	(void) printf("\t\tcreation_time = %s", ctime(&crtime));
	(void) printf("\t\tcreation_txg = %llu\n",
	    (u_longlong_t)ds->ds_creation_txg);
	(void) printf("\t\tdeadlist_obj = %llu\n",
	    (u_longlong_t)ds->ds_deadlist_obj);
	(void) printf("\t\tused_bytes = %s\n", used);
	(void) printf("\t\tcompressed_bytes = %s\n", compressed);
	(void) printf("\t\tuncompressed_bytes = %s\n", uncompressed);
	(void) printf("\t\tunique = %s\n", unique);
	(void) printf("\t\tfsid_guid = %llu\n",
	    (u_longlong_t)ds->ds_fsid_guid);
	(void) printf("\t\tguid = %llu\n",
	    (u_longlong_t)ds->ds_guid);
	(void) printf("\t\tflags = %llx\n",
	    (u_longlong_t)ds->ds_flags);
	(void) printf("\t\tnext_clones_obj = %llu\n",
	    (u_longlong_t)ds->ds_next_clones_obj);
	(void) printf("\t\tprops_obj = %llu\n",
	    (u_longlong_t)ds->ds_props_obj);
	(void) printf("\t\tbp = %s\n", blkbuf);
}

/* ARGSUSED */
static int
dump_bptree_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	char blkbuf[BP_SPRINTF_LEN];

	if (bp->blk_birth != 0) {
		snprintf_blkptr(blkbuf, sizeof (blkbuf), bp);
		(void) printf("\t%s\n", blkbuf);
	}
	return (0);
}

static void
dump_bptree(objset_t *os, uint64_t obj, char *name)
{
	char bytes[32];
	bptree_phys_t *bt;
	dmu_buf_t *db;

	if (dump_opt['d'] < 3)
		return;

	VERIFY3U(0, ==, dmu_bonus_hold(os, obj, FTAG, &db));
	bt = db->db_data;
	zdb_nicenum(bt->bt_bytes, bytes);
	(void) printf("\n    %s: %llu datasets, %s\n",
	    name, (unsigned long long)(bt->bt_end - bt->bt_begin), bytes);
	dmu_buf_rele(db, FTAG);

	if (dump_opt['d'] < 5)
		return;

	(void) printf("\n");

	(void) bptree_iterate(os, obj, B_FALSE, dump_bptree_cb, NULL, NULL);
}

/* ARGSUSED */
static int
dump_bpobj_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	char blkbuf[BP_SPRINTF_LEN];

	ASSERT(bp->blk_birth != 0);
	snprintf_blkptr_compact(blkbuf, sizeof (blkbuf), bp);
	(void) printf("\t%s\n", blkbuf);
	return (0);
}

static void
dump_bpobj(bpobj_t *bpo, char *name, int indent)
{
	char bytes[32];
	char comp[32];
	char uncomp[32];
	uint64_t i;

	if (dump_opt['d'] < 3)
		return;

	zdb_nicenum(bpo->bpo_phys->bpo_bytes, bytes);
	if (bpo->bpo_havesubobj && bpo->bpo_phys->bpo_subobjs != 0) {
		zdb_nicenum(bpo->bpo_phys->bpo_comp, comp);
		zdb_nicenum(bpo->bpo_phys->bpo_uncomp, uncomp);
		(void) printf("    %*s: object %llu, %llu local blkptrs, "
		    "%llu subobjs, %s (%s/%s comp)\n",
		    indent * 8, name,
		    (u_longlong_t)bpo->bpo_object,
		    (u_longlong_t)bpo->bpo_phys->bpo_num_blkptrs,
		    (u_longlong_t)bpo->bpo_phys->bpo_num_subobjs,
		    bytes, comp, uncomp);

		for (i = 0; i < bpo->bpo_phys->bpo_num_subobjs; i++) {
			uint64_t subobj;
			bpobj_t subbpo;
			int error;
			VERIFY0(dmu_read(bpo->bpo_os,
			    bpo->bpo_phys->bpo_subobjs,
			    i * sizeof (subobj), sizeof (subobj), &subobj, 0));
			error = bpobj_open(&subbpo, bpo->bpo_os, subobj);
			if (error != 0) {
				(void) printf("ERROR %u while trying to open "
				    "subobj id %llu\n",
				    error, (u_longlong_t)subobj);
				continue;
			}
			dump_bpobj(&subbpo, "subobj", indent + 1);
		}
	} else {
		(void) printf("    %*s: object %llu, %llu blkptrs, %s\n",
		    indent * 8, name,
		    (u_longlong_t)bpo->bpo_object,
		    (u_longlong_t)bpo->bpo_phys->bpo_num_blkptrs,
		    bytes);
	}

	if (dump_opt['d'] < 5)
		return;


	if (indent == 0) {
		(void) bpobj_iterate_nofree(bpo, dump_bpobj_cb, NULL, NULL);
		(void) printf("\n");
	}
}

static void
dump_deadlist(dsl_deadlist_t *dl)
{
	dsl_deadlist_entry_t *dle;
	uint64_t unused;
	char bytes[32];
	char comp[32];
	char uncomp[32];

	if (dump_opt['d'] < 3)
		return;

	zdb_nicenum(dl->dl_phys->dl_used, bytes);
	zdb_nicenum(dl->dl_phys->dl_comp, comp);
	zdb_nicenum(dl->dl_phys->dl_uncomp, uncomp);
	(void) printf("\n    Deadlist: %s (%s/%s comp)\n",
	    bytes, comp, uncomp);

	if (dump_opt['d'] < 4)
		return;

	(void) printf("\n");

	/* force the tree to be loaded */
	dsl_deadlist_space_range(dl, 0, UINT64_MAX, &unused, &unused, &unused);

	for (dle = avl_first(&dl->dl_tree); dle;
	    dle = AVL_NEXT(&dl->dl_tree, dle)) {
		if (dump_opt['d'] >= 5) {
			char buf[128];
			(void) snprintf(buf, sizeof (buf),
			    "mintxg %llu -> obj %llu",
			    (longlong_t)dle->dle_mintxg,
			    (longlong_t)dle->dle_bpobj.bpo_object);

			dump_bpobj(&dle->dle_bpobj, buf, 0);
		} else {
			(void) printf("mintxg %llu -> obj %llu\n",
			    (longlong_t)dle->dle_mintxg,
			    (longlong_t)dle->dle_bpobj.bpo_object);

		}
	}
}

static avl_tree_t idx_tree;
static avl_tree_t domain_tree;
static boolean_t fuid_table_loaded;
static boolean_t sa_loaded;
sa_attr_type_t *sa_attr_table;

static void
fuid_table_destroy(void)
{
	if (fuid_table_loaded) {
		zfs_fuid_table_destroy(&idx_tree, &domain_tree);
		fuid_table_loaded = B_FALSE;
	}
}

/*
 * print uid or gid information.
 * For normal POSIX id just the id is printed in decimal format.
 * For CIFS files with FUID the fuid is printed in hex followed by
 * the domain-rid string.
 */
static void
print_idstr(uint64_t id, const char *id_type)
{
	if (FUID_INDEX(id)) {
		char *domain;

		domain = zfs_fuid_idx_domain(&idx_tree, FUID_INDEX(id));
		(void) printf("\t%s     %llx [%s-%d]\n", id_type,
		    (u_longlong_t)id, domain, (int)FUID_RID(id));
	} else {
		(void) printf("\t%s     %llu\n", id_type, (u_longlong_t)id);
	}

}

static void
dump_uidgid(objset_t *os, uint64_t uid, uint64_t gid)
{
	uint32_t uid_idx, gid_idx;

	uid_idx = FUID_INDEX(uid);
	gid_idx = FUID_INDEX(gid);

	/* Load domain table, if not already loaded */
	if (!fuid_table_loaded && (uid_idx || gid_idx)) {
		uint64_t fuid_obj;

		/* first find the fuid object.  It lives in the master node */
		VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES,
		    8, 1, &fuid_obj) == 0);
		zfs_fuid_avl_tree_create(&idx_tree, &domain_tree);
		(void) zfs_fuid_table_load(os, fuid_obj,
		    &idx_tree, &domain_tree);
		fuid_table_loaded = B_TRUE;
	}

	print_idstr(uid, "uid");
	print_idstr(gid, "gid");
}

static void
dump_znode_sa_xattr(sa_handle_t *hdl)
{
	nvlist_t *sa_xattr;
	nvpair_t *elem = NULL;
	int sa_xattr_size = 0;
	int sa_xattr_entries = 0;
	int error;
	char *sa_xattr_packed;

	error = sa_size(hdl, sa_attr_table[ZPL_DXATTR], &sa_xattr_size);
	if (error || sa_xattr_size == 0)
		return;

	sa_xattr_packed = malloc(sa_xattr_size);
	if (sa_xattr_packed == NULL)
		return;

	error = sa_lookup(hdl, sa_attr_table[ZPL_DXATTR],
	    sa_xattr_packed, sa_xattr_size);
	if (error) {
		free(sa_xattr_packed);
		return;
	}

	error = nvlist_unpack(sa_xattr_packed, sa_xattr_size, &sa_xattr, 0);
	if (error) {
		free(sa_xattr_packed);
		return;
	}

	while ((elem = nvlist_next_nvpair(sa_xattr, elem)) != NULL)
		sa_xattr_entries++;

	(void) printf("\tSA xattrs: %d bytes, %d entries\n\n",
	    sa_xattr_size, sa_xattr_entries);
	while ((elem = nvlist_next_nvpair(sa_xattr, elem)) != NULL) {
		uchar_t *value;
		uint_t cnt, idx;

		(void) printf("\t\t%s = ", nvpair_name(elem));
		nvpair_value_byte_array(elem, &value, &cnt);
		for (idx = 0; idx < cnt; ++idx) {
			if (isprint(value[idx]))
				(void) putchar(value[idx]);
			else
				(void) printf("\\%3.3o", value[idx]);
		}
		(void) putchar('\n');
	}

	nvlist_free(sa_xattr);
	free(sa_xattr_packed);
}

/*ARGSUSED*/
static void
dump_znode(objset_t *os, uint64_t object, void *data, size_t size)
{
	char path[MAXPATHLEN * 2];	/* allow for xattr and failure prefix */
	sa_handle_t *hdl;
	uint64_t xattr, rdev, gen;
	uint64_t uid, gid, mode, fsize, parent, links;
	uint64_t pflags;
	uint64_t acctm[2], modtm[2], chgtm[2], crtm[2];
	time_t z_crtime, z_atime, z_mtime, z_ctime;
	sa_bulk_attr_t bulk[12];
	int idx = 0;
	int error;

	if (!sa_loaded) {
		uint64_t sa_attrs = 0;
		uint64_t version;

		VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZPL_VERSION_STR,
		    8, 1, &version) == 0);
		if (version >= ZPL_VERSION_SA) {
			VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS,
			    8, 1, &sa_attrs) == 0);
		}
		if ((error = sa_setup(os, sa_attrs, zfs_attr_table,
		    ZPL_END, &sa_attr_table)) != 0) {
			(void) printf("sa_setup failed errno %d, can't "
			    "display znode contents\n", error);
			return;
		}
		sa_loaded = B_TRUE;
	}

	if (sa_handle_get(os, object, NULL, SA_HDL_PRIVATE, &hdl)) {
		(void) printf("Failed to get handle for SA znode\n");
		return;
	}

	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_UID], NULL, &uid, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_GID], NULL, &gid, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_LINKS], NULL,
	    &links, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_GEN], NULL, &gen, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_MODE], NULL,
	    &mode, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_PARENT],
	    NULL, &parent, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_SIZE], NULL,
	    &fsize, 8);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_ATIME], NULL,
	    acctm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_MTIME], NULL,
	    modtm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_CRTIME], NULL,
	    crtm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_CTIME], NULL,
	    chgtm, 16);
	SA_ADD_BULK_ATTR(bulk, idx, sa_attr_table[ZPL_FLAGS], NULL,
	    &pflags, 8);

	if (sa_bulk_lookup(hdl, bulk, idx)) {
		(void) sa_handle_destroy(hdl);
		return;
	}

	error = zfs_obj_to_path(os, object, path, sizeof (path));
	if (error != 0) {
		(void) snprintf(path, sizeof (path), "\?\?\?<object#%llu>",
		    (u_longlong_t)object);
	}
	if (dump_opt['d'] < 3) {
		(void) printf("\t%s\n", path);
		(void) sa_handle_destroy(hdl);
		return;
	}

	z_crtime = (time_t)crtm[0];
	z_atime = (time_t)acctm[0];
	z_mtime = (time_t)modtm[0];
	z_ctime = (time_t)chgtm[0];

	(void) printf("\tpath	%s\n", path);
	dump_uidgid(os, uid, gid);
	(void) printf("\tatime	%s", ctime(&z_atime));
	(void) printf("\tmtime	%s", ctime(&z_mtime));
	(void) printf("\tctime	%s", ctime(&z_ctime));
	(void) printf("\tcrtime	%s", ctime(&z_crtime));
	(void) printf("\tgen	%llu\n", (u_longlong_t)gen);
	(void) printf("\tmode	%llo\n", (u_longlong_t)mode);
	(void) printf("\tsize	%llu\n", (u_longlong_t)fsize);
	(void) printf("\tparent	%llu\n", (u_longlong_t)parent);
	(void) printf("\tlinks	%llu\n", (u_longlong_t)links);
	(void) printf("\tpflags	%llx\n", (u_longlong_t)pflags);
	if (sa_lookup(hdl, sa_attr_table[ZPL_XATTR], &xattr,
	    sizeof (uint64_t)) == 0)
		(void) printf("\txattr	%llu\n", (u_longlong_t)xattr);
	if (sa_lookup(hdl, sa_attr_table[ZPL_RDEV], &rdev,
	    sizeof (uint64_t)) == 0)
		(void) printf("\trdev	0x%016llx\n", (u_longlong_t)rdev);
	dump_znode_sa_xattr(hdl);
	sa_handle_destroy(hdl);
}

/*ARGSUSED*/
static void
dump_acl(objset_t *os, uint64_t object, void *data, size_t size)
{
}

/*ARGSUSED*/
static void
dump_dmu_objset(objset_t *os, uint64_t object, void *data, size_t size)
{
}

static object_viewer_t *object_viewer[DMU_OT_NUMTYPES + 1] = {
	dump_none,		/* unallocated			*/
	dump_zap,		/* object directory		*/
	dump_uint64,		/* object array			*/
	dump_none,		/* packed nvlist		*/
	dump_packed_nvlist,	/* packed nvlist size		*/
	dump_none,		/* bplist			*/
	dump_none,		/* bplist header		*/
	dump_none,		/* SPA space map header		*/
	dump_none,		/* SPA space map		*/
	dump_none,		/* ZIL intent log		*/
	dump_dnode,		/* DMU dnode			*/
	dump_dmu_objset,	/* DMU objset			*/
	dump_dsl_dir,		/* DSL directory		*/
	dump_zap,		/* DSL directory child map	*/
	dump_zap,		/* DSL dataset snap map		*/
	dump_zap,		/* DSL props			*/
	dump_dsl_dataset,	/* DSL dataset			*/
	dump_znode,		/* ZFS znode			*/
	dump_acl,		/* ZFS V0 ACL			*/
	dump_uint8,		/* ZFS plain file		*/
	dump_zpldir,		/* ZFS directory		*/
	dump_zap,		/* ZFS master node		*/
	dump_zap,		/* ZFS delete queue		*/
	dump_uint8,		/* zvol object			*/
	dump_zap,		/* zvol prop			*/
	dump_uint8,		/* other uint8[]		*/
	dump_uint64,		/* other uint64[]		*/
	dump_zap,		/* other ZAP			*/
	dump_zap,		/* persistent error log		*/
	dump_uint8,		/* SPA history			*/
	dump_history_offsets,	/* SPA history offsets		*/
	dump_zap,		/* Pool properties		*/
	dump_zap,		/* DSL permissions		*/
	dump_acl,		/* ZFS ACL			*/
	dump_uint8,		/* ZFS SYSACL			*/
	dump_none,		/* FUID nvlist			*/
	dump_packed_nvlist,	/* FUID nvlist size		*/
	dump_zap,		/* DSL dataset next clones	*/
	dump_zap,		/* DSL scrub queue		*/
	dump_zap,		/* ZFS user/group used		*/
	dump_zap,		/* ZFS user/group quota		*/
	dump_zap,		/* snapshot refcount tags	*/
	dump_ddt_zap,		/* DDT ZAP object		*/
	dump_zap,		/* DDT statistics		*/
	dump_znode,		/* SA object			*/
	dump_zap,		/* SA Master Node		*/
	dump_sa_attrs,		/* SA attribute registration	*/
	dump_sa_layouts,	/* SA attribute layouts		*/
	dump_zap,		/* DSL scrub translations	*/
	dump_none,		/* fake dedup BP		*/
	dump_zap,		/* deadlist			*/
	dump_none,		/* deadlist hdr			*/
	dump_zap,		/* dsl clones			*/
	dump_none,		/* bpobj subobjs		*/
	dump_unknown,		/* Unknown type, must be last	*/
};

static void
dump_object(objset_t *os, uint64_t object, int verbosity, int *print_header)
{
	dmu_buf_t *db = NULL;
	dmu_object_info_t doi;
	dnode_t *dn;
	void *bonus = NULL;
	size_t bsize = 0;
	char iblk[32], dblk[32], lsize[32], asize[32], fill[32];
	char bonus_size[32];
	char aux[50];
	int error;

	if (*print_header) {
		(void) printf("\n%10s  %3s  %5s  %5s  %5s  %5s  %6s  %s\n",
		    "Object", "lvl", "iblk", "dblk", "dsize", "lsize",
		    "%full", "type");
		*print_header = 0;
	}

	if (object == 0) {
		dn = DMU_META_DNODE(os);
	} else {
		error = dmu_bonus_hold(os, object, FTAG, &db);
		if (error)
			fatal("dmu_bonus_hold(%llu) failed, errno %u",
			    object, error);
		bonus = db->db_data;
		bsize = db->db_size;
		dn = DB_DNODE((dmu_buf_impl_t *)db);
	}
	dmu_object_info_from_dnode(dn, &doi);

	zdb_nicenum(doi.doi_metadata_block_size, iblk);
	zdb_nicenum(doi.doi_data_block_size, dblk);
	zdb_nicenum(doi.doi_max_offset, lsize);
	zdb_nicenum(doi.doi_physical_blocks_512 << 9, asize);
	zdb_nicenum(doi.doi_bonus_size, bonus_size);
	(void) sprintf(fill, "%6.2f", 100.0 * doi.doi_fill_count *
	    doi.doi_data_block_size / (object == 0 ? DNODES_PER_BLOCK : 1) /
	    doi.doi_max_offset);

	aux[0] = '\0';

	if (doi.doi_checksum != ZIO_CHECKSUM_INHERIT || verbosity >= 6) {
		(void) snprintf(aux + strlen(aux), sizeof (aux), " (K=%s)",
		    ZDB_CHECKSUM_NAME(doi.doi_checksum));
	}

	if (doi.doi_compress != ZIO_COMPRESS_INHERIT || verbosity >= 6) {
		(void) snprintf(aux + strlen(aux), sizeof (aux), " (Z=%s)",
		    ZDB_COMPRESS_NAME(doi.doi_compress));
	}

	(void) printf("%10lld  %3u  %5s  %5s  %5s  %5s  %6s  %s%s\n",
	    (u_longlong_t)object, doi.doi_indirection, iblk, dblk,
	    asize, lsize, fill, ZDB_OT_NAME(doi.doi_type), aux);

	if (doi.doi_bonus_type != DMU_OT_NONE && verbosity > 3) {
		(void) printf("%10s  %3s  %5s  %5s  %5s  %5s  %6s  %s\n",
		    "", "", "", "", "", bonus_size, "bonus",
		    ZDB_OT_NAME(doi.doi_bonus_type));
	}

	if (verbosity >= 4) {
		(void) printf("\tdnode flags: %s%s%s\n",
		    (dn->dn_phys->dn_flags & DNODE_FLAG_USED_BYTES) ?
		    "USED_BYTES " : "",
		    (dn->dn_phys->dn_flags & DNODE_FLAG_USERUSED_ACCOUNTED) ?
		    "USERUSED_ACCOUNTED " : "",
		    (dn->dn_phys->dn_flags & DNODE_FLAG_SPILL_BLKPTR) ?
		    "SPILL_BLKPTR" : "");
		(void) printf("\tdnode maxblkid: %llu\n",
		    (longlong_t)dn->dn_phys->dn_maxblkid);

		object_viewer[ZDB_OT_TYPE(doi.doi_bonus_type)](os, object,
		    bonus, bsize);
		object_viewer[ZDB_OT_TYPE(doi.doi_type)](os, object, NULL, 0);
		*print_header = 1;
	}

	if (verbosity >= 5)
		dump_indirect(dn);

	if (verbosity >= 5) {
		/*
		 * Report the list of segments that comprise the object.
		 */
		uint64_t start = 0;
		uint64_t end;
		uint64_t blkfill = 1;
		int minlvl = 1;

		if (dn->dn_type == DMU_OT_DNODE) {
			minlvl = 0;
			blkfill = DNODES_PER_BLOCK;
		}

		for (;;) {
			char segsize[32];
			error = dnode_next_offset(dn,
			    0, &start, minlvl, blkfill, 0);
			if (error)
				break;
			end = start;
			error = dnode_next_offset(dn,
			    DNODE_FIND_HOLE, &end, minlvl, blkfill, 0);
			zdb_nicenum(end - start, segsize);
			(void) printf("\t\tsegment [%016llx, %016llx)"
			    " size %5s\n", (u_longlong_t)start,
			    (u_longlong_t)end, segsize);
			if (error)
				break;
			start = end;
		}
	}

	if (db != NULL)
		dmu_buf_rele(db, FTAG);
}

static char *objset_types[DMU_OST_NUMTYPES] = {
	"NONE", "META", "ZPL", "ZVOL", "OTHER", "ANY" };

static void
dump_dir(objset_t *os)
{
	dmu_objset_stats_t dds;
	uint64_t object, object_count;
	uint64_t refdbytes, usedobjs, scratch;
	char numbuf[32];
	char blkbuf[BP_SPRINTF_LEN + 20];
	char osname[MAXNAMELEN];
	char *type = "UNKNOWN";
	int verbosity = dump_opt['d'];
	int print_header = 1;
	int i, error;

	dsl_pool_config_enter(dmu_objset_pool(os), FTAG);
	dmu_objset_fast_stat(os, &dds);
	dsl_pool_config_exit(dmu_objset_pool(os), FTAG);

	if (dds.dds_type < DMU_OST_NUMTYPES)
		type = objset_types[dds.dds_type];

	if (dds.dds_type == DMU_OST_META) {
		dds.dds_creation_txg = TXG_INITIAL;
		usedobjs = BP_GET_FILL(os->os_rootbp);
		refdbytes = os->os_spa->spa_dsl_pool->
		    dp_mos_dir->dd_phys->dd_used_bytes;
	} else {
		dmu_objset_space(os, &refdbytes, &scratch, &usedobjs, &scratch);
	}

	ASSERT3U(usedobjs, ==, BP_GET_FILL(os->os_rootbp));

	zdb_nicenum(refdbytes, numbuf);

	if (verbosity >= 4) {
		(void) snprintf(blkbuf, sizeof (blkbuf), ", rootbp ");
		(void) snprintf_blkptr(blkbuf + strlen(blkbuf),
		    sizeof (blkbuf) - strlen(blkbuf), os->os_rootbp);
	} else {
		blkbuf[0] = '\0';
	}

	dmu_objset_name(os, osname);

	(void) printf("Dataset %s [%s], ID %llu, cr_txg %llu, "
	    "%s, %llu objects%s\n",
	    osname, type, (u_longlong_t)dmu_objset_id(os),
	    (u_longlong_t)dds.dds_creation_txg,
	    numbuf, (u_longlong_t)usedobjs, blkbuf);

	if (zopt_objects != 0) {
		for (i = 0; i < zopt_objects; i++)
			dump_object(os, zopt_object[i], verbosity,
			    &print_header);
		(void) printf("\n");
		return;
	}

	if (dump_opt['i'] != 0 || verbosity >= 2)
		dump_intent_log(dmu_objset_zil(os));

	if (dmu_objset_ds(os) != NULL)
		dump_deadlist(&dmu_objset_ds(os)->ds_deadlist);

	if (verbosity < 2)
		return;

	if (BP_IS_HOLE(os->os_rootbp))
		return;

	dump_object(os, 0, verbosity, &print_header);
	object_count = 0;
	if (DMU_USERUSED_DNODE(os) != NULL &&
	    DMU_USERUSED_DNODE(os)->dn_type != 0) {
		dump_object(os, DMU_USERUSED_OBJECT, verbosity, &print_header);
		dump_object(os, DMU_GROUPUSED_OBJECT, verbosity, &print_header);
	}

	object = 0;
	while ((error = dmu_object_next(os, &object, B_FALSE, 0)) == 0) {
		dump_object(os, object, verbosity, &print_header);
		object_count++;
	}

	ASSERT3U(object_count, ==, usedobjs);

	(void) printf("\n");

	if (error != ESRCH) {
		(void) fprintf(stderr, "dmu_object_next() = %d\n", error);
		abort();
	}
}

static void
dump_uberblock(uberblock_t *ub, const char *header, const char *footer)
{
	time_t timestamp = ub->ub_timestamp;

	(void) printf("%s", header ? header : "");
	(void) printf("\tmagic = %016llx\n", (u_longlong_t)ub->ub_magic);
	(void) printf("\tversion = %llu\n", (u_longlong_t)ub->ub_version);
	(void) printf("\ttxg = %llu\n", (u_longlong_t)ub->ub_txg);
	(void) printf("\tguid_sum = %llu\n", (u_longlong_t)ub->ub_guid_sum);
	(void) printf("\ttimestamp = %llu UTC = %s",
	    (u_longlong_t)ub->ub_timestamp, asctime(localtime(&timestamp)));
	if (dump_opt['u'] >= 3) {
		char blkbuf[BP_SPRINTF_LEN];
		snprintf_blkptr(blkbuf, sizeof (blkbuf), &ub->ub_rootbp);
		(void) printf("\trootbp = %s\n", blkbuf);
	}
	(void) printf("%s", footer ? footer : "");
}

static void
dump_config(spa_t *spa)
{
	dmu_buf_t *db;
	size_t nvsize = 0;
	int error = 0;


	error = dmu_bonus_hold(spa->spa_meta_objset,
	    spa->spa_config_object, FTAG, &db);

	if (error == 0) {
		nvsize = *(uint64_t *)db->db_data;
		dmu_buf_rele(db, FTAG);

		(void) printf("\nMOS Configuration:\n");
		dump_packed_nvlist(spa->spa_meta_objset,
		    spa->spa_config_object, (void *)&nvsize, 1);
	} else {
		(void) fprintf(stderr, "dmu_bonus_hold(%llu) failed, errno %d",
		    (u_longlong_t)spa->spa_config_object, error);
	}
}

static void
dump_cachefile(const char *cachefile)
{
	int fd;
	struct stat64 statbuf;
	char *buf;
	nvlist_t *config;

	if ((fd = open64(cachefile, O_RDONLY)) < 0) {
		(void) printf("cannot open '%s': %s\n", cachefile,
		    strerror(errno));
		exit(1);
	}

	if (fstat64(fd, &statbuf) != 0) {
		(void) printf("failed to stat '%s': %s\n", cachefile,
		    strerror(errno));
		exit(1);
	}

	if ((buf = malloc(statbuf.st_size)) == NULL) {
		(void) fprintf(stderr, "failed to allocate %llu bytes\n",
		    (u_longlong_t)statbuf.st_size);
		exit(1);
	}

	if (read(fd, buf, statbuf.st_size) != statbuf.st_size) {
		(void) fprintf(stderr, "failed to read %llu bytes\n",
		    (u_longlong_t)statbuf.st_size);
		exit(1);
	}

	(void) close(fd);

	if (nvlist_unpack(buf, statbuf.st_size, &config, 0) != 0) {
		(void) fprintf(stderr, "failed to unpack nvlist\n");
		exit(1);
	}

	free(buf);

	dump_nvlist(config, 0);

	nvlist_free(config);
}

#define	ZDB_MAX_UB_HEADER_SIZE 32

static void
dump_label_uberblocks(vdev_label_t *lbl, uint64_t ashift)
{
	vdev_t vd;
	vdev_t *vdp = &vd;
	char header[ZDB_MAX_UB_HEADER_SIZE];
	int i;

	vd.vdev_ashift = ashift;
	vdp->vdev_top = vdp;

	for (i = 0; i < VDEV_UBERBLOCK_COUNT(vdp); i++) {
		uint64_t uoff = VDEV_UBERBLOCK_OFFSET(vdp, i);
		uberblock_t *ub = (void *)((char *)lbl + uoff);

		if (uberblock_verify(ub))
			continue;
		(void) snprintf(header, ZDB_MAX_UB_HEADER_SIZE,
		    "Uberblock[%d]\n", i);
		dump_uberblock(ub, header, "");
	}
}

static void
dump_label(const char *dev)
{
	int fd;
	vdev_label_t label;
	char *path, *buf = label.vl_vdev_phys.vp_nvlist;
	size_t buflen = sizeof (label.vl_vdev_phys.vp_nvlist);
	struct stat64 statbuf;
	uint64_t psize, ashift;
	int len = strlen(dev) + 1;
	int l;

	if (strncmp(dev, "/dev/dsk/", 9) == 0) {
		len++;
		path = malloc(len);
		(void) snprintf(path, len, "%s%s", "/dev/rdsk/", dev + 9);
	} else {
		path = strdup(dev);
	}

	if ((fd = open64(path, O_RDONLY)) < 0) {
		(void) printf("cannot open '%s': %s\n", path, strerror(errno));
		free(path);
		exit(1);
	}

	if (fstat64_blk(fd, &statbuf) != 0) {
		(void) printf("failed to stat '%s': %s\n", path,
		    strerror(errno));
		free(path);
		(void) close(fd);
		exit(1);
	}

	psize = statbuf.st_size;
	psize = P2ALIGN(psize, (uint64_t)sizeof (vdev_label_t));

	for (l = 0; l < VDEV_LABELS; l++) {
		nvlist_t *config = NULL;

		(void) printf("--------------------------------------------\n");
		(void) printf("LABEL %d\n", l);
		(void) printf("--------------------------------------------\n");

		if (pread64(fd, &label, sizeof (label),
		    vdev_label_offset(psize, l, 0)) != sizeof (label)) {
			(void) printf("failed to read label %d\n", l);
			continue;
		}

		if (nvlist_unpack(buf, buflen, &config, 0) != 0) {
			(void) printf("failed to unpack label %d\n", l);
			ashift = SPA_MINBLOCKSHIFT;
		} else {
			nvlist_t *vdev_tree = NULL;

			dump_nvlist(config, 4);
			if ((nvlist_lookup_nvlist(config,
			    ZPOOL_CONFIG_VDEV_TREE, &vdev_tree) != 0) ||
			    (nvlist_lookup_uint64(vdev_tree,
			    ZPOOL_CONFIG_ASHIFT, &ashift) != 0))
				ashift = SPA_MINBLOCKSHIFT;
			nvlist_free(config);
		}
		if (dump_opt['u'])
			dump_label_uberblocks(&label, ashift);
	}

	free(path);
	(void) close(fd);
}

/*ARGSUSED*/
static int
dump_one_dir(const char *dsname, void *arg)
{
	int error;
	objset_t *os;

	error = dmu_objset_own(dsname, DMU_OST_ANY, B_TRUE, FTAG, &os);
	if (error) {
		(void) printf("Could not open %s, error %d\n", dsname, error);
		return (0);
	}
	dump_dir(os);
	dmu_objset_disown(os, FTAG);
	fuid_table_destroy();
	sa_loaded = B_FALSE;
	return (0);
}

/*
 * Block statistics.
 */
#define	PSIZE_HISTO_SIZE (SPA_MAXBLOCKSIZE / SPA_MINBLOCKSIZE + 1)
typedef struct zdb_blkstats {
	uint64_t zb_asize;
	uint64_t zb_lsize;
	uint64_t zb_psize;
	uint64_t zb_count;
	uint64_t zb_psize_histogram[PSIZE_HISTO_SIZE];
} zdb_blkstats_t;

/*
 * Extended object types to report deferred frees and dedup auto-ditto blocks.
 */
#define	ZDB_OT_DEFERRED	(DMU_OT_NUMTYPES + 0)
#define	ZDB_OT_DITTO	(DMU_OT_NUMTYPES + 1)
#define	ZDB_OT_OTHER	(DMU_OT_NUMTYPES + 2)
#define	ZDB_OT_TOTAL	(DMU_OT_NUMTYPES + 3)

static char *zdb_ot_extname[] = {
	"deferred free",
	"dedup ditto",
	"other",
	"Total",
};

#define	ZB_TOTAL	DN_MAX_LEVELS

typedef struct zdb_cb {
	zdb_blkstats_t	zcb_type[ZB_TOTAL + 1][ZDB_OT_TOTAL + 1];
	uint64_t	zcb_dedup_asize;
	uint64_t	zcb_dedup_blocks;
	uint64_t	zcb_embedded_blocks[NUM_BP_EMBEDDED_TYPES];
	uint64_t	zcb_embedded_histogram[NUM_BP_EMBEDDED_TYPES]
	    [BPE_PAYLOAD_SIZE];
	uint64_t	zcb_start;
	uint64_t	zcb_lastprint;
	uint64_t	zcb_totalasize;
	uint64_t	zcb_errors[256];
	int		zcb_readfails;
	int		zcb_haderrors;
	spa_t		*zcb_spa;
} zdb_cb_t;

static void
zdb_count_block(zdb_cb_t *zcb, zilog_t *zilog, const blkptr_t *bp,
    dmu_object_type_t type)
{
	uint64_t refcnt = 0;
	int i;

	ASSERT(type < ZDB_OT_TOTAL);

	if (zilog && zil_bp_tree_add(zilog, bp) != 0)
		return;

	for (i = 0; i < 4; i++) {
		int l = (i < 2) ? BP_GET_LEVEL(bp) : ZB_TOTAL;
		int t = (i & 1) ? type : ZDB_OT_TOTAL;
		zdb_blkstats_t *zb = &zcb->zcb_type[l][t];

		zb->zb_asize += BP_GET_ASIZE(bp);
		zb->zb_lsize += BP_GET_LSIZE(bp);
		zb->zb_psize += BP_GET_PSIZE(bp);
		zb->zb_count++;
		zb->zb_psize_histogram[BP_GET_PSIZE(bp) >> SPA_MINBLOCKSHIFT]++;
	}

	if (BP_IS_EMBEDDED(bp)) {
		zcb->zcb_embedded_blocks[BPE_GET_ETYPE(bp)]++;
		zcb->zcb_embedded_histogram[BPE_GET_ETYPE(bp)]
		    [BPE_GET_PSIZE(bp)]++;
		return;
	}

	if (dump_opt['L'])
		return;

	if (BP_GET_DEDUP(bp)) {
		ddt_t *ddt;
		ddt_entry_t *dde;

		ddt = ddt_select(zcb->zcb_spa, bp);
		ddt_enter(ddt);
		dde = ddt_lookup(ddt, bp, B_FALSE);

		if (dde == NULL) {
			refcnt = 0;
		} else {
			ddt_phys_t *ddp = ddt_phys_select(dde, bp);
			ddt_phys_decref(ddp);
			refcnt = ddp->ddp_refcnt;
			if (ddt_phys_total_refcnt(dde) == 0)
				ddt_remove(ddt, dde);
		}
		ddt_exit(ddt);
	}

	VERIFY3U(zio_wait(zio_claim(NULL, zcb->zcb_spa,
	    refcnt ? 0 : spa_first_txg(zcb->zcb_spa),
	    bp, NULL, NULL, ZIO_FLAG_CANFAIL)), ==, 0);
}

static void
zdb_blkptr_done(zio_t *zio)
{
	spa_t *spa = zio->io_spa;
	blkptr_t *bp = zio->io_bp;
	int ioerr = zio->io_error;
	zdb_cb_t *zcb = zio->io_private;
	zbookmark_phys_t *zb = &zio->io_bookmark;

	zio_data_buf_free(zio->io_data, zio->io_size);

	mutex_enter(&spa->spa_scrub_lock);
	spa->spa_scrub_inflight--;
	cv_broadcast(&spa->spa_scrub_io_cv);

	if (ioerr && !(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
		char blkbuf[BP_SPRINTF_LEN];

		zcb->zcb_haderrors = 1;
		zcb->zcb_errors[ioerr]++;

		if (dump_opt['b'] >= 2)
			snprintf_blkptr(blkbuf, sizeof (blkbuf), bp);
		else
			blkbuf[0] = '\0';

		(void) printf("zdb_blkptr_cb: "
		    "Got error %d reading "
		    "<%llu, %llu, %lld, %llx> %s -- skipping\n",
		    ioerr,
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)zb->zb_object,
		    (u_longlong_t)zb->zb_level,
		    (u_longlong_t)zb->zb_blkid,
		    blkbuf);
	}
	mutex_exit(&spa->spa_scrub_lock);
}

static int
zdb_blkptr_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	zdb_cb_t *zcb = arg;
	dmu_object_type_t type;
	boolean_t is_metadata;

	if (dump_opt['b'] >= 5 && bp->blk_birth > 0) {
		char blkbuf[BP_SPRINTF_LEN];
		snprintf_blkptr(blkbuf, sizeof (blkbuf), bp);
		(void) printf("objset %llu object %llu "
		    "level %lld offset 0x%llx %s\n",
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)zb->zb_object,
		    (longlong_t)zb->zb_level,
		    (u_longlong_t)blkid2offset(dnp, bp, zb),
		    blkbuf);
	}

	if (BP_IS_HOLE(bp))
		return (0);

	type = BP_GET_TYPE(bp);

	zdb_count_block(zcb, zilog, bp,
	    (type & DMU_OT_NEWTYPE) ? ZDB_OT_OTHER : type);

	is_metadata = (BP_GET_LEVEL(bp) != 0 || DMU_OT_IS_METADATA(type));

	if (!BP_IS_EMBEDDED(bp) &&
	    (dump_opt['c'] > 1 || (dump_opt['c'] && is_metadata))) {
		size_t size = BP_GET_PSIZE(bp);
		void *data = zio_data_buf_alloc(size);
		int flags = ZIO_FLAG_CANFAIL | ZIO_FLAG_SCRUB | ZIO_FLAG_RAW;

		/* If it's an intent log block, failure is expected. */
		if (zb->zb_level == ZB_ZIL_LEVEL)
			flags |= ZIO_FLAG_SPECULATIVE;

		mutex_enter(&spa->spa_scrub_lock);
		while (spa->spa_scrub_inflight > max_inflight)
			cv_wait(&spa->spa_scrub_io_cv, &spa->spa_scrub_lock);
		spa->spa_scrub_inflight++;
		mutex_exit(&spa->spa_scrub_lock);

		zio_nowait(zio_read(NULL, spa, bp, data, size,
		    zdb_blkptr_done, zcb, ZIO_PRIORITY_ASYNC_READ, flags, zb));
	}

	zcb->zcb_readfails = 0;

	if (dump_opt['b'] < 5 && isatty(STDERR_FILENO) &&
	    gethrtime() > zcb->zcb_lastprint + NANOSEC) {
		uint64_t now = gethrtime();
		char buf[10];
		uint64_t bytes = zcb->zcb_type[ZB_TOTAL][ZDB_OT_TOTAL].zb_asize;
		int kb_per_sec =
		    1 + bytes / (1 + ((now - zcb->zcb_start) / 1000 / 1000));
		int sec_remaining =
		    (zcb->zcb_totalasize - bytes) / 1024 / kb_per_sec;

		zfs_nicenum(bytes, buf, sizeof (buf));
		(void) fprintf(stderr,
		    "\r%5s completed (%4dMB/s) "
		    "estimated time remaining: %uhr %02umin %02usec        ",
		    buf, kb_per_sec / 1024,
		    sec_remaining / 60 / 60,
		    sec_remaining / 60 % 60,
		    sec_remaining % 60);

		zcb->zcb_lastprint = now;
	}

	return (0);
}

static void
zdb_leak(void *arg, uint64_t start, uint64_t size)
{
	vdev_t *vd = arg;

	(void) printf("leaked space: vdev %llu, offset 0x%llx, size %llu\n",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)start, (u_longlong_t)size);
}

static metaslab_ops_t zdb_metaslab_ops = {
	NULL	/* alloc */
};

static void
zdb_ddt_leak_init(spa_t *spa, zdb_cb_t *zcb)
{
	ddt_bookmark_t ddb = { 0 };
	ddt_entry_t dde;
	int error;
	int p;

	while ((error = ddt_walk(spa, &ddb, &dde)) == 0) {
		blkptr_t blk;
		ddt_phys_t *ddp = dde.dde_phys;

		if (ddb.ddb_class == DDT_CLASS_UNIQUE)
			return;

		ASSERT(ddt_phys_total_refcnt(&dde) > 1);

		for (p = 0; p < DDT_PHYS_TYPES; p++, ddp++) {
			if (ddp->ddp_phys_birth == 0)
				continue;
			ddt_bp_create(ddb.ddb_checksum,
			    &dde.dde_key, ddp, &blk);
			if (p == DDT_PHYS_DITTO) {
				zdb_count_block(zcb, NULL, &blk, ZDB_OT_DITTO);
			} else {
				zcb->zcb_dedup_asize +=
				    BP_GET_ASIZE(&blk) * (ddp->ddp_refcnt - 1);
				zcb->zcb_dedup_blocks++;
			}
		}
		if (!dump_opt['L']) {
			ddt_t *ddt = spa->spa_ddt[ddb.ddb_checksum];
			ddt_enter(ddt);
			VERIFY(ddt_lookup(ddt, &blk, B_TRUE) != NULL);
			ddt_exit(ddt);
		}
	}

	ASSERT(error == ENOENT);
}

static void
zdb_leak_init(spa_t *spa, zdb_cb_t *zcb)
{
	zcb->zcb_spa = spa;
	int c, m;

	if (!dump_opt['L']) {
		vdev_t *rvd = spa->spa_root_vdev;
		for (c = 0; c < rvd->vdev_children; c++) {
			vdev_t *vd = rvd->vdev_child[c];
			for (m = 0; m < vd->vdev_ms_count; m++) {
				metaslab_t *msp = vd->vdev_ms[m];
				mutex_enter(&msp->ms_lock);
				metaslab_unload(msp);

				/*
				 * For leak detection, we overload the metaslab
				 * ms_tree to contain allocated segments
				 * instead of free segments. As a result,
				 * we can't use the normal metaslab_load/unload
				 * interfaces.
				 */
				if (msp->ms_sm != NULL) {
					msp->ms_ops = &zdb_metaslab_ops;
					VERIFY0(space_map_load(msp->ms_sm,
					    msp->ms_tree, SM_ALLOC));
					msp->ms_loaded = B_TRUE;
				}
				mutex_exit(&msp->ms_lock);
			}
		}
	}

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	zdb_ddt_leak_init(spa, zcb);

	spa_config_exit(spa, SCL_CONFIG, FTAG);
}

static void
zdb_leak_fini(spa_t *spa)
{
	int c, m;

	if (!dump_opt['L']) {
		vdev_t *rvd = spa->spa_root_vdev;
		for (c = 0; c < rvd->vdev_children; c++) {
			vdev_t *vd = rvd->vdev_child[c];
			for (m = 0; m < vd->vdev_ms_count; m++) {
				metaslab_t *msp = vd->vdev_ms[m];
				mutex_enter(&msp->ms_lock);

				/*
				 * The ms_tree has been overloaded to
				 * contain allocated segments. Now that we
				 * finished traversing all blocks, any
				 * block that remains in the ms_tree
				 * represents an allocated block that we
				 * did not claim during the traversal.
				 * Claimed blocks would have been removed
				 * from the ms_tree.
				 */
				range_tree_vacate(msp->ms_tree, zdb_leak, vd);
				msp->ms_loaded = B_FALSE;

				mutex_exit(&msp->ms_lock);
			}
		}
	}
}

/* ARGSUSED */
static int
count_block_cb(void *arg, const blkptr_t *bp, dmu_tx_t *tx)
{
	zdb_cb_t *zcb = arg;

	if (dump_opt['b'] >= 5) {
		char blkbuf[BP_SPRINTF_LEN];
		snprintf_blkptr(blkbuf, sizeof (blkbuf), bp);
		(void) printf("[%s] %s\n",
		    "deferred free", blkbuf);
	}
	zdb_count_block(zcb, NULL, bp, ZDB_OT_DEFERRED);
	return (0);
}

static int
dump_block_stats(spa_t *spa)
{
	zdb_cb_t zcb;
	zdb_blkstats_t *zb, *tzb;
	uint64_t norm_alloc, norm_space, total_alloc, total_found;
	int flags = TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA | TRAVERSE_HARD;
	boolean_t leaks = B_FALSE;
	int e;
	bp_embedded_type_t i;

	(void) printf("\nTraversing all blocks %s%s%s%s%s...\n\n",
	    (dump_opt['c'] || !dump_opt['L']) ? "to verify " : "",
	    (dump_opt['c'] == 1) ? "metadata " : "",
	    dump_opt['c'] ? "checksums " : "",
	    (dump_opt['c'] && !dump_opt['L']) ? "and verify " : "",
	    !dump_opt['L'] ? "nothing leaked " : "");

	/*
	 * Load all space maps as SM_ALLOC maps, then traverse the pool
	 * claiming each block we discover.  If the pool is perfectly
	 * consistent, the space maps will be empty when we're done.
	 * Anything left over is a leak; any block we can't claim (because
	 * it's not part of any space map) is a double allocation,
	 * reference to a freed block, or an unclaimed log block.
	 */
	bzero(&zcb, sizeof (zdb_cb_t));
	zdb_leak_init(spa, &zcb);

	/*
	 * If there's a deferred-free bplist, process that first.
	 */
	(void) bpobj_iterate_nofree(&spa->spa_deferred_bpobj,
	    count_block_cb, &zcb, NULL);
	if (spa_version(spa) >= SPA_VERSION_DEADLISTS) {
		(void) bpobj_iterate_nofree(&spa->spa_dsl_pool->dp_free_bpobj,
		    count_block_cb, &zcb, NULL);
	}
	if (spa_feature_is_active(spa, SPA_FEATURE_ASYNC_DESTROY)) {
		VERIFY3U(0, ==, bptree_iterate(spa->spa_meta_objset,
		    spa->spa_dsl_pool->dp_bptree_obj, B_FALSE, count_block_cb,
		    &zcb, NULL));
	}

	if (dump_opt['c'] > 1)
		flags |= TRAVERSE_PREFETCH_DATA;

	zcb.zcb_totalasize = metaslab_class_get_alloc(spa_normal_class(spa));
	zcb.zcb_start = zcb.zcb_lastprint = gethrtime();
	zcb.zcb_haderrors |= traverse_pool(spa, 0, flags, zdb_blkptr_cb, &zcb);

	/*
	 * If we've traversed the data blocks then we need to wait for those
	 * I/Os to complete. We leverage "The Godfather" zio to wait on
	 * all async I/Os to complete.
	 */
	if (dump_opt['c']) {
		(void) zio_wait(spa->spa_async_zio_root);
		spa->spa_async_zio_root = zio_root(spa, NULL, NULL,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE |
		    ZIO_FLAG_GODFATHER);
	}

	if (zcb.zcb_haderrors) {
		(void) printf("\nError counts:\n\n");
		(void) printf("\t%5s  %s\n", "errno", "count");
		for (e = 0; e < 256; e++) {
			if (zcb.zcb_errors[e] != 0) {
				(void) printf("\t%5d  %llu\n",
				    e, (u_longlong_t)zcb.zcb_errors[e]);
			}
		}
	}

	/*
	 * Report any leaked segments.
	 */
	zdb_leak_fini(spa);

	tzb = &zcb.zcb_type[ZB_TOTAL][ZDB_OT_TOTAL];

	norm_alloc = metaslab_class_get_alloc(spa_normal_class(spa));
	norm_space = metaslab_class_get_space(spa_normal_class(spa));

	total_alloc = norm_alloc + metaslab_class_get_alloc(spa_log_class(spa));
	total_found = tzb->zb_asize - zcb.zcb_dedup_asize;

	if (total_found == total_alloc) {
		if (!dump_opt['L'])
			(void) printf("\n\tNo leaks (block sum matches space"
			    " maps exactly)\n");
	} else {
		(void) printf("block traversal size %llu != alloc %llu "
		    "(%s %lld)\n",
		    (u_longlong_t)total_found,
		    (u_longlong_t)total_alloc,
		    (dump_opt['L']) ? "unreachable" : "leaked",
		    (longlong_t)(total_alloc - total_found));
		leaks = B_TRUE;
	}

	if (tzb->zb_count == 0)
		return (2);

	(void) printf("\n");
	(void) printf("\tbp count:      %10llu\n",
	    (u_longlong_t)tzb->zb_count);
	(void) printf("\tbp logical:    %10llu      avg: %6llu\n",
	    (u_longlong_t)tzb->zb_lsize,
	    (u_longlong_t)(tzb->zb_lsize / tzb->zb_count));
	(void) printf("\tbp physical:   %10llu      avg:"
	    " %6llu     compression: %6.2f\n",
	    (u_longlong_t)tzb->zb_psize,
	    (u_longlong_t)(tzb->zb_psize / tzb->zb_count),
	    (double)tzb->zb_lsize / tzb->zb_psize);
	(void) printf("\tbp allocated:  %10llu      avg:"
	    " %6llu     compression: %6.2f\n",
	    (u_longlong_t)tzb->zb_asize,
	    (u_longlong_t)(tzb->zb_asize / tzb->zb_count),
	    (double)tzb->zb_lsize / tzb->zb_asize);
	(void) printf("\tbp deduped:    %10llu    ref>1:"
	    " %6llu   deduplication: %6.2f\n",
	    (u_longlong_t)zcb.zcb_dedup_asize,
	    (u_longlong_t)zcb.zcb_dedup_blocks,
	    (double)zcb.zcb_dedup_asize / tzb->zb_asize + 1.0);
	(void) printf("\tSPA allocated: %10llu     used: %5.2f%%\n",
	    (u_longlong_t)norm_alloc, 100.0 * norm_alloc / norm_space);

	for (i = 0; i < NUM_BP_EMBEDDED_TYPES; i++) {
		if (zcb.zcb_embedded_blocks[i] == 0)
			continue;
		(void) printf("\n");
		(void) printf("\tadditional, non-pointer bps of type %u: "
		    "%10llu\n",
		    i, (u_longlong_t)zcb.zcb_embedded_blocks[i]);

		if (dump_opt['b'] >= 3) {
			(void) printf("\t number of (compressed) bytes:  "
			    "number of bps\n");
			dump_histogram(zcb.zcb_embedded_histogram[i],
			    sizeof (zcb.zcb_embedded_histogram[i]) /
			    sizeof (zcb.zcb_embedded_histogram[i][0]), 0);
		}
	}

	if (dump_opt['b'] >= 2) {
		int l, t, level;
		(void) printf("\nBlocks\tLSIZE\tPSIZE\tASIZE"
		    "\t  avg\t comp\t%%Total\tType\n");

		for (t = 0; t <= ZDB_OT_TOTAL; t++) {
			char csize[32], lsize[32], psize[32], asize[32];
			char avg[32];
			char *typename;

			if (t < DMU_OT_NUMTYPES)
				typename = dmu_ot[t].ot_name;
			else
				typename = zdb_ot_extname[t - DMU_OT_NUMTYPES];

			if (zcb.zcb_type[ZB_TOTAL][t].zb_asize == 0) {
				(void) printf("%6s\t%5s\t%5s\t%5s"
				    "\t%5s\t%5s\t%6s\t%s\n",
				    "-",
				    "-",
				    "-",
				    "-",
				    "-",
				    "-",
				    "-",
				    typename);
				continue;
			}

			for (l = ZB_TOTAL - 1; l >= -1; l--) {
				level = (l == -1 ? ZB_TOTAL : l);
				zb = &zcb.zcb_type[level][t];

				if (zb->zb_asize == 0)
					continue;

				if (dump_opt['b'] < 3 && level != ZB_TOTAL)
					continue;

				if (level == 0 && zb->zb_asize ==
				    zcb.zcb_type[ZB_TOTAL][t].zb_asize)
					continue;

				zdb_nicenum(zb->zb_count, csize);
				zdb_nicenum(zb->zb_lsize, lsize);
				zdb_nicenum(zb->zb_psize, psize);
				zdb_nicenum(zb->zb_asize, asize);
				zdb_nicenum(zb->zb_asize / zb->zb_count, avg);

				(void) printf("%6s\t%5s\t%5s\t%5s\t%5s"
				    "\t%5.2f\t%6.2f\t",
				    csize, lsize, psize, asize, avg,
				    (double)zb->zb_lsize / zb->zb_psize,
				    100.0 * zb->zb_asize / tzb->zb_asize);

				if (level == ZB_TOTAL)
					(void) printf("%s\n", typename);
				else
					(void) printf("    L%d %s\n",
					    level, typename);

				if (dump_opt['b'] >= 4) {
					(void) printf("psize "
					    "(in 512-byte sectors): "
					    "number of blocks\n");
					dump_histogram(zb->zb_psize_histogram,
					    PSIZE_HISTO_SIZE, 0);
				}
			}
		}
	}

	(void) printf("\n");

	if (leaks)
		return (2);

	if (zcb.zcb_haderrors)
		return (3);

	return (0);
}

typedef struct zdb_ddt_entry {
	ddt_key_t	zdde_key;
	uint64_t	zdde_ref_blocks;
	uint64_t	zdde_ref_lsize;
	uint64_t	zdde_ref_psize;
	uint64_t	zdde_ref_dsize;
	avl_node_t	zdde_node;
} zdb_ddt_entry_t;

/* ARGSUSED */
static int
zdb_ddt_add_cb(spa_t *spa, zilog_t *zilog, const blkptr_t *bp,
    const zbookmark_phys_t *zb, const dnode_phys_t *dnp, void *arg)
{
	avl_tree_t *t = arg;
	avl_index_t where;
	zdb_ddt_entry_t *zdde, zdde_search;

	if (BP_IS_HOLE(bp) || BP_IS_EMBEDDED(bp))
		return (0);

	if (dump_opt['S'] > 1 && zb->zb_level == ZB_ROOT_LEVEL) {
		(void) printf("traversing objset %llu, %llu objects, "
		    "%lu blocks so far\n",
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)BP_GET_FILL(bp),
		    avl_numnodes(t));
	}

	if (BP_IS_HOLE(bp) || BP_GET_CHECKSUM(bp) == ZIO_CHECKSUM_OFF ||
	    BP_GET_LEVEL(bp) > 0 || DMU_OT_IS_METADATA(BP_GET_TYPE(bp)))
		return (0);

	ddt_key_fill(&zdde_search.zdde_key, bp);

	zdde = avl_find(t, &zdde_search, &where);

	if (zdde == NULL) {
		zdde = umem_zalloc(sizeof (*zdde), UMEM_NOFAIL);
		zdde->zdde_key = zdde_search.zdde_key;
		avl_insert(t, zdde, where);
	}

	zdde->zdde_ref_blocks += 1;
	zdde->zdde_ref_lsize += BP_GET_LSIZE(bp);
	zdde->zdde_ref_psize += BP_GET_PSIZE(bp);
	zdde->zdde_ref_dsize += bp_get_dsize_sync(spa, bp);

	return (0);
}

static void
dump_simulated_ddt(spa_t *spa)
{
	avl_tree_t t;
	void *cookie = NULL;
	zdb_ddt_entry_t *zdde;
	ddt_histogram_t ddh_total;
	ddt_stat_t dds_total;

	bzero(&ddh_total, sizeof (ddt_histogram_t));
	bzero(&dds_total, sizeof (ddt_stat_t));

	avl_create(&t, ddt_entry_compare,
	    sizeof (zdb_ddt_entry_t), offsetof(zdb_ddt_entry_t, zdde_node));

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);

	(void) traverse_pool(spa, 0, TRAVERSE_PRE | TRAVERSE_PREFETCH_METADATA,
	    zdb_ddt_add_cb, &t);

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	while ((zdde = avl_destroy_nodes(&t, &cookie)) != NULL) {
		ddt_stat_t dds;
		uint64_t refcnt = zdde->zdde_ref_blocks;
		ASSERT(refcnt != 0);

		dds.dds_blocks = zdde->zdde_ref_blocks / refcnt;
		dds.dds_lsize = zdde->zdde_ref_lsize / refcnt;
		dds.dds_psize = zdde->zdde_ref_psize / refcnt;
		dds.dds_dsize = zdde->zdde_ref_dsize / refcnt;

		dds.dds_ref_blocks = zdde->zdde_ref_blocks;
		dds.dds_ref_lsize = zdde->zdde_ref_lsize;
		dds.dds_ref_psize = zdde->zdde_ref_psize;
		dds.dds_ref_dsize = zdde->zdde_ref_dsize;

		ddt_stat_add(&ddh_total.ddh_stat[highbit64(refcnt) - 1],
		    &dds, 0);

		umem_free(zdde, sizeof (*zdde));
	}

	avl_destroy(&t);

	ddt_histogram_stat(&dds_total, &ddh_total);

	(void) printf("Simulated DDT histogram:\n");

	zpool_dump_ddt(&dds_total, &ddh_total);

	dump_dedup_ratio(&dds_total);
}

static void
dump_zpool(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	int rc = 0;

	if (dump_opt['S']) {
		dump_simulated_ddt(spa);
		return;
	}

	if (!dump_opt['e'] && dump_opt['C'] > 1) {
		(void) printf("\nCached configuration:\n");
		dump_nvlist(spa->spa_config, 8);
	}

	if (dump_opt['C'])
		dump_config(spa);

	if (dump_opt['u'])
		dump_uberblock(&spa->spa_uberblock, "\nUberblock:\n", "\n");

	if (dump_opt['D'])
		dump_all_ddts(spa);

	if (dump_opt['d'] > 2 || dump_opt['m'])
		dump_metaslabs(spa);
	if (dump_opt['M'])
		dump_metaslab_groups(spa);

	if (dump_opt['d'] || dump_opt['i']) {
		dump_dir(dp->dp_meta_objset);
		if (dump_opt['d'] >= 3) {
			dump_bpobj(&spa->spa_deferred_bpobj,
			    "Deferred frees", 0);
			if (spa_version(spa) >= SPA_VERSION_DEADLISTS) {
				dump_bpobj(&spa->spa_dsl_pool->dp_free_bpobj,
				    "Pool snapshot frees", 0);
			}

			if (spa_feature_is_active(spa,
			    SPA_FEATURE_ASYNC_DESTROY)) {
				dump_bptree(spa->spa_meta_objset,
				    spa->spa_dsl_pool->dp_bptree_obj,
				    "Pool dataset frees");
			}
			dump_dtl(spa->spa_root_vdev, 0);
		}
		(void) dmu_objset_find(spa_name(spa), dump_one_dir,
		    NULL, DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);
	}
	if (dump_opt['b'] || dump_opt['c'])
		rc = dump_block_stats(spa);

	if (rc == 0)
		rc = verify_spacemap_refcounts(spa);

	if (dump_opt['s'])
		show_pool_stats(spa);

	if (dump_opt['h'])
		dump_history(spa);

	if (rc != 0)
		exit(rc);
}

#define	ZDB_FLAG_CHECKSUM	0x0001
#define	ZDB_FLAG_DECOMPRESS	0x0002
#define	ZDB_FLAG_BSWAP		0x0004
#define	ZDB_FLAG_GBH		0x0008
#define	ZDB_FLAG_INDIRECT	0x0010
#define	ZDB_FLAG_PHYS		0x0020
#define	ZDB_FLAG_RAW		0x0040
#define	ZDB_FLAG_PRINT_BLKPTR	0x0080

int flagbits[256];

static void
zdb_print_blkptr(blkptr_t *bp, int flags)
{
	char blkbuf[BP_SPRINTF_LEN];

	if (flags & ZDB_FLAG_BSWAP)
		byteswap_uint64_array((void *)bp, sizeof (blkptr_t));

	snprintf_blkptr(blkbuf, sizeof (blkbuf), bp);
	(void) printf("%s\n", blkbuf);
}

static void
zdb_dump_indirect(blkptr_t *bp, int nbps, int flags)
{
	int i;

	for (i = 0; i < nbps; i++)
		zdb_print_blkptr(&bp[i], flags);
}

static void
zdb_dump_gbh(void *buf, int flags)
{
	zdb_dump_indirect((blkptr_t *)buf, SPA_GBH_NBLKPTRS, flags);
}

static void
zdb_dump_block_raw(void *buf, uint64_t size, int flags)
{
	if (flags & ZDB_FLAG_BSWAP)
		byteswap_uint64_array(buf, size);
	VERIFY(write(fileno(stdout), buf, size) == size);
}

static void
zdb_dump_block(char *label, void *buf, uint64_t size, int flags)
{
	uint64_t *d = (uint64_t *)buf;
	int nwords = size / sizeof (uint64_t);
	int do_bswap = !!(flags & ZDB_FLAG_BSWAP);
	int i, j;
	char *hdr, *c;


	if (do_bswap)
		hdr = " 7 6 5 4 3 2 1 0   f e d c b a 9 8";
	else
		hdr = " 0 1 2 3 4 5 6 7   8 9 a b c d e f";

	(void) printf("\n%s\n%6s   %s  0123456789abcdef\n", label, "", hdr);

	for (i = 0; i < nwords; i += 2) {
		(void) printf("%06llx:  %016llx  %016llx  ",
		    (u_longlong_t)(i * sizeof (uint64_t)),
		    (u_longlong_t)(do_bswap ? BSWAP_64(d[i]) : d[i]),
		    (u_longlong_t)(do_bswap ? BSWAP_64(d[i + 1]) : d[i + 1]));

		c = (char *)&d[i];
		for (j = 0; j < 2 * sizeof (uint64_t); j++)
			(void) printf("%c", isprint(c[j]) ? c[j] : '.');
		(void) printf("\n");
	}
}

/*
 * There are two acceptable formats:
 *	leaf_name	  - For example: c1t0d0 or /tmp/ztest.0a
 *	child[.child]*    - For example: 0.1.1
 *
 * The second form can be used to specify arbitrary vdevs anywhere
 * in the heirarchy.  For example, in a pool with a mirror of
 * RAID-Zs, you can specify either RAID-Z vdev with 0.0 or 0.1 .
 */
static vdev_t *
zdb_vdev_lookup(vdev_t *vdev, char *path)
{
	char *s, *p, *q;
	int i;

	if (vdev == NULL)
		return (NULL);

	/* First, assume the x.x.x.x format */
	i = (int)strtoul(path, &s, 10);
	if (s == path || (s && *s != '.' && *s != '\0'))
		goto name;
	if (i < 0 || i >= vdev->vdev_children)
		return (NULL);

	vdev = vdev->vdev_child[i];
	if (*s == '\0')
		return (vdev);
	return (zdb_vdev_lookup(vdev, s+1));

name:
	for (i = 0; i < vdev->vdev_children; i++) {
		vdev_t *vc = vdev->vdev_child[i];

		if (vc->vdev_path == NULL) {
			vc = zdb_vdev_lookup(vc, path);
			if (vc == NULL)
				continue;
			else
				return (vc);
		}

		p = strrchr(vc->vdev_path, '/');
		p = p ? p + 1 : vc->vdev_path;
		q = &vc->vdev_path[strlen(vc->vdev_path) - 2];

		if (strcmp(vc->vdev_path, path) == 0)
			return (vc);
		if (strcmp(p, path) == 0)
			return (vc);
		if (strcmp(q, "s0") == 0 && strncmp(p, path, q - p) == 0)
			return (vc);
	}

	return (NULL);
}

/*
 * Read a block from a pool and print it out.  The syntax of the
 * block descriptor is:
 *
 *	pool:vdev_specifier:offset:size[:flags]
 *
 *	pool           - The name of the pool you wish to read from
 *	vdev_specifier - Which vdev (see comment for zdb_vdev_lookup)
 *	offset         - offset, in hex, in bytes
 *	size           - Amount of data to read, in hex, in bytes
 *	flags          - A string of characters specifying options
 *		 b: Decode a blkptr at given offset within block
 *		*c: Calculate and display checksums
 *		 d: Decompress data before dumping
 *		 e: Byteswap data before dumping
 *		 g: Display data as a gang block header
 *		 i: Display as an indirect block
 *		 p: Do I/O to physical offset
 *		 r: Dump raw data to stdout
 *
 *              * = not yet implemented
 */
static void
zdb_read_block(char *thing, spa_t *spa)
{
	blkptr_t blk, *bp = &blk;
	dva_t *dva = bp->blk_dva;
	int flags = 0;
	uint64_t offset = 0, size = 0, psize = 0, lsize = 0, blkptr_offset = 0;
	zio_t *zio;
	vdev_t *vd;
	void *pbuf, *lbuf, *buf;
	char *s, *p, *dup, *vdev, *flagstr;
	int i, error;

	dup = strdup(thing);
	s = strtok(dup, ":");
	vdev = s ? s : "";
	s = strtok(NULL, ":");
	offset = strtoull(s ? s : "", NULL, 16);
	s = strtok(NULL, ":");
	size = strtoull(s ? s : "", NULL, 16);
	s = strtok(NULL, ":");
	flagstr = s ? s : "";

	s = NULL;
	if (size == 0)
		s = "size must not be zero";
	if (!IS_P2ALIGNED(size, DEV_BSIZE))
		s = "size must be a multiple of sector size";
	if (!IS_P2ALIGNED(offset, DEV_BSIZE))
		s = "offset must be a multiple of sector size";
	if (s) {
		(void) printf("Invalid block specifier: %s  - %s\n", thing, s);
		free(dup);
		return;
	}

	for (s = strtok(flagstr, ":"); s; s = strtok(NULL, ":")) {
		for (i = 0; flagstr[i]; i++) {
			int bit = flagbits[(uchar_t)flagstr[i]];

			if (bit == 0) {
				(void) printf("***Invalid flag: %c\n",
				    flagstr[i]);
				continue;
			}
			flags |= bit;

			/* If it's not something with an argument, keep going */
			if ((bit & (ZDB_FLAG_CHECKSUM |
			    ZDB_FLAG_PRINT_BLKPTR)) == 0)
				continue;

			p = &flagstr[i + 1];
			if (bit == ZDB_FLAG_PRINT_BLKPTR)
				blkptr_offset = strtoull(p, &p, 16);
			if (*p != ':' && *p != '\0') {
				(void) printf("***Invalid flag arg: '%s'\n", s);
				free(dup);
				return;
			}
		}
	}

	vd = zdb_vdev_lookup(spa->spa_root_vdev, vdev);
	if (vd == NULL) {
		(void) printf("***Invalid vdev: %s\n", vdev);
		free(dup);
		return;
	} else {
		if (vd->vdev_path)
			(void) fprintf(stderr, "Found vdev: %s\n",
			    vd->vdev_path);
		else
			(void) fprintf(stderr, "Found vdev type: %s\n",
			    vd->vdev_ops->vdev_op_type);
	}

	psize = size;
	lsize = size;

	pbuf = umem_alloc_aligned(SPA_MAXBLOCKSIZE, 512, UMEM_NOFAIL);
	lbuf = umem_alloc(SPA_MAXBLOCKSIZE, UMEM_NOFAIL);

	BP_ZERO(bp);

	DVA_SET_VDEV(&dva[0], vd->vdev_id);
	DVA_SET_OFFSET(&dva[0], offset);
	DVA_SET_GANG(&dva[0], !!(flags & ZDB_FLAG_GBH));
	DVA_SET_ASIZE(&dva[0], vdev_psize_to_asize(vd, psize));

	BP_SET_BIRTH(bp, TXG_INITIAL, TXG_INITIAL);

	BP_SET_LSIZE(bp, lsize);
	BP_SET_PSIZE(bp, psize);
	BP_SET_COMPRESS(bp, ZIO_COMPRESS_OFF);
	BP_SET_CHECKSUM(bp, ZIO_CHECKSUM_OFF);
	BP_SET_TYPE(bp, DMU_OT_NONE);
	BP_SET_LEVEL(bp, 0);
	BP_SET_DEDUP(bp, 0);
	BP_SET_BYTEORDER(bp, ZFS_HOST_BYTEORDER);

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	zio = zio_root(spa, NULL, NULL, 0);

	if (vd == vd->vdev_top) {
		/*
		 * Treat this as a normal block read.
		 */
		zio_nowait(zio_read(zio, spa, bp, pbuf, psize, NULL, NULL,
		    ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW, NULL));
	} else {
		/*
		 * Treat this as a vdev child I/O.
		 */
		zio_nowait(zio_vdev_child_io(zio, bp, vd, offset, pbuf, psize,
		    ZIO_TYPE_READ, ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE |
		    ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY |
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_RAW, NULL, NULL));
	}

	error = zio_wait(zio);
	spa_config_exit(spa, SCL_STATE, FTAG);

	if (error) {
		(void) printf("Read of %s failed, error: %d\n", thing, error);
		goto out;
	}

	if (flags & ZDB_FLAG_DECOMPRESS) {
		/*
		 * We don't know how the data was compressed, so just try
		 * every decompress function at every inflated blocksize.
		 */
		enum zio_compress c;
		void *pbuf2 = umem_alloc(SPA_MAXBLOCKSIZE, UMEM_NOFAIL);
		void *lbuf2 = umem_alloc(SPA_MAXBLOCKSIZE, UMEM_NOFAIL);

		bcopy(pbuf, pbuf2, psize);

		VERIFY(random_get_pseudo_bytes((uint8_t *)pbuf + psize,
		    SPA_MAXBLOCKSIZE - psize) == 0);

		VERIFY(random_get_pseudo_bytes((uint8_t *)pbuf2 + psize,
		    SPA_MAXBLOCKSIZE - psize) == 0);

		for (lsize = SPA_MAXBLOCKSIZE; lsize > psize;
		    lsize -= SPA_MINBLOCKSIZE) {
			for (c = 0; c < ZIO_COMPRESS_FUNCTIONS; c++) {
				if (zio_decompress_data(c, pbuf, lbuf,
				    psize, lsize) == 0 &&
				    zio_decompress_data(c, pbuf2, lbuf2,
				    psize, lsize) == 0 &&
				    bcmp(lbuf, lbuf2, lsize) == 0)
					break;
			}
			if (c != ZIO_COMPRESS_FUNCTIONS)
				break;
			lsize -= SPA_MINBLOCKSIZE;
		}

		umem_free(pbuf2, SPA_MAXBLOCKSIZE);
		umem_free(lbuf2, SPA_MAXBLOCKSIZE);

		if (lsize <= psize) {
			(void) printf("Decompress of %s failed\n", thing);
			goto out;
		}
		buf = lbuf;
		size = lsize;
	} else {
		buf = pbuf;
		size = psize;
	}

	if (flags & ZDB_FLAG_PRINT_BLKPTR)
		zdb_print_blkptr((blkptr_t *)(void *)
		    ((uintptr_t)buf + (uintptr_t)blkptr_offset), flags);
	else if (flags & ZDB_FLAG_RAW)
		zdb_dump_block_raw(buf, size, flags);
	else if (flags & ZDB_FLAG_INDIRECT)
		zdb_dump_indirect((blkptr_t *)buf, size / sizeof (blkptr_t),
		    flags);
	else if (flags & ZDB_FLAG_GBH)
		zdb_dump_gbh(buf, flags);
	else
		zdb_dump_block(thing, buf, size, flags);

out:
	umem_free(pbuf, SPA_MAXBLOCKSIZE);
	umem_free(lbuf, SPA_MAXBLOCKSIZE);
	free(dup);
}

static boolean_t
pool_match(nvlist_t *cfg, char *tgt)
{
	uint64_t v, guid = strtoull(tgt, NULL, 0);
	char *s;

	if (guid != 0) {
		if (nvlist_lookup_uint64(cfg, ZPOOL_CONFIG_POOL_GUID, &v) == 0)
			return (v == guid);
	} else {
		if (nvlist_lookup_string(cfg, ZPOOL_CONFIG_POOL_NAME, &s) == 0)
			return (strcmp(s, tgt) == 0);
	}
	return (B_FALSE);
}

static char *
find_zpool(char **target, nvlist_t **configp, int dirc, char **dirv)
{
	nvlist_t *pools;
	nvlist_t *match = NULL;
	char *name = NULL;
	char *sepp = NULL;
	char sep = 0;
	int count = 0;
	importargs_t args = { 0 };

	args.paths = dirc;
	args.path = dirv;
	args.can_be_active = B_TRUE;

	if ((sepp = strpbrk(*target, "/@")) != NULL) {
		sep = *sepp;
		*sepp = '\0';
	}

	pools = zpool_search_import(g_zfs, &args);

	if (pools != NULL) {
		nvpair_t *elem = NULL;
		while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {
			verify(nvpair_value_nvlist(elem, configp) == 0);
			if (pool_match(*configp, *target)) {
				count++;
				if (match != NULL) {
					/* print previously found config */
					if (name != NULL) {
						(void) printf("%s\n", name);
						dump_nvlist(match, 8);
						name = NULL;
					}
					(void) printf("%s\n",
					    nvpair_name(elem));
					dump_nvlist(*configp, 8);
				} else {
					match = *configp;
					name = nvpair_name(elem);
				}
			}
		}
	}
	if (count > 1)
		(void) fatal("\tMatched %d pools - use pool GUID "
		    "instead of pool name or \n"
		    "\tpool name part of a dataset name to select pool", count);

	if (sepp)
		*sepp = sep;
	/*
	 * If pool GUID was specified for pool id, replace it with pool name
	 */
	if (name && (strstr(*target, name) != *target)) {
		int sz = 1 + strlen(name) + ((sepp) ? strlen(sepp) : 0);

		*target = umem_alloc(sz, UMEM_NOFAIL);
		(void) snprintf(*target, sz, "%s%s", name, sepp ? sepp : "");
	}

	*configp = name ? match : NULL;

	return (name);
}

int
main(int argc, char **argv)
{
	int i, c;
	struct rlimit rl = { 1024, 1024 };
	spa_t *spa = NULL;
	objset_t *os = NULL;
	int dump_all = 1;
	int verbose = 0;
	int error = 0;
	char **searchdirs = NULL;
	int nsearch = 0;
	char *target;
	nvlist_t *policy = NULL;
	uint64_t max_txg = UINT64_MAX;
	int flags = ZFS_IMPORT_MISSING_LOG;
	int rewind = ZPOOL_NEVER_REWIND;
	char *spa_config_path_env;
	const char *opts = "bcdhilmMI:suCDRSAFLXevp:t:U:P";

	(void) setrlimit(RLIMIT_NOFILE, &rl);
	(void) enable_extended_FILE_stdio(-1, -1);

	dprintf_setup(&argc, argv);

	/*
	 * If there is an environment variable SPA_CONFIG_PATH it overrides
	 * default spa_config_path setting. If -U flag is specified it will
	 * override this environment variable settings once again.
	 */
	spa_config_path_env = getenv("SPA_CONFIG_PATH");
	if (spa_config_path_env != NULL)
		spa_config_path = spa_config_path_env;

	while ((c = getopt(argc, argv, opts)) != -1) {
		switch (c) {
		case 'b':
		case 'c':
		case 'd':
		case 'h':
		case 'i':
		case 'l':
		case 'm':
		case 's':
		case 'u':
		case 'C':
		case 'D':
		case 'M':
		case 'R':
		case 'S':
			dump_opt[c]++;
			dump_all = 0;
			break;
		case 'A':
		case 'F':
		case 'L':
		case 'X':
		case 'e':
		case 'P':
			dump_opt[c]++;
			break;
		case 'V':
			flags = ZFS_IMPORT_VERBATIM;
			break;
		case 'I':
			max_inflight = strtoull(optarg, NULL, 0);
			if (max_inflight == 0) {
				(void) fprintf(stderr, "maximum number "
				    "of inflight I/Os must be greater "
				    "than 0\n");
				usage();
			}
			break;
		case 'p':
			if (searchdirs == NULL) {
				searchdirs = umem_alloc(sizeof (char *),
				    UMEM_NOFAIL);
			} else {
				char **tmp = umem_alloc((nsearch + 1) *
				    sizeof (char *), UMEM_NOFAIL);
				bcopy(searchdirs, tmp, nsearch *
				    sizeof (char *));
				umem_free(searchdirs,
				    nsearch * sizeof (char *));
				searchdirs = tmp;
			}
			searchdirs[nsearch++] = optarg;
			break;
		case 't':
			max_txg = strtoull(optarg, NULL, 0);
			if (max_txg < TXG_INITIAL) {
				(void) fprintf(stderr, "incorrect txg "
				    "specified: %s\n", optarg);
				usage();
			}
			break;
		case 'U':
			spa_config_path = optarg;
			break;
		case 'v':
			verbose++;
			break;
		default:
			usage();
			break;
		}
	}

	if (!dump_opt['e'] && searchdirs != NULL) {
		(void) fprintf(stderr, "-p option requires use of -e\n");
		usage();
	}

	kernel_init(FREAD);
	if ((g_zfs = libzfs_init()) == NULL)
		return (1);

	if (dump_all)
		verbose = MAX(verbose, 1);

	for (c = 0; c < 256; c++) {
		if (dump_all && !strchr("elAFLRSXP", c))
			dump_opt[c] = 1;
		if (dump_opt[c])
			dump_opt[c] += verbose;
	}

	aok = (dump_opt['A'] == 1) || (dump_opt['A'] > 2);
	zfs_recover = (dump_opt['A'] > 1);

	argc -= optind;
	argv += optind;

	if (argc < 2 && dump_opt['R'])
		usage();
	if (argc < 1) {
		if (!dump_opt['e'] && dump_opt['C']) {
			dump_cachefile(spa_config_path);
			return (0);
		}
		usage();
	}

	if (dump_opt['l']) {
		dump_label(argv[0]);
		return (0);
	}

	if (dump_opt['X'] || dump_opt['F'])
		rewind = ZPOOL_DO_REWIND |
		    (dump_opt['X'] ? ZPOOL_EXTREME_REWIND : 0);

	if (nvlist_alloc(&policy, NV_UNIQUE_NAME_TYPE, 0) != 0 ||
	    nvlist_add_uint64(policy, ZPOOL_REWIND_REQUEST_TXG, max_txg) != 0 ||
	    nvlist_add_uint32(policy, ZPOOL_REWIND_REQUEST, rewind) != 0)
		fatal("internal error: %s", strerror(ENOMEM));

	error = 0;
	target = argv[0];

	if (dump_opt['e']) {
		nvlist_t *cfg = NULL;
		char *name = find_zpool(&target, &cfg, nsearch, searchdirs);

		error = ENOENT;
		if (name) {
			if (dump_opt['C'] > 1) {
				(void) printf("\nConfiguration for import:\n");
				dump_nvlist(cfg, 8);
			}
			if (nvlist_add_nvlist(cfg,
			    ZPOOL_REWIND_POLICY, policy) != 0) {
				fatal("can't open '%s': %s",
				    target, strerror(ENOMEM));
			}
			error = spa_import(name, cfg, NULL, flags);
		}
	}

	if (error == 0) {
		if (strpbrk(target, "/@") == NULL || dump_opt['R']) {
			error = spa_open_rewind(target, &spa, FTAG, policy,
			    NULL);
			if (error) {
				/*
				 * If we're missing the log device then
				 * try opening the pool after clearing the
				 * log state.
				 */
				mutex_enter(&spa_namespace_lock);
				if ((spa = spa_lookup(target)) != NULL &&
				    spa->spa_log_state == SPA_LOG_MISSING) {
					spa->spa_log_state = SPA_LOG_CLEAR;
					error = 0;
				}
				mutex_exit(&spa_namespace_lock);

				if (!error) {
					error = spa_open_rewind(target, &spa,
					    FTAG, policy, NULL);
				}
			}
		} else {
			error = dmu_objset_own(target, DMU_OST_ANY,
			    B_TRUE, FTAG, &os);
		}
	}
	nvlist_free(policy);

	if (error)
		fatal("can't open '%s': %s", target, strerror(error));

	argv++;
	argc--;
	if (!dump_opt['R']) {
		if (argc > 0) {
			zopt_objects = argc;
			zopt_object = calloc(zopt_objects, sizeof (uint64_t));
			for (i = 0; i < zopt_objects; i++) {
				errno = 0;
				zopt_object[i] = strtoull(argv[i], NULL, 0);
				if (zopt_object[i] == 0 && errno != 0)
					fatal("bad number %s: %s",
					    argv[i], strerror(errno));
			}
		}
		if (os != NULL) {
			dump_dir(os);
		} else if (zopt_objects > 0 && !dump_opt['m']) {
			dump_dir(spa->spa_meta_objset);
		} else {
			dump_zpool(spa);
		}
	} else {
		flagbits['b'] = ZDB_FLAG_PRINT_BLKPTR;
		flagbits['c'] = ZDB_FLAG_CHECKSUM;
		flagbits['d'] = ZDB_FLAG_DECOMPRESS;
		flagbits['e'] = ZDB_FLAG_BSWAP;
		flagbits['g'] = ZDB_FLAG_GBH;
		flagbits['i'] = ZDB_FLAG_INDIRECT;
		flagbits['p'] = ZDB_FLAG_PHYS;
		flagbits['r'] = ZDB_FLAG_RAW;

		for (i = 0; i < argc; i++)
			zdb_read_block(argv[i], spa);
	}

	(os != NULL) ? dmu_objset_disown(os, FTAG) : spa_close(spa, FTAG);

	fuid_table_destroy();
	sa_loaded = B_FALSE;

	libzfs_fini(g_zfs);
	kernel_fini();

	return (0);
}
