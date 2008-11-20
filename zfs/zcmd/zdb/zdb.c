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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)zdb.c	1.40	08/04/01 SMI"

#include <stdio.h>
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
#undef ZFS_MAXNAMELEN
#undef verify
#include <libzfs.h>

const char cmdname[] = "zdb";
uint8_t dump_opt[256];

typedef void object_viewer_t(objset_t *, uint64_t, void *data, size_t size);

extern void dump_intent_log(zilog_t *);
uint64_t *zopt_object = NULL;
int zopt_objects = 0;
int zdb_advance = ADVANCE_PRE;
zbookmark_t zdb_noread = { 0, 0, ZB_NO_LEVEL, 0 };
libzfs_handle_t *g_zfs;
boolean_t zdb_sig_user_data = B_TRUE;
int zdb_sig_cksumalg = ZIO_CHECKSUM_SHA256;

/*
 * These libumem hooks provide a reasonable set of defaults for the allocator's
 * debugging facilities.
 */
const char *
_umem_debug_init()
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
	    "Usage: %s [-udibcsvL] [-U cachefile_path] [-O order] "
	    "[-B os:obj:level:blkid] [-S user:cksumalg] "
	    "dataset [object...]\n"
	    "       %s -C [pool]\n"
	    "       %s -l dev\n"
	    "       %s -R pool:vdev:offset:size:flags\n"
	    "       %s [-p path_to_vdev_dir]\n"
	    "       %s -e pool | GUID | devid ...\n",
	    cmdname, cmdname, cmdname, cmdname, cmdname, cmdname);

	(void) fprintf(stderr, "	-u uberblock\n");
	(void) fprintf(stderr, "	-d datasets\n");
	(void) fprintf(stderr, "        -C cached pool configuration\n");
	(void) fprintf(stderr, "	-i intent logs\n");
	(void) fprintf(stderr, "	-b block statistics\n");
	(void) fprintf(stderr, "	-c checksum all data blocks\n");
	(void) fprintf(stderr, "	-s report stats on zdb's I/O\n");
	(void) fprintf(stderr, "	-S <user|all>:<cksum_alg|all> -- "
	    "dump blkptr signatures\n");
	(void) fprintf(stderr, "	-v verbose (applies to all others)\n");
	(void) fprintf(stderr, "        -l dump label contents\n");
	(void) fprintf(stderr, "	-L live pool (allows some errors)\n");
	(void) fprintf(stderr, "	-O [!]<pre|post|prune|data|holes> "
	    "visitation order\n");
	(void) fprintf(stderr, "	-U cachefile_path -- use alternate "
	    "cachefile\n");
	(void) fprintf(stderr, "	-B objset:object:level:blkid -- "
	    "simulate bad block\n");
	(void) fprintf(stderr, "        -R read and display block from a "
	    "device\n");
	(void) fprintf(stderr, "        -e Pool is exported/destroyed/"
	    "has altroot\n");
	(void) fprintf(stderr, "	-p <Path to vdev dir> (use with -e)\n");
	(void) fprintf(stderr, "Specify an option more than once (e.g. -bb) "
	    "to make only that option verbose\n");
	(void) fprintf(stderr, "Default is to dump everything non-verbosely\n");
	exit(1);
}

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

static void
dump_nvlist(nvlist_t *list, int indent)
{
	nvpair_t *elem = NULL;

	while ((elem = nvlist_next_nvpair(list, elem)) != NULL) {
		switch (nvpair_type(elem)) {
		case DATA_TYPE_STRING:
			{
				char *value;

				VERIFY(nvpair_value_string(elem, &value) == 0);
				(void) printf("%*s%s='%s'\n", indent, "",
				    nvpair_name(elem), value);
			}
			break;

		case DATA_TYPE_UINT64:
			{
				uint64_t value;

				VERIFY(nvpair_value_uint64(elem, &value) == 0);
				(void) printf("%*s%s=%llu\n", indent, "",
				    nvpair_name(elem), (u_longlong_t)value);
			}
			break;

		case DATA_TYPE_NVLIST:
			{
				nvlist_t *value;

				VERIFY(nvpair_value_nvlist(elem, &value) == 0);
				(void) printf("%*s%s\n", indent, "",
				    nvpair_name(elem));
				dump_nvlist(value, indent + 4);
			}
			break;

		case DATA_TYPE_NVLIST_ARRAY:
			{
				nvlist_t **value;
				uint_t c, count;

				VERIFY(nvpair_value_nvlist_array(elem, &value,
				    &count) == 0);

				for (c = 0; c < count; c++) {
					(void) printf("%*s%s[%u]\n", indent, "",
					    nvpair_name(elem), c);
					dump_nvlist(value[c], indent + 8);
				}
			}
			break;

		default:

			(void) printf("bad config type %d for %s\n",
			    nvpair_type(elem), nvpair_name(elem));
		}
	}
}

/* ARGSUSED */
static void
dump_packed_nvlist(objset_t *os, uint64_t object, void *data, size_t size)
{
	nvlist_t *nv;
	size_t nvsize = *(uint64_t *)data;
	char *packed = umem_alloc(nvsize, UMEM_NOFAIL);

	VERIFY(0 == dmu_read(os, object, 0, nvsize, packed));

	VERIFY(nvlist_unpack(packed, nvsize, &nv, 0) == 0);

	umem_free(packed, nvsize);

	dump_nvlist(nv, 8);

	nvlist_free(nv);
}

const char dump_zap_stars[] = "****************************************";
const int dump_zap_width = sizeof (dump_zap_stars) - 1;

static void
dump_zap_histogram(uint64_t histo[ZAP_HISTOGRAM_SIZE])
{
	int i;
	int minidx = ZAP_HISTOGRAM_SIZE - 1;
	int maxidx = 0;
	uint64_t max = 0;

	for (i = 0; i < ZAP_HISTOGRAM_SIZE; i++) {
		if (histo[i] > max)
			max = histo[i];
		if (histo[i] > 0 && i > maxidx)
			maxidx = i;
		if (histo[i] > 0 && i < minidx)
			minidx = i;
	}

	if (max < dump_zap_width)
		max = dump_zap_width;

	for (i = minidx; i <= maxidx; i++)
		(void) printf("\t\t\t%u: %6llu %s\n", i, (u_longlong_t)histo[i],
		    &dump_zap_stars[(max - histo[i]) * dump_zap_width / max]);
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
	dump_zap_histogram(zs.zs_leafs_with_2n_pointers);

	(void) printf("\t\tBlocks with n*5 entries:\n");
	dump_zap_histogram(zs.zs_blocks_with_n5_entries);

	(void) printf("\t\tBlocks n/10 full:\n");
	dump_zap_histogram(zs.zs_blocks_n_tenths_full);

	(void) printf("\t\tEntries with n chunks:\n");
	dump_zap_histogram(zs.zs_entries_using_n_chunks);

	(void) printf("\t\tBuckets with n entries:\n");
	dump_zap_histogram(zs.zs_buckets_with_n_entries);
}

/*ARGSUSED*/
static void
dump_none(objset_t *os, uint64_t object, void *data, size_t size)
{
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

static void
dump_spacemap(objset_t *os, space_map_obj_t *smo, space_map_t *sm)
{
	uint64_t alloc, offset, entry;
	uint8_t mapshift = sm->sm_shift;
	uint64_t mapstart = sm->sm_start;
	char *ddata[] = { "ALLOC", "FREE", "CONDENSE", "INVALID",
			    "INVALID", "INVALID", "INVALID", "INVALID" };

	if (smo->smo_object == 0)
		return;

	/*
	 * Print out the freelist entries in both encoded and decoded form.
	 */
	alloc = 0;
	for (offset = 0; offset < smo->smo_objsize; offset += sizeof (entry)) {
		VERIFY(0 == dmu_read(os, smo->smo_object, offset,
		    sizeof (entry), &entry));
		if (SM_DEBUG_DECODE(entry)) {
			(void) printf("\t\t[%4llu] %s: txg %llu, pass %llu\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    ddata[SM_DEBUG_ACTION_DECODE(entry)],
			    (u_longlong_t)SM_DEBUG_TXG_DECODE(entry),
			    (u_longlong_t)SM_DEBUG_SYNCPASS_DECODE(entry));
		} else {
			(void) printf("\t\t[%4llu]    %c  range:"
			    " %08llx-%08llx  size: %06llx\n",
			    (u_longlong_t)(offset / sizeof (entry)),
			    SM_TYPE_DECODE(entry) == SM_ALLOC ? 'A' : 'F',
			    (u_longlong_t)((SM_OFFSET_DECODE(entry) <<
			    mapshift) + mapstart),
			    (u_longlong_t)((SM_OFFSET_DECODE(entry) <<
			    mapshift) + mapstart + (SM_RUN_DECODE(entry) <<
			    mapshift)),
			    (u_longlong_t)(SM_RUN_DECODE(entry) << mapshift));
			if (SM_TYPE_DECODE(entry) == SM_ALLOC)
				alloc += SM_RUN_DECODE(entry) << mapshift;
			else
				alloc -= SM_RUN_DECODE(entry) << mapshift;
		}
	}
	if (alloc != smo->smo_alloc) {
		(void) printf("space_map_object alloc (%llu) INCONSISTENT "
		    "with space map summary (%llu)\n",
		    (u_longlong_t)smo->smo_alloc, (u_longlong_t)alloc);
	}
}

static void
dump_metaslab(metaslab_t *msp)
{
	char freebuf[5];
	space_map_obj_t *smo = &msp->ms_smo;
	vdev_t *vd = msp->ms_group->mg_vd;
	spa_t *spa = vd->vdev_spa;

	nicenum(msp->ms_map.sm_size - smo->smo_alloc, freebuf);

	if (dump_opt['d'] <= 5) {
		(void) printf("\t%10llx   %10llu   %5s\n",
		    (u_longlong_t)msp->ms_map.sm_start,
		    (u_longlong_t)smo->smo_object,
		    freebuf);
		return;
	}

	(void) printf(
	    "\tvdev %llu   offset %08llx   spacemap %4llu   free %5s\n",
	    (u_longlong_t)vd->vdev_id, (u_longlong_t)msp->ms_map.sm_start,
	    (u_longlong_t)smo->smo_object, freebuf);

	ASSERT(msp->ms_map.sm_size == (1ULL << vd->vdev_ms_shift));

	dump_spacemap(spa->spa_meta_objset, smo, &msp->ms_map);
}

static void
dump_metaslabs(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd;
	int c, m;

	(void) printf("\nMetaslabs:\n");

	for (c = 0; c < rvd->vdev_children; c++) {
		vd = rvd->vdev_child[c];

		(void) printf("\n    vdev %llu = %s\n\n",
		    (u_longlong_t)vd->vdev_id, vdev_description(vd));

		if (dump_opt['d'] <= 5) {
			(void) printf("\t%10s   %10s   %5s\n",
			    "offset", "spacemap", "free");
			(void) printf("\t%10s   %10s   %5s\n",
			    "------", "--------", "----");
		}
		for (m = 0; m < vd->vdev_ms_count; m++)
			dump_metaslab(vd->vdev_ms[m]);
		(void) printf("\n");
	}
}

static void
dump_dtl(vdev_t *vd, int indent)
{
	avl_tree_t *t = &vd->vdev_dtl_map.sm_root;
	space_seg_t *ss;
	vdev_t *pvd;
	int c;

	if (indent == 0)
		(void) printf("\nDirty time logs:\n\n");

	(void) printf("\t%*s%s\n", indent, "", vdev_description(vd));

	for (ss = avl_first(t); ss; ss = AVL_NEXT(t, ss)) {
		/*
		 * Everything in this DTL must appear in all parent DTL unions.
		 */
		for (pvd = vd; pvd; pvd = pvd->vdev_parent)
			ASSERT(vdev_dtl_contains(&pvd->vdev_dtl_map,
			    ss->ss_start, ss->ss_end - ss->ss_start));
		(void) printf("\t%*soutage [%llu,%llu] length %llu\n",
		    indent, "",
		    (u_longlong_t)ss->ss_start,
		    (u_longlong_t)ss->ss_end - 1,
		    (u_longlong_t)(ss->ss_end - ss->ss_start));
	}

	(void) printf("\n");

	if (dump_opt['d'] > 5 && vd->vdev_children == 0) {
		dump_spacemap(vd->vdev_spa->spa_meta_objset, &vd->vdev_dtl,
		    &vd->vdev_dtl_map);
		(void) printf("\n");
	}

	for (c = 0; c < vd->vdev_children; c++)
		dump_dtl(vd->vdev_child[c], indent + 4);
}

/*ARGSUSED*/
static void
dump_dnode(objset_t *os, uint64_t object, void *data, size_t size)
{
}

static uint64_t
blkid2offset(dnode_phys_t *dnp, int level, uint64_t blkid)
{
	if (level < 0)
		return (blkid);

	return ((blkid << (level * (dnp->dn_indblkshift - SPA_BLKPTRSHIFT))) *
	    dnp->dn_datablkszsec << SPA_MINBLOCKSHIFT);
}

static void
sprintf_blkptr_compact(char *blkbuf, blkptr_t *bp, int alldvas)
{
	dva_t *dva = bp->blk_dva;
	int ndvas = alldvas ? BP_GET_NDVAS(bp) : 1;
	int i;

	blkbuf[0] = '\0';

	for (i = 0; i < ndvas; i++)
		(void) sprintf(blkbuf + strlen(blkbuf), "%llu:%llx:%llx ",
		    (u_longlong_t)DVA_GET_VDEV(&dva[i]),
		    (u_longlong_t)DVA_GET_OFFSET(&dva[i]),
		    (u_longlong_t)DVA_GET_ASIZE(&dva[i]));

	(void) sprintf(blkbuf + strlen(blkbuf), "%llxL/%llxP F=%llu B=%llu",
	    (u_longlong_t)BP_GET_LSIZE(bp),
	    (u_longlong_t)BP_GET_PSIZE(bp),
	    (u_longlong_t)bp->blk_fill,
	    (u_longlong_t)bp->blk_birth);
}

/* ARGSUSED */
static int
zdb_indirect_cb(traverse_blk_cache_t *bc, spa_t *spa, void *a)
{
	zbookmark_t *zb = &bc->bc_bookmark;
	blkptr_t *bp = &bc->bc_blkptr;
	void *data = bc->bc_data;
	dnode_phys_t *dnp = bc->bc_dnode;
	char blkbuf[BP_SPRINTF_LEN + 80];
	int l;

	if (bc->bc_errno) {
		(void) sprintf(blkbuf,
		    "Error %d reading <%llu, %llu, %lld, %llu>: ",
		    bc->bc_errno,
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)zb->zb_object,
		    (u_longlong_t)zb->zb_level,
		    (u_longlong_t)zb->zb_blkid);
		goto out;
	}

	if (zb->zb_level == -1) {
		ASSERT3U(BP_GET_TYPE(bp), ==, DMU_OT_OBJSET);
		ASSERT3U(BP_GET_LEVEL(bp), ==, 0);
	} else {
		ASSERT3U(BP_GET_TYPE(bp), ==, dnp->dn_type);
		ASSERT3U(BP_GET_LEVEL(bp), ==, zb->zb_level);
	}

	if (zb->zb_level > 0) {
		uint64_t fill = 0;
		blkptr_t *bpx, *bpend;

		for (bpx = data, bpend = bpx + BP_GET_LSIZE(bp) / sizeof (*bpx);
		    bpx < bpend; bpx++) {
			if (bpx->blk_birth != 0) {
				fill += bpx->blk_fill;
			} else {
				ASSERT(bpx->blk_fill == 0);
			}
		}
		ASSERT3U(fill, ==, bp->blk_fill);
	}

	if (zb->zb_level == 0 && dnp->dn_type == DMU_OT_DNODE) {
		uint64_t fill = 0;
		dnode_phys_t *dnx, *dnend;

		for (dnx = data, dnend = dnx + (BP_GET_LSIZE(bp)>>DNODE_SHIFT);
		    dnx < dnend; dnx++) {
			if (dnx->dn_type != DMU_OT_NONE)
				fill++;
		}
		ASSERT3U(fill, ==, bp->blk_fill);
	}

	(void) sprintf(blkbuf, "%16llx ",
	    (u_longlong_t)blkid2offset(dnp, zb->zb_level, zb->zb_blkid));

	ASSERT(zb->zb_level >= 0);

	for (l = dnp->dn_nlevels - 1; l >= -1; l--) {
		if (l == zb->zb_level) {
			(void) sprintf(blkbuf + strlen(blkbuf), "L%llx",
			    (u_longlong_t)zb->zb_level);
		} else {
			(void) sprintf(blkbuf + strlen(blkbuf), " ");
		}
	}

out:
	if (bp->blk_birth == 0) {
		(void) sprintf(blkbuf + strlen(blkbuf), "<hole>");
		(void) printf("%s\n", blkbuf);
	} else {
		sprintf_blkptr_compact(blkbuf + strlen(blkbuf), bp,
		    dump_opt['d'] > 5 ? 1 : 0);
		(void) printf("%s\n", blkbuf);
	}

	return (bc->bc_errno ? ERESTART : 0);
}

/*ARGSUSED*/
static void
dump_indirect(objset_t *os, uint64_t object, void *data, size_t size)
{
	traverse_handle_t *th;
	uint64_t objset = dmu_objset_id(os);
	int advance = zdb_advance;

	(void) printf("Indirect blocks:\n");

	if (object == 0)
		advance |= ADVANCE_DATA;

	th = traverse_init(dmu_objset_spa(os), zdb_indirect_cb, NULL, advance,
	    ZIO_FLAG_CANFAIL);
	th->th_noread = zdb_noread;

	traverse_add_dnode(th, 0, -1ULL, objset, object);

	while (traverse_more(th) == EAGAIN)
		continue;

	(void) printf("\n");

	traverse_fini(th);
}

/*ARGSUSED*/
static void
dump_dsl_dir(objset_t *os, uint64_t object, void *data, size_t size)
{
	dsl_dir_phys_t *dd = data;
	time_t crtime;
	char used[6], compressed[6], uncompressed[6], quota[6], resv[6];

	if (dd == NULL)
		return;

	ASSERT3U(size, >=, sizeof (dsl_dir_phys_t));

	crtime = dd->dd_creation_time;
	nicenum(dd->dd_used_bytes, used);
	nicenum(dd->dd_compressed_bytes, compressed);
	nicenum(dd->dd_uncompressed_bytes, uncompressed);
	nicenum(dd->dd_quota, quota);
	nicenum(dd->dd_reserved, resv);

	(void) printf("\t\tcreation_time = %s", ctime(&crtime));
	(void) printf("\t\thead_dataset_obj = %llu\n",
	    (u_longlong_t)dd->dd_head_dataset_obj);
	(void) printf("\t\tparent_dir_obj = %llu\n",
	    (u_longlong_t)dd->dd_parent_obj);
	(void) printf("\t\torigin_obj = %llu\n",
	    (u_longlong_t)dd->dd_origin_obj);
	(void) printf("\t\tchild_dir_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_child_dir_zapobj);
	(void) printf("\t\tused_bytes = %s\n", used);
	(void) printf("\t\tcompressed_bytes = %s\n", compressed);
	(void) printf("\t\tuncompressed_bytes = %s\n", uncompressed);
	(void) printf("\t\tquota = %s\n", quota);
	(void) printf("\t\treserved = %s\n", resv);
	(void) printf("\t\tprops_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_props_zapobj);
	(void) printf("\t\tdeleg_zapobj = %llu\n",
	    (u_longlong_t)dd->dd_deleg_zapobj);
}

/*ARGSUSED*/
static void
dump_dsl_dataset(objset_t *os, uint64_t object, void *data, size_t size)
{
	dsl_dataset_phys_t *ds = data;
	time_t crtime;
	char used[6], compressed[6], uncompressed[6], unique[6];
	char blkbuf[BP_SPRINTF_LEN];

	if (ds == NULL)
		return;

	ASSERT(size == sizeof (*ds));
	crtime = ds->ds_creation_time;
	nicenum(ds->ds_used_bytes, used);
	nicenum(ds->ds_compressed_bytes, compressed);
	nicenum(ds->ds_uncompressed_bytes, uncompressed);
	nicenum(ds->ds_unique_bytes, unique);
	sprintf_blkptr(blkbuf, BP_SPRINTF_LEN, &ds->ds_bp);

	(void) printf("\t\tdataset_obj = %llu\n",
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
	(void) printf("\t\tbp = %s\n", blkbuf);
}

static void
dump_bplist(objset_t *mos, uint64_t object, char *name)
{
	bplist_t bpl = { 0 };
	blkptr_t blk, *bp = &blk;
	uint64_t itor = 0;
	char bytes[6];
	char comp[6];
	char uncomp[6];

	if (dump_opt['d'] < 3)
		return;

	mutex_init(&bpl.bpl_lock, NULL, MUTEX_DEFAULT, NULL);
	VERIFY(0 == bplist_open(&bpl, mos, object));
	if (bplist_empty(&bpl)) {
		bplist_close(&bpl);
		mutex_destroy(&bpl.bpl_lock);
		return;
	}

	nicenum(bpl.bpl_phys->bpl_bytes, bytes);
	if (bpl.bpl_dbuf->db_size == sizeof (bplist_phys_t)) {
		nicenum(bpl.bpl_phys->bpl_comp, comp);
		nicenum(bpl.bpl_phys->bpl_uncomp, uncomp);
		(void) printf("\n    %s: %llu entries, %s (%s/%s comp)\n",
		    name, (u_longlong_t)bpl.bpl_phys->bpl_entries,
		    bytes, comp, uncomp);
	} else {
		(void) printf("\n    %s: %llu entries, %s\n",
		    name, (u_longlong_t)bpl.bpl_phys->bpl_entries, bytes);
	}

	if (dump_opt['d'] < 5) {
		bplist_close(&bpl);
		mutex_destroy(&bpl.bpl_lock);
		return;
	}

	(void) printf("\n");

	while (bplist_iterate(&bpl, &itor, bp) == 0) {
		char blkbuf[BP_SPRINTF_LEN];

		ASSERT(bp->blk_birth != 0);
		sprintf_blkptr_compact(blkbuf, bp, dump_opt['d'] > 5 ? 1 : 0);
		(void) printf("\tItem %3llu: %s\n",
		    (u_longlong_t)itor - 1, blkbuf);
	}

	bplist_close(&bpl);
	mutex_destroy(&bpl.bpl_lock);
}

static avl_tree_t idx_tree;
static avl_tree_t domain_tree;
static boolean_t fuid_table_loaded;

static void
fuid_table_destroy()
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
 * the doman-rid string.
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
dump_uidgid(objset_t *os, znode_phys_t *zp)
{
	uint32_t uid_idx, gid_idx;

	uid_idx = FUID_INDEX(zp->zp_uid);
	gid_idx = FUID_INDEX(zp->zp_gid);

	/* Load domain table, if not already loaded */
	if (!fuid_table_loaded && (uid_idx || gid_idx)) {
		uint64_t fuid_obj;

		/* first find the fuid object.  It lives in the master node */
		VERIFY(zap_lookup(os, MASTER_NODE_OBJ, ZFS_FUID_TABLES,
		    8, 1, &fuid_obj) == 0);
		(void) zfs_fuid_table_load(os, fuid_obj,
		    &idx_tree, &domain_tree);
		fuid_table_loaded = B_TRUE;
	}

	print_idstr(zp->zp_uid, "uid");
	print_idstr(zp->zp_gid, "gid");
}

/*ARGSUSED*/
static void
dump_znode(objset_t *os, uint64_t object, void *data, size_t size)
{
	znode_phys_t *zp = data;
	time_t z_crtime, z_atime, z_mtime, z_ctime;
	char path[MAXPATHLEN * 2];	/* allow for xattr and failure prefix */
	int error;

	ASSERT(size >= sizeof (znode_phys_t));

	error = zfs_obj_to_path(os, object, path, sizeof (path));
	if (error != 0) {
		(void) snprintf(path, sizeof (path), "\?\?\?<object#%llu>",
		    (u_longlong_t)object);
	}

	if (dump_opt['d'] < 3) {
		(void) printf("\t%s\n", path);
		return;
	}

	z_crtime = (time_t)zp->zp_crtime[0];
	z_atime = (time_t)zp->zp_atime[0];
	z_mtime = (time_t)zp->zp_mtime[0];
	z_ctime = (time_t)zp->zp_ctime[0];

	(void) printf("\tpath	%s\n", path);
	dump_uidgid(os, zp);
	(void) printf("\tatime	%s", ctime(&z_atime));
	(void) printf("\tmtime	%s", ctime(&z_mtime));
	(void) printf("\tctime	%s", ctime(&z_ctime));
	(void) printf("\tcrtime	%s", ctime(&z_crtime));
	(void) printf("\tgen	%llu\n", (u_longlong_t)zp->zp_gen);
	(void) printf("\tmode	%llo\n", (u_longlong_t)zp->zp_mode);
	(void) printf("\tsize	%llu\n", (u_longlong_t)zp->zp_size);
	(void) printf("\tparent	%llu\n", (u_longlong_t)zp->zp_parent);
	(void) printf("\tlinks	%llu\n", (u_longlong_t)zp->zp_links);
	(void) printf("\txattr	%llu\n", (u_longlong_t)zp->zp_xattr);
	(void) printf("\trdev	0x%016llx\n", (u_longlong_t)zp->zp_rdev);
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

static object_viewer_t *object_viewer[DMU_OT_NUMTYPES] = {
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
	dump_uint64,		/* SPA history offsets		*/
	dump_zap,		/* Pool properties		*/
	dump_zap,		/* DSL permissions		*/
	dump_acl,		/* ZFS ACL			*/
	dump_uint8,		/* ZFS SYSACL			*/
	dump_none,		/* FUID nvlist			*/
	dump_packed_nvlist,	/* FUID nvlist size		*/
};

static void
dump_object(objset_t *os, uint64_t object, int verbosity, int *print_header)
{
	dmu_buf_t *db = NULL;
	dmu_object_info_t doi;
	dnode_t *dn;
	void *bonus = NULL;
	size_t bsize = 0;
	char iblk[6], dblk[6], lsize[6], asize[6], bonus_size[6], segsize[6];
	char aux[50];
	int error;

	if (*print_header) {
		(void) printf("\n    Object  lvl   iblk   dblk  lsize"
		    "  asize  type\n");
		*print_header = 0;
	}

	if (object == 0) {
		dn = os->os->os_meta_dnode;
	} else {
		error = dmu_bonus_hold(os, object, FTAG, &db);
		if (error)
			fatal("dmu_bonus_hold(%llu) failed, errno %u",
			    object, error);
		bonus = db->db_data;
		bsize = db->db_size;
		dn = ((dmu_buf_impl_t *)db)->db_dnode;
	}
	dmu_object_info_from_dnode(dn, &doi);

	nicenum(doi.doi_metadata_block_size, iblk);
	nicenum(doi.doi_data_block_size, dblk);
	nicenum(doi.doi_data_block_size * (doi.doi_max_block_offset + 1),
	    lsize);
	nicenum(doi.doi_physical_blks << 9, asize);
	nicenum(doi.doi_bonus_size, bonus_size);

	aux[0] = '\0';

	if (doi.doi_checksum != ZIO_CHECKSUM_INHERIT || verbosity >= 6) {
		(void) snprintf(aux + strlen(aux), sizeof (aux), " (K=%s)",
		    zio_checksum_table[doi.doi_checksum].ci_name);
	}

	if (doi.doi_compress != ZIO_COMPRESS_INHERIT || verbosity >= 6) {
		(void) snprintf(aux + strlen(aux), sizeof (aux), " (Z=%s)",
		    zio_compress_table[doi.doi_compress].ci_name);
	}

	(void) printf("%10lld  %3u  %5s  %5s  %5s  %5s  %s%s\n",
	    (u_longlong_t)object, doi.doi_indirection, iblk, dblk, lsize,
	    asize, dmu_ot[doi.doi_type].ot_name, aux);

	if (doi.doi_bonus_type != DMU_OT_NONE && verbosity > 3) {
		(void) printf("%10s  %3s  %5s  %5s  %5s  %5s  %s\n",
		    "", "", "", "", bonus_size, "bonus",
		    dmu_ot[doi.doi_bonus_type].ot_name);
	}

	if (verbosity >= 4) {
		object_viewer[doi.doi_bonus_type](os, object, bonus, bsize);
		object_viewer[doi.doi_type](os, object, NULL, 0);
		*print_header = 1;
	}

	if (verbosity >= 5)
		dump_indirect(os, object, NULL, 0);

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
			error = dnode_next_offset(dn, B_FALSE, &start, minlvl,
			    blkfill, 0);
			if (error)
				break;
			end = start;
			error = dnode_next_offset(dn, B_TRUE, &end, minlvl,
			    blkfill, 0);
			nicenum(end - start, segsize);
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
	char numbuf[8];
	char blkbuf[BP_SPRINTF_LEN];
	char osname[MAXNAMELEN];
	char *type = "UNKNOWN";
	int verbosity = dump_opt['d'];
	int print_header = 1;
	int i, error;

	dmu_objset_fast_stat(os, &dds);

	if (dds.dds_type < DMU_OST_NUMTYPES)
		type = objset_types[dds.dds_type];

	if (dds.dds_type == DMU_OST_META) {
		dds.dds_creation_txg = TXG_INITIAL;
		usedobjs = os->os->os_rootbp->blk_fill;
		refdbytes =
		    os->os->os_spa->spa_dsl_pool->dp_mos_dir->dd_used_bytes;
	} else {
		dmu_objset_space(os, &refdbytes, &scratch, &usedobjs, &scratch);
	}

	ASSERT3U(usedobjs, ==, os->os->os_rootbp->blk_fill);

	nicenum(refdbytes, numbuf);

	if (verbosity >= 4) {
		(void) strcpy(blkbuf, ", rootbp ");
		sprintf_blkptr(blkbuf + strlen(blkbuf),
		    BP_SPRINTF_LEN - strlen(blkbuf), os->os->os_rootbp);
	} else {
		blkbuf[0] = '\0';
	}

	dmu_objset_name(os, osname);

	(void) printf("Dataset %s [%s], ID %llu, cr_txg %llu, "
	    "%s, %llu objects%s\n",
	    osname, type, (u_longlong_t)dmu_objset_id(os),
	    (u_longlong_t)dds.dds_creation_txg,
	    numbuf, (u_longlong_t)usedobjs, blkbuf);

	dump_intent_log(dmu_objset_zil(os));

	if (dmu_objset_ds(os) != NULL)
		dump_bplist(dmu_objset_pool(os)->dp_meta_objset,
		    dmu_objset_ds(os)->ds_phys->ds_deadlist_obj, "Deadlist");

	if (verbosity < 2)
		return;

	if (zopt_objects != 0) {
		for (i = 0; i < zopt_objects; i++)
			dump_object(os, zopt_object[i], verbosity,
			    &print_header);
		(void) printf("\n");
		return;
	}

	dump_object(os, 0, verbosity, &print_header);
	object_count = 1;

	object = 0;
	while ((error = dmu_object_next(os, &object, B_FALSE, 0)) == 0) {
		dump_object(os, object, verbosity, &print_header);
		object_count++;
	}

	ASSERT3U(object_count, ==, usedobjs);

	(void) printf("\n");

	if (error != ESRCH)
		fatal("dmu_object_next() = %d", error);
}

static void
dump_uberblock(uberblock_t *ub)
{
	time_t timestamp = ub->ub_timestamp;

	(void) printf("Uberblock\n\n");
	(void) printf("\tmagic = %016llx\n", (u_longlong_t)ub->ub_magic);
	(void) printf("\tversion = %llu\n", (u_longlong_t)ub->ub_version);
	(void) printf("\ttxg = %llu\n", (u_longlong_t)ub->ub_txg);
	(void) printf("\tguid_sum = %llu\n", (u_longlong_t)ub->ub_guid_sum);
	(void) printf("\ttimestamp = %llu UTC = %s",
	    (u_longlong_t)ub->ub_timestamp, asctime(localtime(&timestamp)));
	if (dump_opt['u'] >= 3) {
		char blkbuf[BP_SPRINTF_LEN];
		sprintf_blkptr(blkbuf, BP_SPRINTF_LEN, &ub->ub_rootbp);
		(void) printf("\trootbp = %s\n", blkbuf);
	}
	(void) printf("\n");
}

static void
dump_config(const char *pool)
{
	spa_t *spa = NULL;

	mutex_enter(&spa_namespace_lock);
	while ((spa = spa_next(spa)) != NULL) {
		if (pool == NULL)
			(void) printf("%s\n", spa_name(spa));
		if (pool == NULL || strcmp(pool, spa_name(spa)) == 0)
			dump_nvlist(spa->spa_config, 4);
	}
	mutex_exit(&spa_namespace_lock);
}

static void
dump_label(const char *dev)
{
	int fd;
	vdev_label_t label;
	char *buf = label.vl_vdev_phys.vp_nvlist;
	size_t buflen = sizeof (label.vl_vdev_phys.vp_nvlist);
	struct stat64 statbuf;
	uint64_t psize;
	int l;

	if ((fd = open64(dev, O_RDONLY)) < 0) {
		(void) printf("cannot open '%s': %s\n", dev, strerror(errno));
		exit(1);
	}

	if (fstat64(fd, &statbuf) != 0) {
		(void) printf("failed to stat '%s': %s\n", dev,
		    strerror(errno));
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
			continue;
		}
		dump_nvlist(config, 4);
		nvlist_free(config);
	}
}

/*ARGSUSED*/
static int
dump_one_dir(char *dsname, void *arg)
{
	int error;
	objset_t *os;

	error = dmu_objset_open(dsname, DMU_OST_ANY,
	    DS_MODE_STANDARD | DS_MODE_READONLY, &os);
	if (error) {
		(void) printf("Could not open %s\n", dsname);
		return (0);
	}
	dump_dir(os);
	dmu_objset_close(os);
	fuid_table_destroy();
	return (0);
}

static void
zdb_space_map_load(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd;
	int c, m, error;

	for (c = 0; c < rvd->vdev_children; c++) {
		vd = rvd->vdev_child[c];
		for (m = 0; m < vd->vdev_ms_count; m++) {
			metaslab_t *msp = vd->vdev_ms[m];
			mutex_enter(&msp->ms_lock);
			error = space_map_load(&msp->ms_allocmap[0], NULL,
			    SM_ALLOC, &msp->ms_smo, spa->spa_meta_objset);
			mutex_exit(&msp->ms_lock);
			if (error)
				fatal("%s bad space map #%d, error %d",
				    spa->spa_name, c, error);
		}
	}
}

static int
zdb_space_map_claim(spa_t *spa, blkptr_t *bp, zbookmark_t *zb)
{
	dva_t *dva = bp->blk_dva;
	vdev_t *vd;
	metaslab_t *msp;
	space_map_t *allocmap, *freemap;
	int error;
	int d;
	blkptr_t blk = *bp;

	for (d = 0; d < BP_GET_NDVAS(bp); d++) {
		uint64_t vdev = DVA_GET_VDEV(&dva[d]);
		uint64_t offset = DVA_GET_OFFSET(&dva[d]);
		uint64_t size = DVA_GET_ASIZE(&dva[d]);

		if ((vd = vdev_lookup_top(spa, vdev)) == NULL)
			return (ENXIO);

		if ((offset >> vd->vdev_ms_shift) >= vd->vdev_ms_count)
			return (ENXIO);

		msp = vd->vdev_ms[offset >> vd->vdev_ms_shift];
		allocmap = &msp->ms_allocmap[0];
		freemap = &msp->ms_freemap[0];

		/* Prepare our copy of the bp in case we need to read GBHs */
		if (DVA_GET_GANG(&dva[d])) {
			size = vdev_psize_to_asize(vd, SPA_GANGBLOCKSIZE);
			DVA_SET_ASIZE(&blk.blk_dva[d], size);
			DVA_SET_GANG(&blk.blk_dva[d], 0);
		}

		mutex_enter(&msp->ms_lock);
		if (space_map_contains(freemap, offset, size)) {
			mutex_exit(&msp->ms_lock);
			return (EAGAIN);	/* allocated more than once */
		}

		if (!space_map_contains(allocmap, offset, size)) {
			mutex_exit(&msp->ms_lock);
			return (ESTALE);	/* not allocated at all */
		}

		space_map_remove(allocmap, offset, size);
		space_map_add(freemap, offset, size);

		mutex_exit(&msp->ms_lock);
	}

	if (BP_IS_GANG(bp)) {
		zio_gbh_phys_t gbh;
		int g;

		/* LINTED - compile time assert */
		ASSERT(sizeof (zio_gbh_phys_t) == SPA_GANGBLOCKSIZE);

		BP_SET_CHECKSUM(&blk, ZIO_CHECKSUM_GANG_HEADER);
		BP_SET_PSIZE(&blk, SPA_GANGBLOCKSIZE);
		BP_SET_LSIZE(&blk, SPA_GANGBLOCKSIZE);
		BP_SET_COMPRESS(&blk, ZIO_COMPRESS_OFF);
		error = zio_wait(zio_read(NULL, spa, &blk, &gbh,
		    SPA_GANGBLOCKSIZE, NULL, NULL, ZIO_PRIORITY_SYNC_READ,
		    ZIO_FLAG_CANFAIL | ZIO_FLAG_CONFIG_HELD, zb));
		if (error)
			return (error);
		if (BP_SHOULD_BYTESWAP(&blk))
			byteswap_uint64_array(&gbh, SPA_GANGBLOCKSIZE);
		for (g = 0; g < SPA_GBH_NBLKPTRS; g++) {
			if (BP_IS_HOLE(&gbh.zg_blkptr[g]))
				break;
			error = zdb_space_map_claim(spa, &gbh.zg_blkptr[g], zb);
			if (error)
				return (error);
		}
	}

	return (0);
}

static void
zdb_leak(space_map_t *sm, uint64_t start, uint64_t size)
{
	metaslab_t *msp;

	/* LINTED */
	msp = (metaslab_t *)((char *)sm - offsetof(metaslab_t, ms_allocmap[0]));

	(void) printf("leaked space: vdev %llu, offset 0x%llx, size %llu\n",
	    (u_longlong_t)msp->ms_group->mg_vd->vdev_id,
	    (u_longlong_t)start,
	    (u_longlong_t)size);
}

static void
zdb_space_map_unload(spa_t *spa)
{
	vdev_t *rvd = spa->spa_root_vdev;
	vdev_t *vd;
	int c, m;

	for (c = 0; c < rvd->vdev_children; c++) {
		vd = rvd->vdev_child[c];
		for (m = 0; m < vd->vdev_ms_count; m++) {
			metaslab_t *msp = vd->vdev_ms[m];
			mutex_enter(&msp->ms_lock);
			space_map_vacate(&msp->ms_allocmap[0], zdb_leak,
			    &msp->ms_allocmap[0]);
			space_map_unload(&msp->ms_allocmap[0]);
			space_map_vacate(&msp->ms_freemap[0], NULL, NULL);
			mutex_exit(&msp->ms_lock);
		}
	}
}

static void
zdb_refresh_ubsync(spa_t *spa)
{
	uberblock_t ub = { 0 };
	vdev_t *rvd = spa->spa_root_vdev;
	zio_t *zio;

	/*
	 * Reload the uberblock.
	 */
	zio = zio_root(spa, NULL, NULL,
	    ZIO_FLAG_CANFAIL | ZIO_FLAG_SPECULATIVE);
	vdev_uberblock_load(zio, rvd, &ub);
	(void) zio_wait(zio);

	if (ub.ub_txg != 0)
		spa->spa_ubsync = ub;
}

/*
 * Verify that the sum of the sizes of all blocks in the pool adds up
 * to the SPA's sa_alloc total.
 */
typedef struct zdb_blkstats {
	uint64_t	zb_asize;
	uint64_t	zb_lsize;
	uint64_t	zb_psize;
	uint64_t	zb_count;
} zdb_blkstats_t;

#define	DMU_OT_DEFERRED	DMU_OT_NONE
#define	DMU_OT_TOTAL	DMU_OT_NUMTYPES

#define	ZB_TOTAL	ZB_MAXLEVEL

typedef struct zdb_cb {
	zdb_blkstats_t	zcb_type[ZB_TOTAL + 1][DMU_OT_TOTAL + 1];
	uint64_t	zcb_errors[256];
	traverse_blk_cache_t *zcb_cache;
	int		zcb_readfails;
	int		zcb_haderrors;
} zdb_cb_t;

static void
zdb_count_block(spa_t *spa, zdb_cb_t *zcb, blkptr_t *bp, int type)
{
	int i, error;

	for (i = 0; i < 4; i++) {
		int l = (i < 2) ? BP_GET_LEVEL(bp) : ZB_TOTAL;
		int t = (i & 1) ? type : DMU_OT_TOTAL;
		zdb_blkstats_t *zb = &zcb->zcb_type[l][t];

		zb->zb_asize += BP_GET_ASIZE(bp);
		zb->zb_lsize += BP_GET_LSIZE(bp);
		zb->zb_psize += BP_GET_PSIZE(bp);
		zb->zb_count++;
	}

	if (dump_opt['S']) {
		boolean_t print_sig;

		print_sig  = !zdb_sig_user_data || (BP_GET_LEVEL(bp) == 0 &&
		    BP_GET_TYPE(bp) == DMU_OT_PLAIN_FILE_CONTENTS);

		if (BP_GET_CHECKSUM(bp) < zdb_sig_cksumalg)
			print_sig = B_FALSE;

		if (print_sig) {
			(void) printf("%llu\t%lld\t%lld\t%s\t%s\t%s\t"
			    "%llx:%llx:%llx:%llx\n",
			    (u_longlong_t)BP_GET_LEVEL(bp),
			    (longlong_t)BP_GET_PSIZE(bp),
			    (longlong_t)BP_GET_NDVAS(bp),
			    dmu_ot[BP_GET_TYPE(bp)].ot_name,
			    zio_checksum_table[BP_GET_CHECKSUM(bp)].ci_name,
			    zio_compress_table[BP_GET_COMPRESS(bp)].ci_name,
			    (u_longlong_t)bp->blk_cksum.zc_word[0],
			    (u_longlong_t)bp->blk_cksum.zc_word[1],
			    (u_longlong_t)bp->blk_cksum.zc_word[2],
			    (u_longlong_t)bp->blk_cksum.zc_word[3]);
		}
	}

	if (dump_opt['L'])
		return;

	error = zdb_space_map_claim(spa, bp, &zcb->zcb_cache->bc_bookmark);

	if (error == 0)
		return;

	if (error == EAGAIN)
		(void) fatal("double-allocation, bp=%p", bp);

	if (error == ESTALE)
		(void) fatal("reference to freed block, bp=%p", bp);

	(void) fatal("fatal error %d in bp %p", error, bp);
}

static int
zdb_blkptr_cb(traverse_blk_cache_t *bc, spa_t *spa, void *arg)
{
	zbookmark_t *zb = &bc->bc_bookmark;
	zdb_cb_t *zcb = arg;
	blkptr_t *bp = &bc->bc_blkptr;
	dmu_object_type_t type = BP_GET_TYPE(bp);
	char blkbuf[BP_SPRINTF_LEN];
	int error = 0;

	if (bc->bc_errno) {
		if (zcb->zcb_readfails++ < 10 && dump_opt['L']) {
			zdb_refresh_ubsync(spa);
			error = EAGAIN;
		} else {
			zcb->zcb_haderrors = 1;
			zcb->zcb_errors[bc->bc_errno]++;
			error = ERESTART;
		}

		if (dump_opt['b'] >= 3 || (dump_opt['b'] >= 2 && bc->bc_errno))
			sprintf_blkptr(blkbuf, BP_SPRINTF_LEN, bp);
		else
			blkbuf[0] = '\0';

		if (!dump_opt['S']) {
			(void) printf("zdb_blkptr_cb: Got error %d reading "
			    "<%llu, %llu, %lld, %llx> %s -- %s\n",
			    bc->bc_errno,
			    (u_longlong_t)zb->zb_objset,
			    (u_longlong_t)zb->zb_object,
			    (u_longlong_t)zb->zb_level,
			    (u_longlong_t)zb->zb_blkid,
			    blkbuf,
			    error == EAGAIN ? "retrying" : "skipping");
		}

		return (error);
	}

	zcb->zcb_readfails = 0;

	ASSERT(!BP_IS_HOLE(bp));

	if (dump_opt['b'] >= 4) {
		sprintf_blkptr(blkbuf, BP_SPRINTF_LEN, bp);
		(void) printf("objset %llu object %llu offset 0x%llx %s\n",
		    (u_longlong_t)zb->zb_objset,
		    (u_longlong_t)zb->zb_object,
		    (u_longlong_t)blkid2offset(bc->bc_dnode,
		    zb->zb_level, zb->zb_blkid), blkbuf);
	}

	zdb_count_block(spa, zcb, bp, type);

	return (0);
}

static int
dump_block_stats(spa_t *spa)
{
	traverse_handle_t *th;
	zdb_cb_t zcb = { 0 };
	traverse_blk_cache_t dummy_cache = { 0 };
	zdb_blkstats_t *zb, *tzb;
	uint64_t alloc, space, logalloc;
	vdev_t *rvd = spa->spa_root_vdev;
	int leaks = 0;
	int advance = zdb_advance;
	int c, e, flags;

	zcb.zcb_cache = &dummy_cache;

	if (dump_opt['c'] || dump_opt['S'])
		advance |= ADVANCE_DATA;

	advance |= ADVANCE_PRUNE | ADVANCE_ZIL;

	if (!dump_opt['S']) {
		(void) printf("\nTraversing all blocks to %sverify"
		    " nothing leaked ...\n",
		    dump_opt['c'] ? "verify checksums and " : "");
	}

	/*
	 * Load all space maps.  As we traverse the pool, if we find a block
	 * that's not in its space map, that indicates a double-allocation,
	 * reference to a freed block, or an unclaimed block.  Otherwise we
	 * remove the block from the space map.  If the space maps are not
	 * empty when we're done, that indicates leaked blocks.
	 */
	if (!dump_opt['L'])
		zdb_space_map_load(spa);

	/*
	 * If there's a deferred-free bplist, process that first.
	 */
	if (spa->spa_sync_bplist_obj != 0) {
		bplist_t *bpl = &spa->spa_sync_bplist;
		blkptr_t blk;
		uint64_t itor = 0;

		VERIFY(0 == bplist_open(bpl, spa->spa_meta_objset,
		    spa->spa_sync_bplist_obj));

		while (bplist_iterate(bpl, &itor, &blk) == 0) {
			if (dump_opt['b'] >= 4) {
				char blkbuf[BP_SPRINTF_LEN];
				sprintf_blkptr(blkbuf, BP_SPRINTF_LEN, &blk);
				(void) printf("[%s] %s\n",
				    "deferred free", blkbuf);
			}
			zdb_count_block(spa, &zcb, &blk, DMU_OT_DEFERRED);
		}

		bplist_close(bpl);
	}

	/*
	 * Now traverse the pool.  If we're reading all data to verify
	 * checksums, do a scrubbing read so that we validate all copies.
	 */
	flags = ZIO_FLAG_CANFAIL;
	if (advance & ADVANCE_DATA)
		flags |= ZIO_FLAG_SCRUB;
	th = traverse_init(spa, zdb_blkptr_cb, &zcb, advance, flags);
	th->th_noread = zdb_noread;

	traverse_add_pool(th, 0, spa_first_txg(spa) + TXG_CONCURRENT_STATES);

	while (traverse_more(th) == EAGAIN)
		continue;

	traverse_fini(th);

	if (zcb.zcb_haderrors && !dump_opt['S']) {
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
	if (!dump_opt['L'])
		zdb_space_map_unload(spa);

	/*
	 * If we're interested in printing out the blkptr signatures,
	 * return now as we don't print out anything else (including
	 * errors and leaks).
	 */
	if (dump_opt['S'])
		return (zcb.zcb_haderrors ? 3 : 0);

	if (dump_opt['L'])
		(void) printf("\n\n *** Live pool traversal; "
		    "block counts are only approximate ***\n\n");

	alloc = spa_get_alloc(spa);
	space = spa_get_space(spa);

	/*
	 * Log blocks allocated from a separate log device don't count
	 * as part of the normal pool space; factor them in here.
	 */
	logalloc = 0;

	for (c = 0; c < rvd->vdev_children; c++)
		if (rvd->vdev_child[c]->vdev_islog)
			logalloc += rvd->vdev_child[c]->vdev_stat.vs_alloc;

	tzb = &zcb.zcb_type[ZB_TOTAL][DMU_OT_TOTAL];

	if (tzb->zb_asize == alloc + logalloc) {
		(void) printf("\n\tNo leaks (block sum matches space"
		    " maps exactly)\n");
	} else {
		(void) printf("block traversal size %llu != alloc %llu "
		    "(leaked %lld)\n",
		    (u_longlong_t)tzb->zb_asize,
		    (u_longlong_t)alloc + logalloc,
		    (u_longlong_t)(alloc + logalloc - tzb->zb_asize));
		leaks = 1;
	}

	if (tzb->zb_count == 0)
		return (2);

	(void) printf("\n");
	(void) printf("\tbp count:      %10llu\n",
	    (u_longlong_t)tzb->zb_count);
	(void) printf("\tbp logical:    %10llu\t avg: %6llu\n",
	    (u_longlong_t)tzb->zb_lsize,
	    (u_longlong_t)(tzb->zb_lsize / tzb->zb_count));
	(void) printf("\tbp physical:   %10llu\t avg:"
	    " %6llu\tcompression: %6.2f\n",
	    (u_longlong_t)tzb->zb_psize,
	    (u_longlong_t)(tzb->zb_psize / tzb->zb_count),
	    (double)tzb->zb_lsize / tzb->zb_psize);
	(void) printf("\tbp allocated:  %10llu\t avg:"
	    " %6llu\tcompression: %6.2f\n",
	    (u_longlong_t)tzb->zb_asize,
	    (u_longlong_t)(tzb->zb_asize / tzb->zb_count),
	    (double)tzb->zb_lsize / tzb->zb_asize);
	(void) printf("\tSPA allocated: %10llu\tused: %5.2f%%\n",
	    (u_longlong_t)alloc, 100.0 * alloc / space);

	if (dump_opt['b'] >= 2) {
		int l, t, level;
		(void) printf("\nBlocks\tLSIZE\tPSIZE\tASIZE"
		    "\t  avg\t comp\t%%Total\tType\n");

		for (t = 0; t <= DMU_OT_NUMTYPES; t++) {
			char csize[6], lsize[6], psize[6], asize[6], avg[6];
			char *typename;

			typename = t == DMU_OT_DEFERRED ? "deferred free" :
			    t == DMU_OT_TOTAL ? "Total" : dmu_ot[t].ot_name;

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

				nicenum(zb->zb_count, csize);
				nicenum(zb->zb_lsize, lsize);
				nicenum(zb->zb_psize, psize);
				nicenum(zb->zb_asize, asize);
				nicenum(zb->zb_asize / zb->zb_count, avg);

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

static void
dump_zpool(spa_t *spa)
{
	dsl_pool_t *dp = spa_get_dsl(spa);
	int rc = 0;

	spa_config_enter(spa, RW_READER, FTAG);

	if (dump_opt['u'])
		dump_uberblock(&spa->spa_uberblock);

	if (dump_opt['d'] || dump_opt['i']) {
		dump_dir(dp->dp_meta_objset);
		if (dump_opt['d'] >= 3) {
			dump_bplist(dp->dp_meta_objset,
			    spa->spa_sync_bplist_obj, "Deferred frees");
			dump_dtl(spa->spa_root_vdev, 0);
			dump_metaslabs(spa);
		}
		(void) dmu_objset_find(spa->spa_name, dump_one_dir, NULL,
		    DS_FIND_SNAPSHOTS | DS_FIND_CHILDREN);
	}

	if (dump_opt['b'] || dump_opt['c'] || dump_opt['S'])
		rc = dump_block_stats(spa);

	if (dump_opt['s'])
		show_pool_stats(spa);

	spa_config_exit(spa, FTAG);

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
	dva_t *dva = bp->blk_dva;
	int d;

	if (flags & ZDB_FLAG_BSWAP)
		byteswap_uint64_array((void *)bp, sizeof (blkptr_t));
	/*
	 * Super-ick warning:  This code is also duplicated in
	 * cmd/mdb/common/modules/zfs/zfs.c .  Yeah, I hate code
	 * replication, too.
	 */
	for (d = 0; d < BP_GET_NDVAS(bp); d++) {
		(void) printf("\tDVA[%d]: vdev_id %lld / %llx\n", d,
		    (longlong_t)DVA_GET_VDEV(&dva[d]),
		    (longlong_t)DVA_GET_OFFSET(&dva[d]));
		(void) printf("\tDVA[%d]:       GANG: %-5s  GRID:  %04llx\t"
		    "ASIZE: %llx\n", d,
		    DVA_GET_GANG(&dva[d]) ? "TRUE" : "FALSE",
		    (longlong_t)DVA_GET_GRID(&dva[d]),
		    (longlong_t)DVA_GET_ASIZE(&dva[d]));
		(void) printf("\tDVA[%d]: :%llu:%llx:%llx:%s%s%s%s\n", d,
		    (u_longlong_t)DVA_GET_VDEV(&dva[d]),
		    (longlong_t)DVA_GET_OFFSET(&dva[d]),
		    (longlong_t)BP_GET_PSIZE(bp),
		    BP_SHOULD_BYTESWAP(bp) ? "e" : "",
		    !DVA_GET_GANG(&dva[d]) && BP_GET_LEVEL(bp) != 0 ?
		    "d" : "",
		    DVA_GET_GANG(&dva[d]) ? "g" : "",
		    BP_GET_COMPRESS(bp) != 0 ? "d" : "");
	}
	(void) printf("\tLSIZE:  %-16llx\t\tPSIZE: %llx\n",
	    (longlong_t)BP_GET_LSIZE(bp), (longlong_t)BP_GET_PSIZE(bp));
	(void) printf("\tENDIAN: %6s\t\t\t\t\tTYPE:  %s\n",
	    BP_GET_BYTEORDER(bp) ? "LITTLE" : "BIG",
	    dmu_ot[BP_GET_TYPE(bp)].ot_name);
	(void) printf("\tBIRTH:  %-16llx   LEVEL: %-2llu\tFILL:  %llx\n",
	    (u_longlong_t)bp->blk_birth, (u_longlong_t)BP_GET_LEVEL(bp),
	    (u_longlong_t)bp->blk_fill);
	(void) printf("\tCKFUNC: %-16s\t\tCOMP:  %s\n",
	    zio_checksum_table[BP_GET_CHECKSUM(bp)].ci_name,
	    zio_compress_table[BP_GET_COMPRESS(bp)].ci_name);
	(void) printf("\tCKSUM:  %llx:%llx:%llx:%llx\n",
	    (u_longlong_t)bp->blk_cksum.zc_word[0],
	    (u_longlong_t)bp->blk_cksum.zc_word[1],
	    (u_longlong_t)bp->blk_cksum.zc_word[2],
	    (u_longlong_t)bp->blk_cksum.zc_word[3]);
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
	(void) write(2, buf, size);
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
 *		*d: Decompress data before dumping
 *		 e: Byteswap data before dumping
 *		*g: Display data as a gang block header
 *		*i: Display as an indirect block
 *		 p: Do I/O to physical offset
 *		 r: Dump raw data to stdout
 *
 *              * = not yet implemented
 */
static void
zdb_read_block(char *thing, spa_t **spap)
{
	spa_t *spa = *spap;
	int flags = 0;
	uint64_t offset = 0, size = 0, blkptr_offset = 0;
	zio_t *zio;
	vdev_t *vd;
	void *buf;
	char *s, *p, *dup, *spa_name, *vdev, *flagstr;
	int i, error, zio_flags;

	dup = strdup(thing);
	s = strtok(dup, ":");
	spa_name = s ? s : "";
	s = strtok(NULL, ":");
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
			if ((bit & (ZDB_FLAG_CHECKSUM | ZDB_FLAG_DECOMPRESS |
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

	if (spa == NULL || spa->spa_name == NULL ||
	    strcmp(spa->spa_name, spa_name)) {
		if (spa && spa->spa_name)
			spa_close(spa, (void *)zdb_read_block);
		error = spa_open(spa_name, spap, (void *)zdb_read_block);
		if (error)
			fatal("Failed to open pool '%s': %s",
			    spa_name, strerror(error));
		spa = *spap;
	}

	vd = zdb_vdev_lookup(spa->spa_root_vdev, vdev);
	if (vd == NULL) {
		(void) printf("***Invalid vdev: %s\n", vdev);
		free(dup);
		return;
	} else {
		if (vd->vdev_path)
			(void) printf("Found vdev: %s\n", vd->vdev_path);
		else
			(void) printf("Found vdev type: %s\n",
			    vd->vdev_ops->vdev_op_type);
	}

	buf = umem_alloc(size, UMEM_NOFAIL);

	zio_flags = ZIO_FLAG_DONT_CACHE | ZIO_FLAG_DONT_QUEUE |
	    ZIO_FLAG_DONT_PROPAGATE | ZIO_FLAG_DONT_RETRY | ZIO_FLAG_NOBOOKMARK;

	if (flags & ZDB_FLAG_PHYS)
		zio_flags |= ZIO_FLAG_PHYSICAL;

	zio = zio_root(spa, NULL, NULL, 0);
	/* XXX todo - cons up a BP so RAID-Z will be happy */
	zio_nowait(zio_vdev_child_io(zio, NULL, vd, offset, buf, size,
	    ZIO_TYPE_READ, ZIO_PRIORITY_SYNC_READ, zio_flags, NULL, NULL));
	error = zio_wait(zio);

	if (error) {
		(void) printf("Read of %s failed, error: %d\n", thing, error);
		goto out;
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
	umem_free(buf, size);
	free(dup);
}

static boolean_t
nvlist_string_match(nvlist_t *config, char *name, char *tgt)
{
	char *s;

	verify(nvlist_lookup_string(config, name, &s) == 0);
	return (strcmp(s, tgt) == 0);
}

static boolean_t
nvlist_uint64_match(nvlist_t *config, char *name, uint64_t tgt)
{
	uint64_t val;

	verify(nvlist_lookup_uint64(config, name, &val) == 0);
	return (val == tgt);
}

static boolean_t
vdev_child_guid_match(nvlist_t *vdev, uint64_t guid)
{
	nvlist_t **child;
	uint_t c, children;

	verify(nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	for (c = 0; c < children; ++c)
		if (nvlist_uint64_match(child[c], ZPOOL_CONFIG_GUID, guid))
			return (B_TRUE);
	return (B_FALSE);
}

static boolean_t
vdev_child_string_match(nvlist_t *vdev, char *tgt)
{
	nvlist_t **child;
	uint_t c, children;

	verify(nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);
	for (c = 0; c < children; ++c) {
		if (nvlist_string_match(child[c], ZPOOL_CONFIG_PATH, tgt) ||
		    nvlist_string_match(child[c], ZPOOL_CONFIG_DEVID, tgt))
			return (B_TRUE);
	}
	return (B_FALSE);
}

static boolean_t
vdev_guid_match(nvlist_t *config, uint64_t guid)
{
	nvlist_t *nvroot;

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	return (nvlist_uint64_match(nvroot, ZPOOL_CONFIG_GUID, guid) ||
	    vdev_child_guid_match(nvroot, guid));
}

static boolean_t
vdev_string_match(nvlist_t *config, char *tgt)
{
	nvlist_t *nvroot;

	verify(nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE,
	    &nvroot) == 0);

	return (vdev_child_string_match(nvroot, tgt));
}

static boolean_t
pool_match(nvlist_t *config, char *tgt)
{
	uint64_t guid = strtoull(tgt, NULL, 0);

	if (guid != 0) {
		return (
		    nvlist_uint64_match(config, ZPOOL_CONFIG_POOL_GUID, guid) ||
		    vdev_guid_match(config, guid));
	} else {
		return (
		    nvlist_string_match(config, ZPOOL_CONFIG_POOL_NAME, tgt) ||
		    vdev_string_match(config, tgt));
	}
}

static int
find_exported_zpool(char *pool_id, nvlist_t **configp, char *vdev_dir,
    char *cachefile)
{
	nvlist_t *pools;
	int error = ENOENT;
	nvlist_t *match = NULL;

	if (vdev_dir != NULL)
		pools = zpool_find_import(g_zfs, 1, &vdev_dir, B_TRUE);
	else if (cachefile != NULL)
		pools = zpool_find_import_cached(g_zfs, cachefile, B_TRUE);
	else
		pools = zpool_find_import(g_zfs, 0, NULL, B_TRUE);

	if (pools != NULL) {
		nvpair_t *elem = NULL;

		while ((elem = nvlist_next_nvpair(pools, elem)) != NULL) {
			verify(nvpair_value_nvlist(elem, configp) == 0);
			if (pool_match(*configp, pool_id)) {
				if (match != NULL) {
					(void) fatal(
					    "More than one matching pool - "
					    "specify guid/devid/device path.");
				} else {
					match = *configp;
					error = 0;
				}
			}
		}
	}

	*configp = error ? NULL : match;

	return (error);
}

int
main(int argc, char **argv)
{
	int i, c;
	struct rlimit rl = { 1024, 1024 };
	spa_t *spa;
	objset_t *os = NULL;
	char *endstr;
	int dump_all = 1;
	int verbose = 0;
	int error;
	int flag, set;
	int exported = 0;
	char *vdev_dir = NULL;
	char *cachefile = NULL;

	(void) setrlimit(RLIMIT_NOFILE, &rl);
	(void) enable_extended_FILE_stdio(-1, -1);

	dprintf_setup(&argc, argv);

	while ((c = getopt(argc, argv, "udibcsvCLO:B:S:U:lRep:")) != -1) {
		switch (c) {
		case 'u':
		case 'd':
		case 'i':
		case 'b':
		case 'c':
		case 's':
		case 'C':
		case 'l':
		case 'R':
			dump_opt[c]++;
			dump_all = 0;
			break;
		case 'L':
			dump_opt[c]++;
			break;
		case 'O':
			endstr = optarg;
			if (endstr[0] == '!') {
				endstr++;
				set = 0;
			} else {
				set = 1;
			}
			if (strcmp(endstr, "post") == 0) {
				flag = ADVANCE_PRE;
				set = !set;
			} else if (strcmp(endstr, "pre") == 0) {
				flag = ADVANCE_PRE;
			} else if (strcmp(endstr, "prune") == 0) {
				flag = ADVANCE_PRUNE;
			} else if (strcmp(endstr, "data") == 0) {
				flag = ADVANCE_DATA;
			} else if (strcmp(endstr, "holes") == 0) {
				flag = ADVANCE_HOLES;
			} else {
				usage();
			}
			if (set)
				zdb_advance |= flag;
			else
				zdb_advance &= ~flag;
			break;
		case 'B':
			endstr = optarg - 1;
			zdb_noread.zb_objset = strtoull(endstr + 1, &endstr, 0);
			zdb_noread.zb_object = strtoull(endstr + 1, &endstr, 0);
			zdb_noread.zb_level = strtol(endstr + 1, &endstr, 0);
			zdb_noread.zb_blkid = strtoull(endstr + 1, &endstr, 16);
			(void) printf("simulating bad block "
			    "<%llu, %llu, %lld, %llx>\n",
			    (u_longlong_t)zdb_noread.zb_objset,
			    (u_longlong_t)zdb_noread.zb_object,
			    (u_longlong_t)zdb_noread.zb_level,
			    (u_longlong_t)zdb_noread.zb_blkid);
			break;
		case 'v':
			verbose++;
			break;
		case 'U':
			cachefile = optarg;
			break;
		case 'e':
			exported = 1;
			break;
		case 'p':
			vdev_dir = optarg;
			break;
		case 'S':
			dump_opt[c]++;
			dump_all = 0;
			zdb_sig_user_data = (strncmp(optarg, "user:", 5) == 0);
			if (!zdb_sig_user_data && strncmp(optarg, "all:", 4))
				usage();
			endstr = strchr(optarg, ':') + 1;
			if (strcmp(endstr, "fletcher2") == 0)
				zdb_sig_cksumalg = ZIO_CHECKSUM_FLETCHER_2;
			else if (strcmp(endstr, "fletcher4") == 0)
				zdb_sig_cksumalg = ZIO_CHECKSUM_FLETCHER_4;
			else if (strcmp(endstr, "sha256") == 0)
				zdb_sig_cksumalg = ZIO_CHECKSUM_SHA256;
			else if (strcmp(endstr, "all") == 0)
				zdb_sig_cksumalg = ZIO_CHECKSUM_FLETCHER_2;
			else
				usage();
			break;
		default:
			usage();
			break;
		}
	}

	if (vdev_dir != NULL && exported == 0)
		(void) fatal("-p option requires use of -e\n");

	kernel_init(FREAD);
	g_zfs = libzfs_init();
	ASSERT(g_zfs != NULL);

	/*
	 * Disable vdev caching.  If we don't do this, live pool traversal
	 * won't make progress because it will never see disk updates.
	 */
	zfs_vdev_cache_size = 0;

	for (c = 0; c < 256; c++) {
		if (dump_all && c != 'L' && c != 'l' && c != 'R')
			dump_opt[c] = 1;
		if (dump_opt[c])
			dump_opt[c] += verbose;
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		if (dump_opt['C']) {
			dump_config(NULL);
			return (0);
		}
		usage();
	}

	if (dump_opt['l']) {
		dump_label(argv[0]);
		return (0);
	}

	if (dump_opt['R']) {
		flagbits['b'] = ZDB_FLAG_PRINT_BLKPTR;
		flagbits['c'] = ZDB_FLAG_CHECKSUM;
		flagbits['d'] = ZDB_FLAG_DECOMPRESS;
		flagbits['e'] = ZDB_FLAG_BSWAP;
		flagbits['g'] = ZDB_FLAG_GBH;
		flagbits['i'] = ZDB_FLAG_INDIRECT;
		flagbits['p'] = ZDB_FLAG_PHYS;
		flagbits['r'] = ZDB_FLAG_RAW;

		spa = NULL;
		while (argv[0]) {
			zdb_read_block(argv[0], &spa);
			argv++;
			argc--;
		}
		if (spa)
			spa_close(spa, (void *)zdb_read_block);
		return (0);
	}

	if (dump_opt['C'])
		dump_config(argv[0]);

	if (exported == 0 && cachefile == NULL) {
		if (strchr(argv[0], '/') != NULL) {
			error = dmu_objset_open(argv[0], DMU_OST_ANY,
			    DS_MODE_STANDARD | DS_MODE_READONLY, &os);
		} else {
			error = spa_open(argv[0], &spa, FTAG);
		}
	} else {
		/*
		 * Check to see if the name refers to an exported zpool
		 */
		nvlist_t *exported_conf = NULL;

		error = find_exported_zpool(argv[0], &exported_conf, vdev_dir,
		    cachefile);
		if (error == 0) {
			nvlist_t *nvl = NULL;

			if (vdev_dir != NULL) {
				if (nvlist_alloc(&nvl, NV_UNIQUE_NAME, 0) != 0)
					error = ENOMEM;
				else if (nvlist_add_string(nvl,
				    zpool_prop_to_name(ZPOOL_PROP_ALTROOT),
				    vdev_dir) != 0)
					error = ENOMEM;
			}

			if (error == 0)
				error = spa_import(argv[0], exported_conf, nvl);
			if (error == 0)
				error = spa_open(argv[0], &spa, FTAG);

			nvlist_free(nvl);
		}
	}

	if (error)
		fatal("can't open %s: %s", argv[0], strerror(error));

	argv++;
	if (--argc > 0) {
		zopt_objects = argc;
		zopt_object = calloc(zopt_objects, sizeof (uint64_t));
		for (i = 0; i < zopt_objects; i++) {
			errno = 0;
			zopt_object[i] = strtoull(argv[i], NULL, 0);
			if (zopt_object[i] == 0 && errno != 0)
				fatal("bad object number %s: %s",
				    argv[i], strerror(errno));
		}
	}

	if (os != NULL) {
		dump_dir(os);
		dmu_objset_close(os);
	} else {
		dump_zpool(spa);
		spa_close(spa, FTAG);
	}

	fuid_table_destroy();

	libzfs_fini(g_zfs);
	kernel_fini();

	return (0);
}
