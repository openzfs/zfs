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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2016 Gvozden Nešković. All rights reserved.
 */

#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/zap.h>
#include <sys/vdev_impl.h>
#include <sys/metaslab_impl.h>
#include <sys/zio.h>
#include <sys/zio_checksum.h>
#include <sys/dmu_tx.h>
#include <sys/abd.h>
#include <sys/zfs_rlock.h>
#include <sys/fs/zfs.h>
#include <sys/fm/fs/zfs.h>
#include <sys/vdev_raidz.h>
#include <sys/vdev_raidz_impl.h>
#include <sys/vdev_draid.h>
#include <sys/uberblock_impl.h>
#include <sys/dsl_scan.h>

#ifdef ZFS_DEBUG
#include <sys/vdev.h>	/* For vdev_xlate() in vdev_raidz_io_verify() */
#endif

/*
 * Virtual device vector for RAID-Z.
 *
 * This vdev supports single, double, and triple parity. For single parity,
 * we use a simple XOR of all the data columns. For double or triple parity,
 * we use a special case of Reed-Solomon coding. This extends the
 * technique described in "The mathematics of RAID-6" by H. Peter Anvin by
 * drawing on the system described in "A Tutorial on Reed-Solomon Coding for
 * Fault-Tolerance in RAID-like Systems" by James S. Plank on which the
 * former is also based. The latter is designed to provide higher performance
 * for writes.
 *
 * Note that the Plank paper claimed to support arbitrary N+M, but was then
 * amended six years later identifying a critical flaw that invalidates its
 * claims. Nevertheless, the technique can be adapted to work for up to
 * triple parity. For additional parity, the amendment "Note: Correction to
 * the 1997 Tutorial on Reed-Solomon Coding" by James S. Plank and Ying Ding
 * is viable, but the additional complexity means that write performance will
 * suffer.
 *
 * All of the methods above operate on a Galois field, defined over the
 * integers mod 2^N. In our case we choose N=8 for GF(8) so that all elements
 * can be expressed with a single byte. Briefly, the operations on the
 * field are defined as follows:
 *
 *   o addition (+) is represented by a bitwise XOR
 *   o subtraction (-) is therefore identical to addition: A + B = A - B
 *   o multiplication of A by 2 is defined by the following bitwise expression:
 *
 *	(A * 2)_7 = A_6
 *	(A * 2)_6 = A_5
 *	(A * 2)_5 = A_4
 *	(A * 2)_4 = A_3 + A_7
 *	(A * 2)_3 = A_2 + A_7
 *	(A * 2)_2 = A_1 + A_7
 *	(A * 2)_1 = A_0
 *	(A * 2)_0 = A_7
 *
 * In C, multiplying by 2 is therefore ((a << 1) ^ ((a & 0x80) ? 0x1d : 0)).
 * As an aside, this multiplication is derived from the error correcting
 * primitive polynomial x^8 + x^4 + x^3 + x^2 + 1.
 *
 * Observe that any number in the field (except for 0) can be expressed as a
 * power of 2 -- a generator for the field. We store a table of the powers of
 * 2 and logs base 2 for quick look ups, and exploit the fact that A * B can
 * be rewritten as 2^(log_2(A) + log_2(B)) (where '+' is normal addition rather
 * than field addition). The inverse of a field element A (A^-1) is therefore
 * A ^ (255 - 1) = A^254.
 *
 * The up-to-three parity columns, P, Q, R over several data columns,
 * D_0, ... D_n-1, can be expressed by field operations:
 *
 *	P = D_0 + D_1 + ... + D_n-2 + D_n-1
 *	Q = 2^n-1 * D_0 + 2^n-2 * D_1 + ... + 2^1 * D_n-2 + 2^0 * D_n-1
 *	  = ((...((D_0) * 2 + D_1) * 2 + ...) * 2 + D_n-2) * 2 + D_n-1
 *	R = 4^n-1 * D_0 + 4^n-2 * D_1 + ... + 4^1 * D_n-2 + 4^0 * D_n-1
 *	  = ((...((D_0) * 4 + D_1) * 4 + ...) * 4 + D_n-2) * 4 + D_n-1
 *
 * We chose 1, 2, and 4 as our generators because 1 corresponds to the trivial
 * XOR operation, and 2 and 4 can be computed quickly and generate linearly-
 * independent coefficients. (There are no additional coefficients that have
 * this property which is why the uncorrected Plank method breaks down.)
 *
 * See the reconstruction code below for how P, Q and R can used individually
 * or in concert to recover missing data columns.
 */

#define	VDEV_RAIDZ_P		0
#define	VDEV_RAIDZ_Q		1
#define	VDEV_RAIDZ_R		2

#define	VDEV_RAIDZ_MUL_2(x)	(((x) << 1) ^ (((x) & 0x80) ? 0x1d : 0))
#define	VDEV_RAIDZ_MUL_4(x)	(VDEV_RAIDZ_MUL_2(VDEV_RAIDZ_MUL_2(x)))

/*
 * We provide a mechanism to perform the field multiplication operation on a
 * 64-bit value all at once rather than a byte at a time. This works by
 * creating a mask from the top bit in each byte and using that to
 * conditionally apply the XOR of 0x1d.
 */
#define	VDEV_RAIDZ_64MUL_2(x, mask) \
{ \
	(mask) = (x) & 0x8080808080808080ULL; \
	(mask) = ((mask) << 1) - ((mask) >> 7); \
	(x) = (((x) << 1) & 0xfefefefefefefefeULL) ^ \
	    ((mask) & 0x1d1d1d1d1d1d1d1dULL); \
}

#define	VDEV_RAIDZ_64MUL_4(x, mask) \
{ \
	VDEV_RAIDZ_64MUL_2((x), mask); \
	VDEV_RAIDZ_64MUL_2((x), mask); \
}


/*
 * Big Theory Statement for how a RAIDZ VDEV is expanded
 *
 * An existing RAIDZ VDEV can be expanded by attaching a new disk. Expansion
 * works with all three RAIDZ parity choices, including RAIDZ1, 2, or 3. VDEVs
 * that have been previously expanded can be expanded again.
 *
 * The RAIDZ VDEV must be healthy (must be able to write to all the drives in
 * the VDEV) when an expansion starts.  And the expansion will pause if any
 * disk in the VDEV fails, and resume once the VDEV is healthy again. All other
 * operations on the pool can continue while an expansion is in progress (e.g.
 * read/write, snapshot, zpool add, etc). Except zpool checkpoint, zpool trim,
 * and zpool initialize which can't be run during an expansion.  Following a
 * reboot or export/import, the expansion resumes where it left off.
 *
 * == Reflowing the Data ==
 *
 * The expansion involves reflowing (copying) the data from the current set
 * of disks to spread it across the new set which now has one more disk. This
 * reflow operation is similar to reflowing text when the column width of a
 * text editor window is expanded. The text doesn’t change but the location of
 * the text changes to accommodate the new width. An example reflow result for
 * a 4-wide RAIDZ1 to a 5-wide is shown below.
 *
 *                            Reflow End State
 *            Each letter indicates a parity group (logical stripe)
 *
 *         Before expansion                         After Expansion
 *     D1     D2     D3     D4               D1     D2     D3     D4     D5
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  A   |  A   |  A   |  A   |         |  A   |  A   |  A   |  A   |  B   |
 *  |     1|     2|     3|     4|         |     1|     2|     3|     4|     5|
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  B   |  B   |  C   |  C   |         |  B   |  C   |  C   |  C   |  C   |
 *  |     5|     6|     7|     8|         |     6|     7|     8|     9|    10|
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  C   |  C   |  D   |  D   |         |  D   |  D   |  E   |  E   |  E   |
 *  |     9|    10|    11|    12|         |    11|    12|    13|    14|    15|
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  E   |  E   |  E   |  E   |   -->   |  E   |  F   |  F   |  G   |  G   |
 *  |    13|    14|    15|    16|         |    16|    17|    18|p   19|    20|
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  F   |  F   |  G   |  G   |         |  G   |  G   |  H   |  H   |  H   |
 *  |    17|    18|    19|    20|         |    21|    22|    23|    24|    25|
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  G   |  G   |  H   |  H   |         |  H   |  I   |  I   |  J   |  J   |
 *  |    21|    22|    23|    24|         |    26|    27|    28|    29|    30|
 *  +------+------+------+------+         +------+------+------+------+------+
 *  |      |      |      |      |         |      |      |      |      |      |
 *  |  H   |  H   |  I   |  I   |         |  J   |  J   |      |      |  K   |
 *  |    25|    26|    27|    28|         |    31|    32|    33|    34|    35|
 *  +------+------+------+------+         +------+------+------+------+------+
 *
 * This reflow approach has several advantages. There is no need to read or
 * modify the block pointers or recompute any block checksums.  The reflow
 * doesn’t need to know where the parity sectors reside. We can read and write
 * data sequentially and the copy can occur in a background thread in open
 * context. The design also allows for fast discovery of what data to copy.
 *
 * The VDEV metaslabs are processed, one at a time, to copy the block data to
 * have it flow across all the disks. The metaslab is disabled for allocations
 * during the copy. As an optimization, we only copy the allocated data which
 * can be determined by looking at the metaslab range tree. During the copy we
 * must maintain the redundancy guarantees of the RAIDZ VDEV (i.e., we still
 * need to be able to survive losing parity count disks).  This means we
 * cannot overwrite data during the reflow that would be needed if a disk is
 * lost.
 *
 * After the reflow completes, all newly-written blocks will have the new
 * layout, i.e., they will have the parity to data ratio implied by the new
 * number of disks in the RAIDZ group.  Even though the reflow copies all of
 * the allocated space (data and parity), it is only rearranged, not changed.
 *
 * This act of reflowing the data has a few implications about blocks
 * that were written before the reflow completes:
 *
 *  - Old blocks will still use the same amount of space (i.e., they will have
 *    the parity to data ratio implied by the old number of disks in the RAIDZ
 *    group).
 *  - Reading old blocks will be slightly slower than before the reflow, for
 *    two reasons. First, we will have to read from all disks in the RAIDZ
 *    VDEV, rather than being able to skip the children that contain only
 *    parity of this block (because the data of a single block is now spread
 *    out across all the disks).  Second, in most cases there will be an extra
 *    bcopy, needed to rearrange the data back to its original layout in memory.
 *
 * == Scratch Area ==
 *
 * As we copy the block data, we can only progress to the point that writes
 * will not overlap with blocks whose progress has not yet been recorded on
 * disk.  Since partially-copied rows are always read from the old location,
 * we need to stop one row before the sector-wise overlap, to prevent any
 * row-wise overlap. For example, in the diagram above, when we reflow sector
 * B6 it will overwite the original location for B5.
 *
 * To get around this, a scratch space is used so that we can start copying
 * without risking data loss by overlapping the row. As an added benefit, it
 * improves performance at the beginning of the reflow, but that small perf
 * boost wouldn't be worth the complexity on its own.
 *
 * Ideally we want to copy at least 2 * (new_width)^2 so that we have a
 * separation of 2*(new_width+1) and a chunk size of new_width+2. With the max
 * RAIDZ width of 255 and 4K sectors this would be 2MB per disk. In practice
 * the widths will likely be single digits so we can get a substantial chuck
 * size using only a few MB of scratch per disk.
 *
 * The scratch area is persisted to disk which holds a large amount of reflowed
 * state. We can always read the partially written stripes when a disk fails or
 * the copy is interrupted (crash) during the initial copying phase and also
 * get past a small chunk size restriction.  At a minimum, the scratch space
 * must be large enough to get us to the point that one row does not overlap
 * itself when moved (i.e new_width^2).  But going larger is even better. We
 * use the 3.5 MiB reserved "boot" space that resides after the ZFS disk labels
 * as our scratch space to handle overwriting the initial part of the VDEV.
 *
 *	0     256K   512K                    4M
 *	+------+------+-----------------------+-----------------------------
 *	| VDEV | VDEV |   Boot Block (3.5M)   |  Allocatable space ...
 *	|  L0  |  L1  |       Reserved        |     (Metaslabs)
 *	+------+------+-----------------------+-------------------------------
 *                        Scratch Area
 *
 * == Reflow Progress Updates ==
 * After the initial scratch-based reflow, the expansion process works
 * similarly to device removal. We create a new open context thread which
 * reflows the data, and periodically kicks off sync tasks to update logical
 * state. In this case, state is the committed progress (offset of next data
 * to copy). We need to persist the completed offset on disk, so that if we
 * crash we know which format each VDEV offset is in.
 *
 * == Time Dependent Geometry ==
 *
 * In non-expanded RAIDZ, blocks are read from disk in a column by column
 * fashion. For a multi-row block, the second sector is in the first column
 * not in the second column. This allows us to issue full reads for each
 * column directly into the request buffer. The block data is thus laid out
 * sequentially in a column-by-column fashion.
 *
 * For example, in the before expansion diagram above, one logical block might
 * be sectors G19-H26. The parity is in G19,H23; and the data is in
 * G20,H24,G21,H25,G22,H26.
 *
 * After a block is reflowed, the sectors that were all in the original column
 * data can now reside in different columns. When reading from an expanded
 * VDEV, we need to know the logical stripe width for each block so we can
 * reconstitute the block’s data after the reads are completed. Likewise,
 * when we perform the combinatorial reconstruction we need to know the
 * original width so we can retry combinations from the past layouts.
 *
 * Time dependent geometry is what we call having blocks with different layouts
 * (stripe widths) in the same VDEV. This time-dependent geometry uses the
 * block’s birth time (+ the time expansion ended) to establish the correct
 * width for a given block. After an expansion completes, we record the time
 * for blocks written with a particular width (geometry).
 *
 * == On Disk Format Changes ==
 *
 * New pool feature flag, 'raidz_expansion' whose reference count is the number
 * of RAIDZ VDEVs that have been expanded.
 *
 * The blocks on expanded RAIDZ VDEV can have different logical stripe widths.
 *
 * Since the uberblock can point to arbitrary blocks, which might be on the
 * expanding RAIDZ, and might or might not have been expanded. We need to know
 * which way a block is laid out before reading it. This info is the next
 * offset that needs to be reflowed and we persist that in the uberblock, in
 * the new ub_raidz_reflow_info field, as opposed to the MOS or the vdev label.
 * After the expansion is complete, we then use the raidz_expand_txgs array
 * (see below) to determine how to read a block and the ub_raidz_reflow_info
 * field no longer required.
 *
 * The uberblock's ub_raidz_reflow_info field also holds the scratch space
 * state (i.e., active or not) which is also required before reading a block
 * during the initial phase of reflowing the data.
 *
 * The top-level RAIDZ VDEV has two new entries in the nvlist:
 *
 * 'raidz_expand_txgs' array: logical stripe widths by txg are recorded here
 *                            and used after the expansion is complete to
 *                            determine how to read a raidz block
 * 'raidz_expanding' boolean: present during reflow and removed after completion
 *                            used during a spa import to resume an unfinished
 *                            expansion
 *
 * And finally the VDEVs top zap adds the following informational entries:
 *   VDEV_TOP_ZAP_RAIDZ_EXPAND_STATE
 *   VDEV_TOP_ZAP_RAIDZ_EXPAND_START_TIME
 *   VDEV_TOP_ZAP_RAIDZ_EXPAND_END_TIME
 *   VDEV_TOP_ZAP_RAIDZ_EXPAND_BYTES_COPIED
 */

/*
 * For testing only: pause the raidz expansion after reflowing this amount.
 * (accessed by ZTS and ztest)
 */
#ifdef	_KERNEL
static
#endif	/* _KERNEL */
unsigned long raidz_expand_max_reflow_bytes = 0;

/*
 * For testing only: pause the raidz expansion at a certain point.
 */
uint_t raidz_expand_pause_point = 0;

/*
 * Maximum amount of copy io's outstanding at once.
 */
#ifdef _ILP32
static unsigned long raidz_expand_max_copy_bytes = SPA_MAXBLOCKSIZE;
#else
static unsigned long raidz_expand_max_copy_bytes = 10 * SPA_MAXBLOCKSIZE;
#endif

/*
 * Apply raidz map abds aggregation if the number of rows in the map is equal
 * or greater than the value below.
 */
static unsigned long raidz_io_aggregate_rows = 4;

/*
 * Automatically start a pool scrub when a RAIDZ expansion completes in
 * order to verify the checksums of all blocks which have been copied
 * during the expansion.  Automatic scrubbing is enabled by default and
 * is strongly recommended.
 */
static int zfs_scrub_after_expand = 1;

static void
vdev_raidz_row_free(raidz_row_t *rr)
{
	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_size != 0)
			abd_free(rc->rc_abd);
		if (rc->rc_orig_data != NULL)
			abd_free(rc->rc_orig_data);
	}

	if (rr->rr_abd_empty != NULL)
		abd_free(rr->rr_abd_empty);

	kmem_free(rr, offsetof(raidz_row_t, rr_col[rr->rr_scols]));
}

void
vdev_raidz_map_free(raidz_map_t *rm)
{
	for (int i = 0; i < rm->rm_nrows; i++)
		vdev_raidz_row_free(rm->rm_row[i]);

	if (rm->rm_nphys_cols) {
		for (int i = 0; i < rm->rm_nphys_cols; i++) {
			if (rm->rm_phys_col[i].rc_abd != NULL)
				abd_free(rm->rm_phys_col[i].rc_abd);
		}

		kmem_free(rm->rm_phys_col, sizeof (raidz_col_t) *
		    rm->rm_nphys_cols);
	}

	ASSERT3P(rm->rm_lr, ==, NULL);
	kmem_free(rm, offsetof(raidz_map_t, rm_row[rm->rm_nrows]));
}

static void
vdev_raidz_map_free_vsd(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	vdev_raidz_map_free(rm);
}

static int
vdev_raidz_reflow_compare(const void *x1, const void *x2)
{
	const reflow_node_t *l = x1;
	const reflow_node_t *r = x2;

	return (TREE_CMP(l->re_txg, r->re_txg));
}

const zio_vsd_ops_t vdev_raidz_vsd_ops = {
	.vsd_free = vdev_raidz_map_free_vsd,
};

raidz_row_t *
vdev_raidz_row_alloc(int cols, zio_t *zio)
{
	raidz_row_t *rr =
	    kmem_zalloc(offsetof(raidz_row_t, rr_col[cols]), KM_SLEEP);

	rr->rr_cols = cols;
	rr->rr_scols = cols;

	for (int c = 0; c < cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		rc->rc_shadow_devidx = INT_MAX;
		rc->rc_shadow_offset = UINT64_MAX;
		/*
		 * We can not allow self healing to take place for Direct I/O
		 * reads. There is nothing that stops the buffer contents from
		 * being manipulated while the I/O is in flight. It is possible
		 * that the checksum could be verified on the buffer and then
		 * the contents of that buffer are manipulated afterwards. This
		 * could lead to bad data being written out during self
		 * healing.
		 */
		if (!(zio->io_flags & ZIO_FLAG_DIO_READ))
			rc->rc_allow_repair = 1;
	}
	return (rr);
}

static void
vdev_raidz_map_alloc_write(zio_t *zio, raidz_map_t *rm, uint64_t ashift)
{
	int c;
	int nwrapped = 0;
	uint64_t off = 0;
	raidz_row_t *rr = rm->rm_row[0];

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);
	ASSERT3U(rm->rm_nrows, ==, 1);

	/*
	 * Pad any parity columns with additional space to account for skip
	 * sectors.
	 */
	if (rm->rm_skipstart < rr->rr_firstdatacol) {
		ASSERT0(rm->rm_skipstart);
		nwrapped = rm->rm_nskip;
	} else if (rr->rr_scols < (rm->rm_skipstart + rm->rm_nskip)) {
		nwrapped =
		    (rm->rm_skipstart + rm->rm_nskip) % rr->rr_scols;
	}

	/*
	 * Optional single skip sectors (rc_size == 0) will be handled in
	 * vdev_raidz_io_start_write().
	 */
	int skipped = rr->rr_scols - rr->rr_cols;

	/* Allocate buffers for the parity columns */
	for (c = 0; c < rr->rr_firstdatacol; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		/*
		 * Parity columns will pad out a linear ABD to account for
		 * the skip sector. A linear ABD is used here because
		 * parity calculations use the ABD buffer directly to calculate
		 * parity. This avoids doing a memcpy back to the ABD after the
		 * parity has been calculated. By issuing the parity column
		 * with the skip sector we can reduce contention on the child
		 * VDEV queue locks (vq_lock).
		 */
		if (c < nwrapped) {
			rc->rc_abd = abd_alloc_linear(
			    rc->rc_size + (1ULL << ashift), B_FALSE);
			abd_zero_off(rc->rc_abd, rc->rc_size, 1ULL << ashift);
			skipped++;
		} else {
			rc->rc_abd = abd_alloc_linear(rc->rc_size, B_FALSE);
		}
	}

	for (off = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		abd_t *abd = abd_get_offset_struct(&rc->rc_abdstruct,
		    zio->io_abd, off, rc->rc_size);

		/*
		 * Generate I/O for skip sectors to improve aggregation
		 * continuity. We will use gang ABD's to reduce contention
		 * on the child VDEV queue locks (vq_lock) by issuing
		 * a single I/O that contains the data and skip sector.
		 *
		 * It is important to make sure that rc_size is not updated
		 * even though we are adding a skip sector to the ABD. When
		 * calculating the parity in vdev_raidz_generate_parity_row()
		 * the rc_size is used to iterate through the ABD's. We can
		 * not have zero'd out skip sectors used for calculating
		 * parity for raidz, because those same sectors are not used
		 * during reconstruction.
		 */
		if (c >= rm->rm_skipstart && skipped < rm->rm_nskip) {
			rc->rc_abd = abd_alloc_gang();
			abd_gang_add(rc->rc_abd, abd, B_TRUE);
			abd_gang_add(rc->rc_abd,
			    abd_get_zeros(1ULL << ashift), B_TRUE);
			skipped++;
		} else {
			rc->rc_abd = abd;
		}
		off += rc->rc_size;
	}

	ASSERT3U(off, ==, zio->io_size);
	ASSERT3S(skipped, ==, rm->rm_nskip);
}

static void
vdev_raidz_map_alloc_read(zio_t *zio, raidz_map_t *rm)
{
	int c;
	raidz_row_t *rr = rm->rm_row[0];

	ASSERT3U(rm->rm_nrows, ==, 1);

	/* Allocate buffers for the parity columns */
	for (c = 0; c < rr->rr_firstdatacol; c++)
		rr->rr_col[c].rc_abd =
		    abd_alloc_linear(rr->rr_col[c].rc_size, B_FALSE);

	for (uint64_t off = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		rc->rc_abd = abd_get_offset_struct(&rc->rc_abdstruct,
		    zio->io_abd, off, rc->rc_size);
		off += rc->rc_size;
	}
}

/*
 * Divides the IO evenly across all child vdevs; usually, dcols is
 * the number of children in the target vdev.
 *
 * Avoid inlining the function to keep vdev_raidz_io_start(), which
 * is this functions only caller, as small as possible on the stack.
 */
noinline raidz_map_t *
vdev_raidz_map_alloc(zio_t *zio, uint64_t ashift, uint64_t dcols,
    uint64_t nparity)
{
	raidz_row_t *rr;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = zio->io_offset >> ashift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = zio->io_size >> ashift;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;
	/* The starting byte offset on each child vdev. */
	uint64_t o = (b / dcols) << ashift;
	uint64_t acols, scols;

	raidz_map_t *rm =
	    kmem_zalloc(offsetof(raidz_map_t, rm_row[1]), KM_SLEEP);
	rm->rm_nrows = 1;

	/*
	 * "Quotient": The number of data sectors for this stripe on all but
	 * the "big column" child vdevs that also contain "remainder" data.
	 */
	uint64_t q = s / (dcols - nparity);

	/*
	 * "Remainder": The number of partial stripe data sectors in this I/O.
	 * This will add a sector to some, but not all, child vdevs.
	 */
	uint64_t r = s - q * (dcols - nparity);

	/* The number of "big columns" - those which contain remainder data. */
	uint64_t bc = (r == 0 ? 0 : r + nparity);

	/*
	 * The total number of data and parity sectors associated with
	 * this I/O.
	 */
	uint64_t tot = s + nparity * (q + (r == 0 ? 0 : 1));

	/*
	 * acols: The columns that will be accessed.
	 * scols: The columns that will be accessed or skipped.
	 */
	if (q == 0) {
		/* Our I/O request doesn't span all child vdevs. */
		acols = bc;
		scols = MIN(dcols, roundup(bc, nparity + 1));
	} else {
		acols = dcols;
		scols = dcols;
	}

	ASSERT3U(acols, <=, scols);
	rr = vdev_raidz_row_alloc(scols, zio);
	rm->rm_row[0] = rr;
	rr->rr_cols = acols;
	rr->rr_bigcols = bc;
	rr->rr_firstdatacol = nparity;
#ifdef ZFS_DEBUG
	rr->rr_offset = zio->io_offset;
	rr->rr_size = zio->io_size;
#endif

	uint64_t asize = 0;

	for (uint64_t c = 0; c < scols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		uint64_t col = f + c;
		uint64_t coff = o;
		if (col >= dcols) {
			col -= dcols;
			coff += 1ULL << ashift;
		}
		rc->rc_devidx = col;
		rc->rc_offset = coff;

		if (c >= acols)
			rc->rc_size = 0;
		else if (c < bc)
			rc->rc_size = (q + 1) << ashift;
		else
			rc->rc_size = q << ashift;

		asize += rc->rc_size;
	}

	ASSERT3U(asize, ==, tot << ashift);
	rm->rm_nskip = roundup(tot, nparity + 1) - tot;
	rm->rm_skipstart = bc;

	/*
	 * If all data stored spans all columns, there's a danger that parity
	 * will always be on the same device and, since parity isn't read
	 * during normal operation, that device's I/O bandwidth won't be
	 * used effectively. We therefore switch the parity every 1MB.
	 *
	 * ... at least that was, ostensibly, the theory. As a practical
	 * matter unless we juggle the parity between all devices evenly, we
	 * won't see any benefit. Further, occasional writes that aren't a
	 * multiple of the LCM of the number of children and the minimum
	 * stripe width are sufficient to avoid pessimal behavior.
	 * Unfortunately, this decision created an implicit on-disk format
	 * requirement that we need to support for all eternity, but only
	 * for single-parity RAID-Z.
	 *
	 * If we intend to skip a sector in the zeroth column for padding
	 * we must make sure to note this swap. We will never intend to
	 * skip the first column since at least one data and one parity
	 * column must appear in each row.
	 */
	ASSERT(rr->rr_cols >= 2);
	ASSERT(rr->rr_col[0].rc_size == rr->rr_col[1].rc_size);

	if (rr->rr_firstdatacol == 1 && (zio->io_offset & (1ULL << 20))) {
		uint64_t devidx = rr->rr_col[0].rc_devidx;
		o = rr->rr_col[0].rc_offset;
		rr->rr_col[0].rc_devidx = rr->rr_col[1].rc_devidx;
		rr->rr_col[0].rc_offset = rr->rr_col[1].rc_offset;
		rr->rr_col[1].rc_devidx = devidx;
		rr->rr_col[1].rc_offset = o;
		if (rm->rm_skipstart == 0)
			rm->rm_skipstart = 1;
	}

	if (zio->io_type == ZIO_TYPE_WRITE) {
		vdev_raidz_map_alloc_write(zio, rm, ashift);
	} else {
		vdev_raidz_map_alloc_read(zio, rm);
	}
	/* init RAIDZ parity ops */
	rm->rm_ops = vdev_raidz_math_get_ops();

	return (rm);
}

/*
 * Everything before reflow_offset_synced should have been moved to the new
 * location (read and write completed).  However, this may not yet be reflected
 * in the on-disk format (e.g. raidz_reflow_sync() has been called but the
 * uberblock has not yet been written). If reflow is not in progress,
 * reflow_offset_synced should be UINT64_MAX. For each row, if the row is
 * entirely before reflow_offset_synced, it will come from the new location.
 * Otherwise this row will come from the old location.  Therefore, rows that
 * straddle the reflow_offset_synced will come from the old location.
 *
 * For writes, reflow_offset_next is the next offset to copy.  If a sector has
 * been copied, but not yet reflected in the on-disk progress
 * (reflow_offset_synced), it will also be written to the new (already copied)
 * offset.
 */
noinline raidz_map_t *
vdev_raidz_map_alloc_expanded(zio_t *zio,
    uint64_t ashift, uint64_t physical_cols, uint64_t logical_cols,
    uint64_t nparity, uint64_t reflow_offset_synced,
    uint64_t reflow_offset_next, boolean_t use_scratch)
{
	abd_t *abd = zio->io_abd;
	uint64_t offset = zio->io_offset;
	uint64_t size = zio->io_size;

	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = size >> ashift;

	/*
	 * "Quotient": The number of data sectors for this stripe on all but
	 * the "big column" child vdevs that also contain "remainder" data.
	 * AKA "full rows"
	 */
	uint64_t q = s / (logical_cols - nparity);

	/*
	 * "Remainder": The number of partial stripe data sectors in this I/O.
	 * This will add a sector to some, but not all, child vdevs.
	 */
	uint64_t r = s - q * (logical_cols - nparity);

	/* The number of "big columns" - those which contain remainder data. */
	uint64_t bc = (r == 0 ? 0 : r + nparity);

	/*
	 * The total number of data and parity sectors associated with
	 * this I/O.
	 */
	uint64_t tot = s + nparity * (q + (r == 0 ? 0 : 1));

	/* How many rows contain data (not skip) */
	uint64_t rows = howmany(tot, logical_cols);
	int cols = MIN(tot, logical_cols);

	raidz_map_t *rm =
	    kmem_zalloc(offsetof(raidz_map_t, rm_row[rows]),
	    KM_SLEEP);
	rm->rm_nrows = rows;
	rm->rm_nskip = roundup(tot, nparity + 1) - tot;
	rm->rm_skipstart = bc;
	uint64_t asize = 0;

	for (uint64_t row = 0; row < rows; row++) {
		boolean_t row_use_scratch = B_FALSE;
		raidz_row_t *rr = vdev_raidz_row_alloc(cols, zio);
		rm->rm_row[row] = rr;

		/* The starting RAIDZ (parent) vdev sector of the row. */
		uint64_t b = (offset >> ashift) + row * logical_cols;

		/*
		 * If we are in the middle of a reflow, and the copying has
		 * not yet completed for any part of this row, then use the
		 * old location of this row.  Note that reflow_offset_synced
		 * reflects the i/o that's been completed, because it's
		 * updated by a synctask, after zio_wait(spa_txg_zio[]).
		 * This is sufficient for our check, even if that progress
		 * has not yet been recorded to disk (reflected in
		 * spa_ubsync).  Also note that we consider the last row to
		 * be "full width" (`cols`-wide rather than `bc`-wide) for
		 * this calculation. This causes a tiny bit of unnecessary
		 * double-writes but is safe and simpler to calculate.
		 */
		int row_phys_cols = physical_cols;
		if (b + cols > reflow_offset_synced >> ashift)
			row_phys_cols--;
		else if (use_scratch)
			row_use_scratch = B_TRUE;

		/* starting child of this row */
		uint64_t child_id = b % row_phys_cols;
		/* The starting byte offset on each child vdev. */
		uint64_t child_offset = (b / row_phys_cols) << ashift;

		/*
		 * Note, rr_cols is the entire width of the block, even
		 * if this row is shorter.  This is needed because parity
		 * generation (for Q and R) needs to know the entire width,
		 * because it treats the short row as though it was
		 * full-width (and the "phantom" sectors were zero-filled).
		 *
		 * Another approach to this would be to set cols shorter
		 * (to just the number of columns that we might do i/o to)
		 * and have another mechanism to tell the parity generation
		 * about the "entire width".  Reconstruction (at least
		 * vdev_raidz_reconstruct_general()) would also need to
		 * know about the "entire width".
		 */
		rr->rr_firstdatacol = nparity;
#ifdef ZFS_DEBUG
		/*
		 * note: rr_size is PSIZE, not ASIZE
		 */
		rr->rr_offset = b << ashift;
		rr->rr_size = (rr->rr_cols - rr->rr_firstdatacol) << ashift;
#endif

		for (int c = 0; c < rr->rr_cols; c++, child_id++) {
			if (child_id >= row_phys_cols) {
				child_id -= row_phys_cols;
				child_offset += 1ULL << ashift;
			}
			raidz_col_t *rc = &rr->rr_col[c];
			rc->rc_devidx = child_id;
			rc->rc_offset = child_offset;

			/*
			 * Get this from the scratch space if appropriate.
			 * This only happens if we crashed in the middle of
			 * raidz_reflow_scratch_sync() (while it's running,
			 * the rangelock prevents us from doing concurrent
			 * io), and even then only during zpool import or
			 * when the pool is imported readonly.
			 */
			if (row_use_scratch)
				rc->rc_offset -= VDEV_BOOT_SIZE;

			uint64_t dc = c - rr->rr_firstdatacol;
			if (c < rr->rr_firstdatacol) {
				rc->rc_size = 1ULL << ashift;

				/*
				 * Parity sectors' rc_abd's are set below
				 * after determining if this is an aggregation.
				 */
			} else if (row == rows - 1 && bc != 0 && c >= bc) {
				/*
				 * Past the end of the block (even including
				 * skip sectors).  This sector is part of the
				 * map so that we have full rows for p/q parity
				 * generation.
				 */
				rc->rc_size = 0;
				rc->rc_abd = NULL;
			} else {
				/* "data column" (col excluding parity) */
				uint64_t off;

				if (c < bc || r == 0) {
					off = dc * rows + row;
				} else {
					off = r * rows +
					    (dc - r) * (rows - 1) + row;
				}
				rc->rc_size = 1ULL << ashift;
				rc->rc_abd = abd_get_offset_struct(
				    &rc->rc_abdstruct, abd, off << ashift,
				    rc->rc_size);
			}

			if (rc->rc_size == 0)
				continue;

			/*
			 * If any part of this row is in both old and new
			 * locations, the primary location is the old
			 * location. If this sector was already copied to the
			 * new location, we need to also write to the new,
			 * "shadow" location.
			 *
			 * Note, `row_phys_cols != physical_cols` indicates
			 * that the primary location is the old location.
			 * `b+c < reflow_offset_next` indicates that the copy
			 * to the new location has been initiated. We know
			 * that the copy has completed because we have the
			 * rangelock, which is held exclusively while the
			 * copy is in progress.
			 */
			if (row_use_scratch ||
			    (row_phys_cols != physical_cols &&
			    b + c < reflow_offset_next >> ashift)) {
				rc->rc_shadow_devidx = (b + c) % physical_cols;
				rc->rc_shadow_offset =
				    ((b + c) / physical_cols) << ashift;
				if (row_use_scratch)
					rc->rc_shadow_offset -= VDEV_BOOT_SIZE;
			}

			asize += rc->rc_size;
		}

		/*
		 * See comment in vdev_raidz_map_alloc()
		 */
		if (rr->rr_firstdatacol == 1 && rr->rr_cols > 1 &&
		    (offset & (1ULL << 20))) {
			ASSERT(rr->rr_cols >= 2);
			ASSERT(rr->rr_col[0].rc_size == rr->rr_col[1].rc_size);

			int devidx0 = rr->rr_col[0].rc_devidx;
			uint64_t offset0 = rr->rr_col[0].rc_offset;
			int shadow_devidx0 = rr->rr_col[0].rc_shadow_devidx;
			uint64_t shadow_offset0 =
			    rr->rr_col[0].rc_shadow_offset;

			rr->rr_col[0].rc_devidx = rr->rr_col[1].rc_devidx;
			rr->rr_col[0].rc_offset = rr->rr_col[1].rc_offset;
			rr->rr_col[0].rc_shadow_devidx =
			    rr->rr_col[1].rc_shadow_devidx;
			rr->rr_col[0].rc_shadow_offset =
			    rr->rr_col[1].rc_shadow_offset;

			rr->rr_col[1].rc_devidx = devidx0;
			rr->rr_col[1].rc_offset = offset0;
			rr->rr_col[1].rc_shadow_devidx = shadow_devidx0;
			rr->rr_col[1].rc_shadow_offset = shadow_offset0;
		}
	}
	ASSERT3U(asize, ==, tot << ashift);

	/*
	 * Determine if the block is contiguous, in which case we can use
	 * an aggregation.
	 */
	if (rows >= raidz_io_aggregate_rows) {
		rm->rm_nphys_cols = physical_cols;
		rm->rm_phys_col =
		    kmem_zalloc(sizeof (raidz_col_t) * rm->rm_nphys_cols,
		    KM_SLEEP);

		/*
		 * Determine the aggregate io's offset and size, and check
		 * that the io is contiguous.
		 */
		for (int i = 0;
		    i < rm->rm_nrows && rm->rm_phys_col != NULL; i++) {
			raidz_row_t *rr = rm->rm_row[i];
			for (int c = 0; c < rr->rr_cols; c++) {
				raidz_col_t *rc = &rr->rr_col[c];
				raidz_col_t *prc =
				    &rm->rm_phys_col[rc->rc_devidx];

				if (rc->rc_size == 0)
					continue;

				if (prc->rc_size == 0) {
					ASSERT0(prc->rc_offset);
					prc->rc_offset = rc->rc_offset;
				} else if (prc->rc_offset + prc->rc_size !=
				    rc->rc_offset) {
					/*
					 * This block is not contiguous and
					 * therefore can't be aggregated.
					 * This is expected to be rare, so
					 * the cost of allocating and then
					 * freeing rm_phys_col is not
					 * significant.
					 */
					kmem_free(rm->rm_phys_col,
					    sizeof (raidz_col_t) *
					    rm->rm_nphys_cols);
					rm->rm_phys_col = NULL;
					rm->rm_nphys_cols = 0;
					break;
				}
				prc->rc_size += rc->rc_size;
			}
		}
	}
	if (rm->rm_phys_col != NULL) {
		/*
		 * Allocate aggregate ABD's.
		 */
		for (int i = 0; i < rm->rm_nphys_cols; i++) {
			raidz_col_t *prc = &rm->rm_phys_col[i];

			prc->rc_devidx = i;

			if (prc->rc_size == 0)
				continue;

			prc->rc_abd =
			    abd_alloc_linear(rm->rm_phys_col[i].rc_size,
			    B_FALSE);
		}

		/*
		 * Point the parity abd's into the aggregate abd's.
		 */
		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];
			for (int c = 0; c < rr->rr_firstdatacol; c++) {
				raidz_col_t *rc = &rr->rr_col[c];
				raidz_col_t *prc =
				    &rm->rm_phys_col[rc->rc_devidx];
				rc->rc_abd =
				    abd_get_offset_struct(&rc->rc_abdstruct,
				    prc->rc_abd,
				    rc->rc_offset - prc->rc_offset,
				    rc->rc_size);
			}
		}
	} else {
		/*
		 * Allocate new abd's for the parity sectors.
		 */
		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];
			for (int c = 0; c < rr->rr_firstdatacol; c++) {
				raidz_col_t *rc = &rr->rr_col[c];
				rc->rc_abd =
				    abd_alloc_linear(rc->rc_size,
				    B_TRUE);
			}
		}
	}
	/* init RAIDZ parity ops */
	rm->rm_ops = vdev_raidz_math_get_ops();

	return (rm);
}

struct pqr_struct {
	uint64_t *p;
	uint64_t *q;
	uint64_t *r;
};

static int
vdev_raidz_p_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	int cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && !pqr->q && !pqr->r);

	for (int i = 0; i < cnt; i++, src++, pqr->p++)
		*pqr->p ^= *src;

	return (0);
}

static int
vdev_raidz_pq_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	uint64_t mask;
	int cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && pqr->q && !pqr->r);

	for (int i = 0; i < cnt; i++, src++, pqr->p++, pqr->q++) {
		*pqr->p ^= *src;
		VDEV_RAIDZ_64MUL_2(*pqr->q, mask);
		*pqr->q ^= *src;
	}

	return (0);
}

static int
vdev_raidz_pqr_func(void *buf, size_t size, void *private)
{
	struct pqr_struct *pqr = private;
	const uint64_t *src = buf;
	uint64_t mask;
	int cnt = size / sizeof (src[0]);

	ASSERT(pqr->p && pqr->q && pqr->r);

	for (int i = 0; i < cnt; i++, src++, pqr->p++, pqr->q++, pqr->r++) {
		*pqr->p ^= *src;
		VDEV_RAIDZ_64MUL_2(*pqr->q, mask);
		*pqr->q ^= *src;
		VDEV_RAIDZ_64MUL_4(*pqr->r, mask);
		*pqr->r ^= *src;
	}

	return (0);
}

static void
vdev_raidz_generate_parity_p(raidz_row_t *rr)
{
	uint64_t *p = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		abd_t *src = rr->rr_col[c].rc_abd;

		if (c == rr->rr_firstdatacol) {
			abd_copy_to_buf(p, src, rr->rr_col[c].rc_size);
		} else {
			struct pqr_struct pqr = { p, NULL, NULL };
			(void) abd_iterate_func(src, 0, rr->rr_col[c].rc_size,
			    vdev_raidz_p_func, &pqr);
		}
	}
}

static void
vdev_raidz_generate_parity_pq(raidz_row_t *rr)
{
	uint64_t *p = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	uint64_t *q = abd_to_buf(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);
	uint64_t pcnt = rr->rr_col[VDEV_RAIDZ_P].rc_size / sizeof (p[0]);
	ASSERT(rr->rr_col[VDEV_RAIDZ_P].rc_size ==
	    rr->rr_col[VDEV_RAIDZ_Q].rc_size);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		abd_t *src = rr->rr_col[c].rc_abd;

		uint64_t ccnt = rr->rr_col[c].rc_size / sizeof (p[0]);

		if (c == rr->rr_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			abd_copy_to_buf(p, src, rr->rr_col[c].rc_size);
			(void) memcpy(q, p, rr->rr_col[c].rc_size);

			for (uint64_t i = ccnt; i < pcnt; i++) {
				p[i] = 0;
				q[i] = 0;
			}
		} else {
			struct pqr_struct pqr = { p, q, NULL };

			ASSERT(ccnt <= pcnt);
			(void) abd_iterate_func(src, 0, rr->rr_col[c].rc_size,
			    vdev_raidz_pq_func, &pqr);

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			uint64_t mask;
			for (uint64_t i = ccnt; i < pcnt; i++) {
				VDEV_RAIDZ_64MUL_2(q[i], mask);
			}
		}
	}
}

static void
vdev_raidz_generate_parity_pqr(raidz_row_t *rr)
{
	uint64_t *p = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	uint64_t *q = abd_to_buf(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);
	uint64_t *r = abd_to_buf(rr->rr_col[VDEV_RAIDZ_R].rc_abd);
	uint64_t pcnt = rr->rr_col[VDEV_RAIDZ_P].rc_size / sizeof (p[0]);
	ASSERT(rr->rr_col[VDEV_RAIDZ_P].rc_size ==
	    rr->rr_col[VDEV_RAIDZ_Q].rc_size);
	ASSERT(rr->rr_col[VDEV_RAIDZ_P].rc_size ==
	    rr->rr_col[VDEV_RAIDZ_R].rc_size);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		abd_t *src = rr->rr_col[c].rc_abd;

		uint64_t ccnt = rr->rr_col[c].rc_size / sizeof (p[0]);

		if (c == rr->rr_firstdatacol) {
			ASSERT(ccnt == pcnt || ccnt == 0);
			abd_copy_to_buf(p, src, rr->rr_col[c].rc_size);
			(void) memcpy(q, p, rr->rr_col[c].rc_size);
			(void) memcpy(r, p, rr->rr_col[c].rc_size);

			for (uint64_t i = ccnt; i < pcnt; i++) {
				p[i] = 0;
				q[i] = 0;
				r[i] = 0;
			}
		} else {
			struct pqr_struct pqr = { p, q, r };

			ASSERT(ccnt <= pcnt);
			(void) abd_iterate_func(src, 0, rr->rr_col[c].rc_size,
			    vdev_raidz_pqr_func, &pqr);

			/*
			 * Treat short columns as though they are full of 0s.
			 * Note that there's therefore nothing needed for P.
			 */
			uint64_t mask;
			for (uint64_t i = ccnt; i < pcnt; i++) {
				VDEV_RAIDZ_64MUL_2(q[i], mask);
				VDEV_RAIDZ_64MUL_4(r[i], mask);
			}
		}
	}
}

/*
 * Generate RAID parity in the first virtual columns according to the number of
 * parity columns available.
 */
void
vdev_raidz_generate_parity_row(raidz_map_t *rm, raidz_row_t *rr)
{
	if (rr->rr_cols == 0) {
		/*
		 * We are handling this block one row at a time (because
		 * this block has a different logical vs physical width,
		 * due to RAIDZ expansion), and this is a pad-only row,
		 * which has no parity.
		 */
		return;
	}

	/* Generate using the new math implementation */
	if (vdev_raidz_math_generate(rm, rr) != RAIDZ_ORIGINAL_IMPL)
		return;

	switch (rr->rr_firstdatacol) {
	case 1:
		vdev_raidz_generate_parity_p(rr);
		break;
	case 2:
		vdev_raidz_generate_parity_pq(rr);
		break;
	case 3:
		vdev_raidz_generate_parity_pqr(rr);
		break;
	default:
		cmn_err(CE_PANIC, "invalid RAID-Z configuration");
	}
}

void
vdev_raidz_generate_parity(raidz_map_t *rm)
{
	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];
		vdev_raidz_generate_parity_row(rm, rr);
	}
}

static int
vdev_raidz_reconst_p_func(void *dbuf, void *sbuf, size_t size, void *private)
{
	(void) private;
	uint64_t *dst = dbuf;
	uint64_t *src = sbuf;
	int cnt = size / sizeof (src[0]);

	for (int i = 0; i < cnt; i++) {
		dst[i] ^= src[i];
	}

	return (0);
}

static int
vdev_raidz_reconst_q_pre_func(void *dbuf, void *sbuf, size_t size,
    void *private)
{
	(void) private;
	uint64_t *dst = dbuf;
	uint64_t *src = sbuf;
	uint64_t mask;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++, src++) {
		VDEV_RAIDZ_64MUL_2(*dst, mask);
		*dst ^= *src;
	}

	return (0);
}

static int
vdev_raidz_reconst_q_pre_tail_func(void *buf, size_t size, void *private)
{
	(void) private;
	uint64_t *dst = buf;
	uint64_t mask;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++) {
		/* same operation as vdev_raidz_reconst_q_pre_func() on dst */
		VDEV_RAIDZ_64MUL_2(*dst, mask);
	}

	return (0);
}

struct reconst_q_struct {
	uint64_t *q;
	int exp;
};

static int
vdev_raidz_reconst_q_post_func(void *buf, size_t size, void *private)
{
	struct reconst_q_struct *rq = private;
	uint64_t *dst = buf;
	int cnt = size / sizeof (dst[0]);

	for (int i = 0; i < cnt; i++, dst++, rq->q++) {
		int j;
		uint8_t *b;

		*dst ^= *rq->q;
		for (j = 0, b = (uint8_t *)dst; j < 8; j++, b++) {
			*b = vdev_raidz_exp2(*b, rq->exp);
		}
	}

	return (0);
}

struct reconst_pq_struct {
	uint8_t *p;
	uint8_t *q;
	uint8_t *pxy;
	uint8_t *qxy;
	int aexp;
	int bexp;
};

static int
vdev_raidz_reconst_pq_func(void *xbuf, void *ybuf, size_t size, void *private)
{
	struct reconst_pq_struct *rpq = private;
	uint8_t *xd = xbuf;
	uint8_t *yd = ybuf;

	for (int i = 0; i < size;
	    i++, rpq->p++, rpq->q++, rpq->pxy++, rpq->qxy++, xd++, yd++) {
		*xd = vdev_raidz_exp2(*rpq->p ^ *rpq->pxy, rpq->aexp) ^
		    vdev_raidz_exp2(*rpq->q ^ *rpq->qxy, rpq->bexp);
		*yd = *rpq->p ^ *rpq->pxy ^ *xd;
	}

	return (0);
}

static int
vdev_raidz_reconst_pq_tail_func(void *xbuf, size_t size, void *private)
{
	struct reconst_pq_struct *rpq = private;
	uint8_t *xd = xbuf;

	for (int i = 0; i < size;
	    i++, rpq->p++, rpq->q++, rpq->pxy++, rpq->qxy++, xd++) {
		/* same operation as vdev_raidz_reconst_pq_func() on xd */
		*xd = vdev_raidz_exp2(*rpq->p ^ *rpq->pxy, rpq->aexp) ^
		    vdev_raidz_exp2(*rpq->q ^ *rpq->qxy, rpq->bexp);
	}

	return (0);
}

static void
vdev_raidz_reconstruct_p(raidz_row_t *rr, int *tgts, int ntgts)
{
	int x = tgts[0];
	abd_t *dst, *src;

	if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT)
		zfs_dbgmsg("reconstruct_p(rm=%px x=%u)", rr, x);

	ASSERT3U(ntgts, ==, 1);
	ASSERT3U(x, >=, rr->rr_firstdatacol);
	ASSERT3U(x, <, rr->rr_cols);

	ASSERT3U(rr->rr_col[x].rc_size, <=, rr->rr_col[VDEV_RAIDZ_P].rc_size);

	src = rr->rr_col[VDEV_RAIDZ_P].rc_abd;
	dst = rr->rr_col[x].rc_abd;

	abd_copy_from_buf(dst, abd_to_buf(src), rr->rr_col[x].rc_size);

	for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		uint64_t size = MIN(rr->rr_col[x].rc_size,
		    rr->rr_col[c].rc_size);

		src = rr->rr_col[c].rc_abd;

		if (c == x)
			continue;

		(void) abd_iterate_func2(dst, src, 0, 0, size,
		    vdev_raidz_reconst_p_func, NULL);
	}
}

static void
vdev_raidz_reconstruct_q(raidz_row_t *rr, int *tgts, int ntgts)
{
	int x = tgts[0];
	int c, exp;
	abd_t *dst, *src;

	if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT)
		zfs_dbgmsg("reconstruct_q(rm=%px x=%u)", rr, x);

	ASSERT(ntgts == 1);

	ASSERT(rr->rr_col[x].rc_size <= rr->rr_col[VDEV_RAIDZ_Q].rc_size);

	for (c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		uint64_t size = (c == x) ? 0 : MIN(rr->rr_col[x].rc_size,
		    rr->rr_col[c].rc_size);

		src = rr->rr_col[c].rc_abd;
		dst = rr->rr_col[x].rc_abd;

		if (c == rr->rr_firstdatacol) {
			abd_copy(dst, src, size);
			if (rr->rr_col[x].rc_size > size) {
				abd_zero_off(dst, size,
				    rr->rr_col[x].rc_size - size);
			}
		} else {
			ASSERT3U(size, <=, rr->rr_col[x].rc_size);
			(void) abd_iterate_func2(dst, src, 0, 0, size,
			    vdev_raidz_reconst_q_pre_func, NULL);
			(void) abd_iterate_func(dst,
			    size, rr->rr_col[x].rc_size - size,
			    vdev_raidz_reconst_q_pre_tail_func, NULL);
		}
	}

	src = rr->rr_col[VDEV_RAIDZ_Q].rc_abd;
	dst = rr->rr_col[x].rc_abd;
	exp = 255 - (rr->rr_cols - 1 - x);

	struct reconst_q_struct rq = { abd_to_buf(src), exp };
	(void) abd_iterate_func(dst, 0, rr->rr_col[x].rc_size,
	    vdev_raidz_reconst_q_post_func, &rq);
}

static void
vdev_raidz_reconstruct_pq(raidz_row_t *rr, int *tgts, int ntgts)
{
	uint8_t *p, *q, *pxy, *qxy, tmp, a, b, aexp, bexp;
	abd_t *pdata, *qdata;
	uint64_t xsize, ysize;
	int x = tgts[0];
	int y = tgts[1];
	abd_t *xd, *yd;

	if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT)
		zfs_dbgmsg("reconstruct_pq(rm=%px x=%u y=%u)", rr, x, y);

	ASSERT(ntgts == 2);
	ASSERT(x < y);
	ASSERT(x >= rr->rr_firstdatacol);
	ASSERT(y < rr->rr_cols);

	ASSERT(rr->rr_col[x].rc_size >= rr->rr_col[y].rc_size);

	/*
	 * Move the parity data aside -- we're going to compute parity as
	 * though columns x and y were full of zeros -- Pxy and Qxy. We want to
	 * reuse the parity generation mechanism without trashing the actual
	 * parity so we make those columns appear to be full of zeros by
	 * setting their lengths to zero.
	 */
	pdata = rr->rr_col[VDEV_RAIDZ_P].rc_abd;
	qdata = rr->rr_col[VDEV_RAIDZ_Q].rc_abd;
	xsize = rr->rr_col[x].rc_size;
	ysize = rr->rr_col[y].rc_size;

	rr->rr_col[VDEV_RAIDZ_P].rc_abd =
	    abd_alloc_linear(rr->rr_col[VDEV_RAIDZ_P].rc_size, B_TRUE);
	rr->rr_col[VDEV_RAIDZ_Q].rc_abd =
	    abd_alloc_linear(rr->rr_col[VDEV_RAIDZ_Q].rc_size, B_TRUE);
	rr->rr_col[x].rc_size = 0;
	rr->rr_col[y].rc_size = 0;

	vdev_raidz_generate_parity_pq(rr);

	rr->rr_col[x].rc_size = xsize;
	rr->rr_col[y].rc_size = ysize;

	p = abd_to_buf(pdata);
	q = abd_to_buf(qdata);
	pxy = abd_to_buf(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	qxy = abd_to_buf(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);
	xd = rr->rr_col[x].rc_abd;
	yd = rr->rr_col[y].rc_abd;

	/*
	 * We now have:
	 *	Pxy = P + D_x + D_y
	 *	Qxy = Q + 2^(ndevs - 1 - x) * D_x + 2^(ndevs - 1 - y) * D_y
	 *
	 * We can then solve for D_x:
	 *	D_x = A * (P + Pxy) + B * (Q + Qxy)
	 * where
	 *	A = 2^(x - y) * (2^(x - y) + 1)^-1
	 *	B = 2^(ndevs - 1 - x) * (2^(x - y) + 1)^-1
	 *
	 * With D_x in hand, we can easily solve for D_y:
	 *	D_y = P + Pxy + D_x
	 */

	a = vdev_raidz_pow2[255 + x - y];
	b = vdev_raidz_pow2[255 - (rr->rr_cols - 1 - x)];
	tmp = 255 - vdev_raidz_log2[a ^ 1];

	aexp = vdev_raidz_log2[vdev_raidz_exp2(a, tmp)];
	bexp = vdev_raidz_log2[vdev_raidz_exp2(b, tmp)];

	ASSERT3U(xsize, >=, ysize);
	struct reconst_pq_struct rpq = { p, q, pxy, qxy, aexp, bexp };

	(void) abd_iterate_func2(xd, yd, 0, 0, ysize,
	    vdev_raidz_reconst_pq_func, &rpq);
	(void) abd_iterate_func(xd, ysize, xsize - ysize,
	    vdev_raidz_reconst_pq_tail_func, &rpq);

	abd_free(rr->rr_col[VDEV_RAIDZ_P].rc_abd);
	abd_free(rr->rr_col[VDEV_RAIDZ_Q].rc_abd);

	/*
	 * Restore the saved parity data.
	 */
	rr->rr_col[VDEV_RAIDZ_P].rc_abd = pdata;
	rr->rr_col[VDEV_RAIDZ_Q].rc_abd = qdata;
}

/*
 * In the general case of reconstruction, we must solve the system of linear
 * equations defined by the coefficients used to generate parity as well as
 * the contents of the data and parity disks. This can be expressed with
 * vectors for the original data (D) and the actual data (d) and parity (p)
 * and a matrix composed of the identity matrix (I) and a dispersal matrix (V):
 *
 *            __   __                     __     __
 *            |     |         __     __   |  p_0  |
 *            |  V  |         |  D_0  |   | p_m-1 |
 *            |     |    x    |   :   | = |  d_0  |
 *            |  I  |         | D_n-1 |   |   :   |
 *            |     |         ~~     ~~   | d_n-1 |
 *            ~~   ~~                     ~~     ~~
 *
 * I is simply a square identity matrix of size n, and V is a vandermonde
 * matrix defined by the coefficients we chose for the various parity columns
 * (1, 2, 4). Note that these values were chosen both for simplicity, speedy
 * computation as well as linear separability.
 *
 *      __               __               __     __
 *      |   1   ..  1 1 1 |               |  p_0  |
 *      | 2^n-1 ..  4 2 1 |   __     __   |   :   |
 *      | 4^n-1 .. 16 4 1 |   |  D_0  |   | p_m-1 |
 *      |   1   ..  0 0 0 |   |  D_1  |   |  d_0  |
 *      |   0   ..  0 0 0 | x |  D_2  | = |  d_1  |
 *      |   :       : : : |   |   :   |   |  d_2  |
 *      |   0   ..  1 0 0 |   | D_n-1 |   |   :   |
 *      |   0   ..  0 1 0 |   ~~     ~~   |   :   |
 *      |   0   ..  0 0 1 |               | d_n-1 |
 *      ~~               ~~               ~~     ~~
 *
 * Note that I, V, d, and p are known. To compute D, we must invert the
 * matrix and use the known data and parity values to reconstruct the unknown
 * data values. We begin by removing the rows in V|I and d|p that correspond
 * to failed or missing columns; we then make V|I square (n x n) and d|p
 * sized n by removing rows corresponding to unused parity from the bottom up
 * to generate (V|I)' and (d|p)'. We can then generate the inverse of (V|I)'
 * using Gauss-Jordan elimination. In the example below we use m=3 parity
 * columns, n=8 data columns, with errors in d_1, d_2, and p_1:
 *           __                               __
 *           |  1   1   1   1   1   1   1   1  |
 *           | 128  64  32  16  8   4   2   1  | <-----+-+-- missing disks
 *           |  19 205 116  29  64  16  4   1  |      / /
 *           |  1   0   0   0   0   0   0   0  |     / /
 *           |  0   1   0   0   0   0   0   0  | <--' /
 *  (V|I)  = |  0   0   1   0   0   0   0   0  | <---'
 *           |  0   0   0   1   0   0   0   0  |
 *           |  0   0   0   0   1   0   0   0  |
 *           |  0   0   0   0   0   1   0   0  |
 *           |  0   0   0   0   0   0   1   0  |
 *           |  0   0   0   0   0   0   0   1  |
 *           ~~                               ~~
 *           __                               __
 *           |  1   1   1   1   1   1   1   1  |
 *           | 128  64  32  16  8   4   2   1  |
 *           |  19 205 116  29  64  16  4   1  |
 *           |  1   0   0   0   0   0   0   0  |
 *           |  0   1   0   0   0   0   0   0  |
 *  (V|I)' = |  0   0   1   0   0   0   0   0  |
 *           |  0   0   0   1   0   0   0   0  |
 *           |  0   0   0   0   1   0   0   0  |
 *           |  0   0   0   0   0   1   0   0  |
 *           |  0   0   0   0   0   0   1   0  |
 *           |  0   0   0   0   0   0   0   1  |
 *           ~~                               ~~
 *
 * Here we employ Gauss-Jordan elimination to find the inverse of (V|I)'. We
 * have carefully chosen the seed values 1, 2, and 4 to ensure that this
 * matrix is not singular.
 * __                                                                 __
 * |  1   1   1   1   1   1   1   1     1   0   0   0   0   0   0   0  |
 * |  19 205 116  29  64  16  4   1     0   1   0   0   0   0   0   0  |
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  1   1   1   1   1   1   1   1     1   0   0   0   0   0   0   0  |
 * |  19 205 116  29  64  16  4   1     0   1   0   0   0   0   0   0  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0  205 116  0   0   0   0   0     0   1   19  29  64  16  4   1  |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0   0  185  0   0   0   0   0    205  1  222 208 141 221 201 204 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   1   0   0   0   0   0     1   0   1   1   1   1   1   1  |
 * |  0   0   1   0   0   0   0   0    166 100  4   40 158 168 216 209 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 * __                                                                 __
 * |  1   0   0   0   0   0   0   0     0   0   1   0   0   0   0   0  |
 * |  0   1   0   0   0   0   0   0    167 100  5   41 159 169 217 208 |
 * |  0   0   1   0   0   0   0   0    166 100  4   40 158 168 216 209 |
 * |  0   0   0   1   0   0   0   0     0   0   0   1   0   0   0   0  |
 * |  0   0   0   0   1   0   0   0     0   0   0   0   1   0   0   0  |
 * |  0   0   0   0   0   1   0   0     0   0   0   0   0   1   0   0  |
 * |  0   0   0   0   0   0   1   0     0   0   0   0   0   0   1   0  |
 * |  0   0   0   0   0   0   0   1     0   0   0   0   0   0   0   1  |
 * ~~                                                                 ~~
 *                   __                               __
 *                   |  0   0   1   0   0   0   0   0  |
 *                   | 167 100  5   41 159 169 217 208 |
 *                   | 166 100  4   40 158 168 216 209 |
 *       (V|I)'^-1 = |  0   0   0   1   0   0   0   0  |
 *                   |  0   0   0   0   1   0   0   0  |
 *                   |  0   0   0   0   0   1   0   0  |
 *                   |  0   0   0   0   0   0   1   0  |
 *                   |  0   0   0   0   0   0   0   1  |
 *                   ~~                               ~~
 *
 * We can then simply compute D = (V|I)'^-1 x (d|p)' to discover the values
 * of the missing data.
 *
 * As is apparent from the example above, the only non-trivial rows in the
 * inverse matrix correspond to the data disks that we're trying to
 * reconstruct. Indeed, those are the only rows we need as the others would
 * only be useful for reconstructing data known or assumed to be valid. For
 * that reason, we only build the coefficients in the rows that correspond to
 * targeted columns.
 */

static void
vdev_raidz_matrix_init(raidz_row_t *rr, int n, int nmap, int *map,
    uint8_t **rows)
{
	int i, j;
	int pow;

	ASSERT(n == rr->rr_cols - rr->rr_firstdatacol);

	/*
	 * Fill in the missing rows of interest.
	 */
	for (i = 0; i < nmap; i++) {
		ASSERT3S(0, <=, map[i]);
		ASSERT3S(map[i], <=, 2);

		pow = map[i] * n;
		if (pow > 255)
			pow -= 255;
		ASSERT(pow <= 255);

		for (j = 0; j < n; j++) {
			pow -= map[i];
			if (pow < 0)
				pow += 255;
			rows[i][j] = vdev_raidz_pow2[pow];
		}
	}
}

static void
vdev_raidz_matrix_invert(raidz_row_t *rr, int n, int nmissing, int *missing,
    uint8_t **rows, uint8_t **invrows, const uint8_t *used)
{
	int i, j, ii, jj;
	uint8_t log;

	/*
	 * Assert that the first nmissing entries from the array of used
	 * columns correspond to parity columns and that subsequent entries
	 * correspond to data columns.
	 */
	for (i = 0; i < nmissing; i++) {
		ASSERT3S(used[i], <, rr->rr_firstdatacol);
	}
	for (; i < n; i++) {
		ASSERT3S(used[i], >=, rr->rr_firstdatacol);
	}

	/*
	 * First initialize the storage where we'll compute the inverse rows.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			invrows[i][j] = (i == j) ? 1 : 0;
		}
	}

	/*
	 * Subtract all trivial rows from the rows of consequence.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = nmissing; j < n; j++) {
			ASSERT3U(used[j], >=, rr->rr_firstdatacol);
			jj = used[j] - rr->rr_firstdatacol;
			ASSERT3S(jj, <, n);
			invrows[i][j] = rows[i][jj];
			rows[i][jj] = 0;
		}
	}

	/*
	 * For each of the rows of interest, we must normalize it and subtract
	 * a multiple of it from the other rows.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < missing[i]; j++) {
			ASSERT0(rows[i][j]);
		}
		ASSERT3U(rows[i][missing[i]], !=, 0);

		/*
		 * Compute the inverse of the first element and multiply each
		 * element in the row by that value.
		 */
		log = 255 - vdev_raidz_log2[rows[i][missing[i]]];

		for (j = 0; j < n; j++) {
			rows[i][j] = vdev_raidz_exp2(rows[i][j], log);
			invrows[i][j] = vdev_raidz_exp2(invrows[i][j], log);
		}

		for (ii = 0; ii < nmissing; ii++) {
			if (i == ii)
				continue;

			ASSERT3U(rows[ii][missing[i]], !=, 0);

			log = vdev_raidz_log2[rows[ii][missing[i]]];

			for (j = 0; j < n; j++) {
				rows[ii][j] ^=
				    vdev_raidz_exp2(rows[i][j], log);
				invrows[ii][j] ^=
				    vdev_raidz_exp2(invrows[i][j], log);
			}
		}
	}

	/*
	 * Verify that the data that is left in the rows are properly part of
	 * an identity matrix.
	 */
	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			if (j == missing[i]) {
				ASSERT3U(rows[i][j], ==, 1);
			} else {
				ASSERT0(rows[i][j]);
			}
		}
	}
}

static void
vdev_raidz_matrix_reconstruct(raidz_row_t *rr, int n, int nmissing,
    int *missing, uint8_t **invrows, const uint8_t *used)
{
	int i, j, x, cc, c;
	uint8_t *src;
	uint64_t ccount;
	uint8_t *dst[VDEV_RAIDZ_MAXPARITY] = { NULL };
	uint64_t dcount[VDEV_RAIDZ_MAXPARITY] = { 0 };
	uint8_t log = 0;
	uint8_t val;
	int ll;
	uint8_t *invlog[VDEV_RAIDZ_MAXPARITY];
	uint8_t *p, *pp;
	size_t psize;

	psize = sizeof (invlog[0][0]) * n * nmissing;
	p = kmem_alloc(psize, KM_SLEEP);

	for (pp = p, i = 0; i < nmissing; i++) {
		invlog[i] = pp;
		pp += n;
	}

	for (i = 0; i < nmissing; i++) {
		for (j = 0; j < n; j++) {
			ASSERT3U(invrows[i][j], !=, 0);
			invlog[i][j] = vdev_raidz_log2[invrows[i][j]];
		}
	}

	for (i = 0; i < n; i++) {
		c = used[i];
		ASSERT3U(c, <, rr->rr_cols);

		ccount = rr->rr_col[c].rc_size;
		ASSERT(ccount >= rr->rr_col[missing[0]].rc_size || i > 0);
		if (ccount == 0)
			continue;
		src = abd_to_buf(rr->rr_col[c].rc_abd);
		for (j = 0; j < nmissing; j++) {
			cc = missing[j] + rr->rr_firstdatacol;
			ASSERT3U(cc, >=, rr->rr_firstdatacol);
			ASSERT3U(cc, <, rr->rr_cols);
			ASSERT3U(cc, !=, c);

			dcount[j] = rr->rr_col[cc].rc_size;
			if (dcount[j] != 0)
				dst[j] = abd_to_buf(rr->rr_col[cc].rc_abd);
		}

		for (x = 0; x < ccount; x++, src++) {
			if (*src != 0)
				log = vdev_raidz_log2[*src];

			for (cc = 0; cc < nmissing; cc++) {
				if (x >= dcount[cc])
					continue;

				if (*src == 0) {
					val = 0;
				} else {
					if ((ll = log + invlog[cc][i]) >= 255)
						ll -= 255;
					val = vdev_raidz_pow2[ll];
				}

				if (i == 0)
					dst[cc][x] = val;
				else
					dst[cc][x] ^= val;
			}
		}
	}

	kmem_free(p, psize);
}

static void
vdev_raidz_reconstruct_general(raidz_row_t *rr, int *tgts, int ntgts)
{
	int i, c, t, tt;
	unsigned int n;
	unsigned int nmissing_rows;
	int missing_rows[VDEV_RAIDZ_MAXPARITY];
	int parity_map[VDEV_RAIDZ_MAXPARITY];
	uint8_t *p, *pp;
	size_t psize;
	uint8_t *rows[VDEV_RAIDZ_MAXPARITY];
	uint8_t *invrows[VDEV_RAIDZ_MAXPARITY];
	uint8_t *used;

	abd_t **bufs = NULL;

	if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT)
		zfs_dbgmsg("reconstruct_general(rm=%px ntgts=%u)", rr, ntgts);
	/*
	 * Matrix reconstruction can't use scatter ABDs yet, so we allocate
	 * temporary linear ABDs if any non-linear ABDs are found.
	 */
	for (i = rr->rr_firstdatacol; i < rr->rr_cols; i++) {
		ASSERT(rr->rr_col[i].rc_abd != NULL);
		if (!abd_is_linear(rr->rr_col[i].rc_abd)) {
			bufs = kmem_alloc(rr->rr_cols * sizeof (abd_t *),
			    KM_PUSHPAGE);

			for (c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
				raidz_col_t *col = &rr->rr_col[c];

				bufs[c] = col->rc_abd;
				if (bufs[c] != NULL) {
					col->rc_abd = abd_alloc_linear(
					    col->rc_size, B_TRUE);
					abd_copy(col->rc_abd, bufs[c],
					    col->rc_size);
				}
			}

			break;
		}
	}

	n = rr->rr_cols - rr->rr_firstdatacol;

	/*
	 * Figure out which data columns are missing.
	 */
	nmissing_rows = 0;
	for (t = 0; t < ntgts; t++) {
		if (tgts[t] >= rr->rr_firstdatacol) {
			missing_rows[nmissing_rows++] =
			    tgts[t] - rr->rr_firstdatacol;
		}
	}

	/*
	 * Figure out which parity columns to use to help generate the missing
	 * data columns.
	 */
	for (tt = 0, c = 0, i = 0; i < nmissing_rows; c++) {
		ASSERT(tt < ntgts);
		ASSERT(c < rr->rr_firstdatacol);

		/*
		 * Skip any targeted parity columns.
		 */
		if (c == tgts[tt]) {
			tt++;
			continue;
		}

		parity_map[i] = c;
		i++;
	}

	psize = (sizeof (rows[0][0]) + sizeof (invrows[0][0])) *
	    nmissing_rows * n + sizeof (used[0]) * n;
	p = kmem_alloc(psize, KM_SLEEP);

	for (pp = p, i = 0; i < nmissing_rows; i++) {
		rows[i] = pp;
		pp += n;
		invrows[i] = pp;
		pp += n;
	}
	used = pp;

	for (i = 0; i < nmissing_rows; i++) {
		used[i] = parity_map[i];
	}

	for (tt = 0, c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
		if (tt < nmissing_rows &&
		    c == missing_rows[tt] + rr->rr_firstdatacol) {
			tt++;
			continue;
		}

		ASSERT3S(i, <, n);
		used[i] = c;
		i++;
	}

	/*
	 * Initialize the interesting rows of the matrix.
	 */
	vdev_raidz_matrix_init(rr, n, nmissing_rows, parity_map, rows);

	/*
	 * Invert the matrix.
	 */
	vdev_raidz_matrix_invert(rr, n, nmissing_rows, missing_rows, rows,
	    invrows, used);

	/*
	 * Reconstruct the missing data using the generated matrix.
	 */
	vdev_raidz_matrix_reconstruct(rr, n, nmissing_rows, missing_rows,
	    invrows, used);

	kmem_free(p, psize);

	/*
	 * copy back from temporary linear abds and free them
	 */
	if (bufs) {
		for (c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
			raidz_col_t *col = &rr->rr_col[c];

			if (bufs[c] != NULL) {
				abd_copy(bufs[c], col->rc_abd, col->rc_size);
				abd_free(col->rc_abd);
			}
			col->rc_abd = bufs[c];
		}
		kmem_free(bufs, rr->rr_cols * sizeof (abd_t *));
	}
}

static void
vdev_raidz_reconstruct_row(raidz_map_t *rm, raidz_row_t *rr,
    const int *t, int nt)
{
	int tgts[VDEV_RAIDZ_MAXPARITY], *dt;
	int ntgts;
	int i, c, ret;
	int nbadparity, nbaddata;
	int parity_valid[VDEV_RAIDZ_MAXPARITY];

	if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT) {
		zfs_dbgmsg("reconstruct(rm=%px nt=%u cols=%u md=%u mp=%u)",
		    rr, nt, (int)rr->rr_cols, (int)rr->rr_missingdata,
		    (int)rr->rr_missingparity);
	}

	nbadparity = rr->rr_firstdatacol;
	nbaddata = rr->rr_cols - nbadparity;
	ntgts = 0;
	for (i = 0, c = 0; c < rr->rr_cols; c++) {
		if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT) {
			zfs_dbgmsg("reconstruct(rm=%px col=%u devid=%u "
			    "offset=%llx error=%u)",
			    rr, c, (int)rr->rr_col[c].rc_devidx,
			    (long long)rr->rr_col[c].rc_offset,
			    (int)rr->rr_col[c].rc_error);
		}
		if (c < rr->rr_firstdatacol)
			parity_valid[c] = B_FALSE;

		if (i < nt && c == t[i]) {
			tgts[ntgts++] = c;
			i++;
		} else if (rr->rr_col[c].rc_error != 0) {
			tgts[ntgts++] = c;
		} else if (c >= rr->rr_firstdatacol) {
			nbaddata--;
		} else {
			parity_valid[c] = B_TRUE;
			nbadparity--;
		}
	}

	ASSERT(ntgts >= nt);
	ASSERT(nbaddata >= 0);
	ASSERT(nbaddata + nbadparity == ntgts);

	dt = &tgts[nbadparity];

	/* Reconstruct using the new math implementation */
	ret = vdev_raidz_math_reconstruct(rm, rr, parity_valid, dt, nbaddata);
	if (ret != RAIDZ_ORIGINAL_IMPL)
		return;

	/*
	 * See if we can use any of our optimized reconstruction routines.
	 */
	switch (nbaddata) {
	case 1:
		if (parity_valid[VDEV_RAIDZ_P]) {
			vdev_raidz_reconstruct_p(rr, dt, 1);
			return;
		}

		ASSERT(rr->rr_firstdatacol > 1);

		if (parity_valid[VDEV_RAIDZ_Q]) {
			vdev_raidz_reconstruct_q(rr, dt, 1);
			return;
		}

		ASSERT(rr->rr_firstdatacol > 2);
		break;

	case 2:
		ASSERT(rr->rr_firstdatacol > 1);

		if (parity_valid[VDEV_RAIDZ_P] &&
		    parity_valid[VDEV_RAIDZ_Q]) {
			vdev_raidz_reconstruct_pq(rr, dt, 2);
			return;
		}

		ASSERT(rr->rr_firstdatacol > 2);

		break;
	}

	vdev_raidz_reconstruct_general(rr, tgts, ntgts);
}

static int
vdev_raidz_open(vdev_t *vd, uint64_t *asize, uint64_t *max_asize,
    uint64_t *logical_ashift, uint64_t *physical_ashift)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	uint64_t nparity = vdrz->vd_nparity;
	int c;
	int lasterror = 0;
	int numerrors = 0;

	ASSERT(nparity > 0);

	if (nparity > VDEV_RAIDZ_MAXPARITY ||
	    vd->vdev_children < nparity + 1) {
		vd->vdev_stat.vs_aux = VDEV_AUX_BAD_LABEL;
		return (SET_ERROR(EINVAL));
	}

	vdev_open_children(vd);

	for (c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0) {
			lasterror = cvd->vdev_open_error;
			numerrors++;
			continue;
		}

		*asize = MIN(*asize - 1, cvd->vdev_asize - 1) + 1;
		*max_asize = MIN(*max_asize - 1, cvd->vdev_max_asize - 1) + 1;
		*logical_ashift = MAX(*logical_ashift, cvd->vdev_ashift);
	}
	for (c = 0; c < vd->vdev_children; c++) {
		vdev_t *cvd = vd->vdev_child[c];

		if (cvd->vdev_open_error != 0)
			continue;
		*physical_ashift = vdev_best_ashift(*logical_ashift,
		    *physical_ashift, cvd->vdev_physical_ashift);
	}

	if (vd->vdev_rz_expanding) {
		*asize *= vd->vdev_children - 1;
		*max_asize *= vd->vdev_children - 1;

		vd->vdev_min_asize = *asize;
	} else {
		*asize *= vd->vdev_children;
		*max_asize *= vd->vdev_children;
	}

	if (numerrors > nparity) {
		vd->vdev_stat.vs_aux = VDEV_AUX_NO_REPLICAS;
		return (lasterror);
	}

	return (0);
}

static void
vdev_raidz_close(vdev_t *vd)
{
	for (int c = 0; c < vd->vdev_children; c++) {
		if (vd->vdev_child[c] != NULL)
			vdev_close(vd->vdev_child[c]);
	}
}

/*
 * Return the logical width to use, given the txg in which the allocation
 * happened.  Note that BP_GET_BIRTH() is usually the txg in which the
 * BP was allocated.  Remapped BP's (that were relocated due to device
 * removal, see remap_blkptr_cb()), will have a more recent physical birth
 * which reflects when the BP was relocated, but we can ignore these because
 * they can't be on RAIDZ (device removal doesn't support RAIDZ).
 */
static uint64_t
vdev_raidz_get_logical_width(vdev_raidz_t *vdrz, uint64_t txg)
{
	reflow_node_t lookup = {
		.re_txg = txg,
	};
	avl_index_t where;

	uint64_t width;
	mutex_enter(&vdrz->vd_expand_lock);
	reflow_node_t *re = avl_find(&vdrz->vd_expand_txgs, &lookup, &where);
	if (re != NULL) {
		width = re->re_logical_width;
	} else {
		re = avl_nearest(&vdrz->vd_expand_txgs, where, AVL_BEFORE);
		if (re != NULL)
			width = re->re_logical_width;
		else
			width = vdrz->vd_original_width;
	}
	mutex_exit(&vdrz->vd_expand_lock);
	return (width);
}

/*
 * Note: If the RAIDZ vdev has been expanded, older BP's may have allocated
 * more space due to the lower data-to-parity ratio.  In this case it's
 * important to pass in the correct txg.  Note that vdev_gang_header_asize()
 * relies on a constant asize for psize=SPA_GANGBLOCKSIZE=SPA_MINBLOCKSIZE,
 * regardless of txg.  This is assured because for a single data sector, we
 * allocate P+1 sectors regardless of width ("cols", which is at least P+1).
 */
static uint64_t
vdev_raidz_asize(vdev_t *vd, uint64_t psize, uint64_t txg)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	uint64_t asize;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	uint64_t cols = vdrz->vd_original_width;
	uint64_t nparity = vdrz->vd_nparity;

	cols = vdev_raidz_get_logical_width(vdrz, txg);

	asize = ((psize - 1) >> ashift) + 1;
	asize += nparity * ((asize + cols - nparity - 1) / (cols - nparity));
	asize = roundup(asize, nparity + 1) << ashift;

#ifdef ZFS_DEBUG
	uint64_t asize_new = ((psize - 1) >> ashift) + 1;
	uint64_t ncols_new = vdrz->vd_physical_width;
	asize_new += nparity * ((asize_new + ncols_new - nparity - 1) /
	    (ncols_new - nparity));
	asize_new = roundup(asize_new, nparity + 1) << ashift;
	VERIFY3U(asize_new, <=, asize);
#endif

	return (asize);
}

/*
 * The allocatable space for a raidz vdev is N * sizeof(smallest child)
 * so each child must provide at least 1/Nth of its asize.
 */
static uint64_t
vdev_raidz_min_asize(vdev_t *vd)
{
	return ((vd->vdev_min_asize + vd->vdev_children - 1) /
	    vd->vdev_children);
}

void
vdev_raidz_child_done(zio_t *zio)
{
	raidz_col_t *rc = zio->io_private;

	ASSERT3P(rc->rc_abd, !=, NULL);
	rc->rc_error = zio->io_error;
	rc->rc_tried = 1;
	rc->rc_skipped = 0;
}

static void
vdev_raidz_shadow_child_done(zio_t *zio)
{
	raidz_col_t *rc = zio->io_private;

	rc->rc_shadow_error = zio->io_error;
}

static void
vdev_raidz_io_verify(zio_t *zio, raidz_map_t *rm, raidz_row_t *rr, int col)
{
	(void) rm;
#ifdef ZFS_DEBUG
	zfs_range_seg64_t logical_rs, physical_rs, remain_rs;
	logical_rs.rs_start = rr->rr_offset;
	logical_rs.rs_end = logical_rs.rs_start +
	    vdev_raidz_asize(zio->io_vd, rr->rr_size,
	    BP_GET_BIRTH(zio->io_bp));

	raidz_col_t *rc = &rr->rr_col[col];
	vdev_t *cvd = zio->io_vd->vdev_child[rc->rc_devidx];

	vdev_xlate(cvd, &logical_rs, &physical_rs, &remain_rs);
	ASSERT(vdev_xlate_is_empty(&remain_rs));
	if (vdev_xlate_is_empty(&physical_rs)) {
		/*
		 * If we are in the middle of expansion, the
		 * physical->logical mapping is changing so vdev_xlate()
		 * can't give us a reliable answer.
		 */
		return;
	}
	ASSERT3U(rc->rc_offset, ==, physical_rs.rs_start);
	ASSERT3U(rc->rc_offset, <, physical_rs.rs_end);
	/*
	 * It would be nice to assert that rs_end is equal
	 * to rc_offset + rc_size but there might be an
	 * optional I/O at the end that is not accounted in
	 * rc_size.
	 */
	if (physical_rs.rs_end > rc->rc_offset + rc->rc_size) {
		ASSERT3U(physical_rs.rs_end, ==, rc->rc_offset +
		    rc->rc_size + (1 << zio->io_vd->vdev_top->vdev_ashift));
	} else {
		ASSERT3U(physical_rs.rs_end, ==, rc->rc_offset + rc->rc_size);
	}
#endif
}

static void
vdev_raidz_io_start_write(zio_t *zio, raidz_row_t *rr)
{
	vdev_t *vd = zio->io_vd;
	raidz_map_t *rm = zio->io_vsd;

	vdev_raidz_generate_parity_row(rm, rr);

	for (int c = 0; c < rr->rr_scols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

		/* Verify physical to logical translation */
		vdev_raidz_io_verify(zio, rm, rr, c);

		if (rc->rc_size == 0)
			continue;

		ASSERT3U(rc->rc_offset + rc->rc_size, <,
		    cvd->vdev_psize - VDEV_LABEL_END_SIZE);

		ASSERT3P(rc->rc_abd, !=, NULL);
		zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
		    rc->rc_offset, rc->rc_abd,
		    abd_get_size(rc->rc_abd), zio->io_type,
		    zio->io_priority, 0, vdev_raidz_child_done, rc));

		if (rc->rc_shadow_devidx != INT_MAX) {
			vdev_t *cvd2 = vd->vdev_child[rc->rc_shadow_devidx];

			ASSERT3U(
			    rc->rc_shadow_offset + abd_get_size(rc->rc_abd), <,
			    cvd2->vdev_psize - VDEV_LABEL_END_SIZE);

			zio_nowait(zio_vdev_child_io(zio, NULL, cvd2,
			    rc->rc_shadow_offset, rc->rc_abd,
			    abd_get_size(rc->rc_abd),
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_shadow_child_done, rc));
		}
	}
}

/*
 * Generate optional I/Os for skip sectors to improve aggregation contiguity.
 * This only works for vdev_raidz_map_alloc() (not _expanded()).
 */
static void
raidz_start_skip_writes(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	raidz_map_t *rm = zio->io_vsd;
	ASSERT3U(rm->rm_nrows, ==, 1);
	raidz_row_t *rr = rm->rm_row[0];
	for (int c = 0; c < rr->rr_scols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];
		if (rc->rc_size != 0)
			continue;
		ASSERT3P(rc->rc_abd, ==, NULL);

		ASSERT3U(rc->rc_offset, <,
		    cvd->vdev_psize - VDEV_LABEL_END_SIZE);

		zio_nowait(zio_vdev_child_io(zio, NULL, cvd, rc->rc_offset,
		    NULL, 1ULL << ashift, zio->io_type, zio->io_priority,
		    ZIO_FLAG_NODATA | ZIO_FLAG_OPTIONAL, NULL, NULL));
	}
}

static void
vdev_raidz_io_start_read_row(zio_t *zio, raidz_row_t *rr, boolean_t forceparity)
{
	vdev_t *vd = zio->io_vd;

	/*
	 * Iterate over the columns in reverse order so that we hit the parity
	 * last -- any errors along the way will force us to read the parity.
	 */
	for (int c = rr->rr_cols - 1; c >= 0; c--) {
		raidz_col_t *rc = &rr->rr_col[c];
		if (rc->rc_size == 0)
			continue;
		vdev_t *cvd = vd->vdev_child[rc->rc_devidx];
		if (!vdev_readable(cvd)) {
			if (c >= rr->rr_firstdatacol)
				rr->rr_missingdata++;
			else
				rr->rr_missingparity++;
			rc->rc_error = SET_ERROR(ENXIO);
			rc->rc_tried = 1;	/* don't even try */
			rc->rc_skipped = 1;
			continue;
		}
		if (vdev_dtl_contains(cvd, DTL_MISSING, zio->io_txg, 1)) {
			if (c >= rr->rr_firstdatacol)
				rr->rr_missingdata++;
			else
				rr->rr_missingparity++;
			rc->rc_error = SET_ERROR(ESTALE);
			rc->rc_skipped = 1;
			continue;
		}
		if (forceparity ||
		    c >= rr->rr_firstdatacol || rr->rr_missingdata > 0 ||
		    (zio->io_flags & (ZIO_FLAG_SCRUB | ZIO_FLAG_RESILVER))) {
			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    zio->io_type, zio->io_priority, 0,
			    vdev_raidz_child_done, rc));
		}
	}
}

static void
vdev_raidz_io_start_read_phys_cols(zio_t *zio, raidz_map_t *rm)
{
	vdev_t *vd = zio->io_vd;

	for (int i = 0; i < rm->rm_nphys_cols; i++) {
		raidz_col_t *prc = &rm->rm_phys_col[i];
		if (prc->rc_size == 0)
			continue;

		ASSERT3U(prc->rc_devidx, ==, i);
		vdev_t *cvd = vd->vdev_child[i];
		if (!vdev_readable(cvd)) {
			prc->rc_error = SET_ERROR(ENXIO);
			prc->rc_tried = 1;	/* don't even try */
			prc->rc_skipped = 1;
			continue;
		}
		if (vdev_dtl_contains(cvd, DTL_MISSING, zio->io_txg, 1)) {
			prc->rc_error = SET_ERROR(ESTALE);
			prc->rc_skipped = 1;
			continue;
		}
		zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
		    prc->rc_offset, prc->rc_abd, prc->rc_size,
		    zio->io_type, zio->io_priority, 0,
		    vdev_raidz_child_done, prc));
	}
}

static void
vdev_raidz_io_start_read(zio_t *zio, raidz_map_t *rm)
{
	/*
	 * If there are multiple rows, we will be hitting
	 * all disks, so go ahead and read the parity so
	 * that we are reading in decent size chunks.
	 */
	boolean_t forceparity = rm->rm_nrows > 1;

	if (rm->rm_phys_col) {
		vdev_raidz_io_start_read_phys_cols(zio, rm);
	} else {
		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];
			vdev_raidz_io_start_read_row(zio, rr, forceparity);
		}
	}
}

/*
 * Start an IO operation on a RAIDZ VDev
 *
 * Outline:
 * - For write operations:
 *   1. Generate the parity data
 *   2. Create child zio write operations to each column's vdev, for both
 *      data and parity.
 *   3. If the column skips any sectors for padding, create optional dummy
 *      write zio children for those areas to improve aggregation continuity.
 * - For read operations:
 *   1. Create child zio read operations to each data column's vdev to read
 *      the range of data required for zio.
 *   2. If this is a scrub or resilver operation, or if any of the data
 *      vdevs have had errors, then create zio read operations to the parity
 *      columns' VDevs as well.
 */
static void
vdev_raidz_io_start(zio_t *zio)
{
	vdev_t *vd = zio->io_vd;
	vdev_t *tvd = vd->vdev_top;
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	raidz_map_t *rm;

	uint64_t logical_width = vdev_raidz_get_logical_width(vdrz,
	    BP_GET_BIRTH(zio->io_bp));
	if (logical_width != vdrz->vd_physical_width) {
		zfs_locked_range_t *lr = NULL;
		uint64_t synced_offset = UINT64_MAX;
		uint64_t next_offset = UINT64_MAX;
		boolean_t use_scratch = B_FALSE;
		/*
		 * Note: when the expansion is completing, we set
		 * vre_state=DSS_FINISHED (in raidz_reflow_complete_sync())
		 * in a later txg than when we last update spa_ubsync's state
		 * (see the end of spa_raidz_expand_thread()).  Therefore we
		 * may see vre_state!=SCANNING before
		 * VDEV_TOP_ZAP_RAIDZ_EXPAND_STATE=DSS_FINISHED is reflected
		 * on disk, but the copying progress has been synced to disk
		 * (and reflected in spa_ubsync).  In this case it's fine to
		 * treat the expansion as completed, since if we crash there's
		 * no additional copying to do.
		 */
		if (vdrz->vn_vre.vre_state == DSS_SCANNING) {
			ASSERT3P(vd->vdev_spa->spa_raidz_expand, ==,
			    &vdrz->vn_vre);
			lr = zfs_rangelock_enter(&vdrz->vn_vre.vre_rangelock,
			    zio->io_offset, zio->io_size, RL_READER);
			use_scratch =
			    (RRSS_GET_STATE(&vd->vdev_spa->spa_ubsync) ==
			    RRSS_SCRATCH_VALID);
			synced_offset =
			    RRSS_GET_OFFSET(&vd->vdev_spa->spa_ubsync);
			next_offset = vdrz->vn_vre.vre_offset;
			/*
			 * If we haven't resumed expanding since importing the
			 * pool, vre_offset won't have been set yet.  In
			 * this case the next offset to be copied is the same
			 * as what was synced.
			 */
			if (next_offset == UINT64_MAX) {
				next_offset = synced_offset;
			}
		}
		if (use_scratch) {
			zfs_dbgmsg("zio=%px %s io_offset=%llu offset_synced="
			    "%lld next_offset=%lld use_scratch=%u",
			    zio,
			    zio->io_type == ZIO_TYPE_WRITE ? "WRITE" : "READ",
			    (long long)zio->io_offset,
			    (long long)synced_offset,
			    (long long)next_offset,
			    use_scratch);
		}

		rm = vdev_raidz_map_alloc_expanded(zio,
		    tvd->vdev_ashift, vdrz->vd_physical_width,
		    logical_width, vdrz->vd_nparity,
		    synced_offset, next_offset, use_scratch);
		rm->rm_lr = lr;
	} else {
		rm = vdev_raidz_map_alloc(zio,
		    tvd->vdev_ashift, logical_width, vdrz->vd_nparity);
	}
	rm->rm_original_width = vdrz->vd_original_width;

	zio->io_vsd = rm;
	zio->io_vsd_ops = &vdev_raidz_vsd_ops;
	if (zio->io_type == ZIO_TYPE_WRITE) {
		for (int i = 0; i < rm->rm_nrows; i++) {
			vdev_raidz_io_start_write(zio, rm->rm_row[i]);
		}

		if (logical_width == vdrz->vd_physical_width) {
			raidz_start_skip_writes(zio);
		}
	} else {
		ASSERT(zio->io_type == ZIO_TYPE_READ);
		vdev_raidz_io_start_read(zio, rm);
	}

	zio_execute(zio);
}

/*
 * Report a checksum error for a child of a RAID-Z device.
 */
void
vdev_raidz_checksum_error(zio_t *zio, raidz_col_t *rc, abd_t *bad_data)
{
	vdev_t *vd = zio->io_vd->vdev_child[rc->rc_devidx];

	if (!(zio->io_flags & ZIO_FLAG_SPECULATIVE) &&
	    zio->io_priority != ZIO_PRIORITY_REBUILD) {
		zio_bad_cksum_t zbc;
		raidz_map_t *rm = zio->io_vsd;

		zbc.zbc_has_cksum = 0;
		zbc.zbc_injected = rm->rm_ecksuminjected;

		mutex_enter(&vd->vdev_stat_lock);
		vd->vdev_stat.vs_checksum_errors++;
		mutex_exit(&vd->vdev_stat_lock);
		(void) zfs_ereport_post_checksum(zio->io_spa, vd,
		    &zio->io_bookmark, zio, rc->rc_offset, rc->rc_size,
		    rc->rc_abd, bad_data, &zbc);
	}
}

/*
 * We keep track of whether or not there were any injected errors, so that
 * any ereports we generate can note it.
 */
static int
raidz_checksum_verify(zio_t *zio)
{
	zio_bad_cksum_t zbc = {0};
	raidz_map_t *rm = zio->io_vsd;

	int ret = zio_checksum_error(zio, &zbc);
	/*
	 * Any Direct I/O read that has a checksum error must be treated as
	 * suspicious as the contents of the buffer could be getting
	 * manipulated while the I/O is taking place. The checksum verify error
	 * will be reported to the top-level RAIDZ VDEV.
	 */
	if (zio->io_flags & ZIO_FLAG_DIO_READ && ret == ECKSUM) {
		zio->io_error = ret;
		zio->io_flags |= ZIO_FLAG_DIO_CHKSUM_ERR;
		zio_dio_chksum_verify_error_report(zio);
		zio_checksum_verified(zio);
		return (0);
	}

	if (ret != 0 && zbc.zbc_injected != 0)
		rm->rm_ecksuminjected = 1;

	return (ret);
}

/*
 * Generate the parity from the data columns. If we tried and were able to
 * read the parity without error, verify that the generated parity matches the
 * data we read. If it doesn't, we fire off a checksum error. Return the
 * number of such failures.
 */
static int
raidz_parity_verify(zio_t *zio, raidz_row_t *rr)
{
	abd_t *orig[VDEV_RAIDZ_MAXPARITY];
	int c, ret = 0;
	raidz_map_t *rm = zio->io_vsd;
	raidz_col_t *rc;

	blkptr_t *bp = zio->io_bp;
	enum zio_checksum checksum = (bp == NULL ? zio->io_prop.zp_checksum :
	    (BP_IS_GANG(bp) ? ZIO_CHECKSUM_GANG_HEADER : BP_GET_CHECKSUM(bp)));

	if (checksum == ZIO_CHECKSUM_NOPARITY)
		return (ret);

	for (c = 0; c < rr->rr_firstdatacol; c++) {
		rc = &rr->rr_col[c];
		if (!rc->rc_tried || rc->rc_error != 0)
			continue;

		orig[c] = rc->rc_abd;
		ASSERT3U(abd_get_size(rc->rc_abd), ==, rc->rc_size);
		rc->rc_abd = abd_alloc_linear(rc->rc_size, B_FALSE);
	}

	/*
	 * Verify any empty sectors are zero filled to ensure the parity
	 * is calculated correctly even if these non-data sectors are damaged.
	 */
	if (rr->rr_nempty && rr->rr_abd_empty != NULL)
		ret += vdev_draid_map_verify_empty(zio, rr);

	/*
	 * Regenerates parity even for !tried||rc_error!=0 columns.  This
	 * isn't harmful but it does have the side effect of fixing stuff
	 * we didn't realize was necessary (i.e. even if we return 0).
	 */
	vdev_raidz_generate_parity_row(rm, rr);

	for (c = 0; c < rr->rr_firstdatacol; c++) {
		rc = &rr->rr_col[c];

		if (!rc->rc_tried || rc->rc_error != 0)
			continue;

		if (abd_cmp(orig[c], rc->rc_abd) != 0) {
			zfs_dbgmsg("found error on col=%u devidx=%u off %llx",
			    c, (int)rc->rc_devidx, (u_longlong_t)rc->rc_offset);
			vdev_raidz_checksum_error(zio, rc, orig[c]);
			rc->rc_error = SET_ERROR(ECKSUM);
			ret++;
		}
		abd_free(orig[c]);
	}

	return (ret);
}

static int
vdev_raidz_worst_error(raidz_row_t *rr)
{
	int error = 0;

	for (int c = 0; c < rr->rr_cols; c++) {
		error = zio_worst_error(error, rr->rr_col[c].rc_error);
		error = zio_worst_error(error, rr->rr_col[c].rc_shadow_error);
	}

	return (error);
}

static void
vdev_raidz_io_done_verified(zio_t *zio, raidz_row_t *rr)
{
	int unexpected_errors = 0;
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;

	ASSERT3U(zio->io_type, ==, ZIO_TYPE_READ);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_error) {
			if (c < rr->rr_firstdatacol)
				parity_errors++;
			else
				data_errors++;

			if (!rc->rc_skipped)
				unexpected_errors++;
		} else if (c < rr->rr_firstdatacol && !rc->rc_tried) {
			parity_untried++;
		}

		if (rc->rc_force_repair)
			unexpected_errors++;
	}

	/*
	 * If we read more parity disks than were used for
	 * reconstruction, confirm that the other parity disks produced
	 * correct data.
	 *
	 * Note that we also regenerate parity when resilvering so we
	 * can write it out to failed devices later.
	 */
	if (parity_errors + parity_untried <
	    rr->rr_firstdatacol - data_errors ||
	    (zio->io_flags & ZIO_FLAG_RESILVER)) {
		int n = raidz_parity_verify(zio, rr);
		unexpected_errors += n;
	}

	if (zio->io_error == 0 && spa_writeable(zio->io_spa) &&
	    (unexpected_errors > 0 || (zio->io_flags & ZIO_FLAG_RESILVER))) {
		/*
		 * Use the good data we have in hand to repair damaged children.
		 */
		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			vdev_t *vd = zio->io_vd;
			vdev_t *cvd = vd->vdev_child[rc->rc_devidx];

			if (!rc->rc_allow_repair) {
				continue;
			} else if (!rc->rc_force_repair &&
			    (rc->rc_error == 0 || rc->rc_size == 0)) {
				continue;
			}
			/*
			 * We do not allow self healing for Direct I/O reads.
			 * See comment in vdev_raid_row_alloc().
			 */
			ASSERT0(zio->io_flags & ZIO_FLAG_DIO_READ);

			zfs_dbgmsg("zio=%px repairing c=%u devidx=%u "
			    "offset=%llx",
			    zio, c, rc->rc_devidx, (long long)rc->rc_offset);

			zio_nowait(zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_offset, rc->rc_abd, rc->rc_size,
			    ZIO_TYPE_WRITE,
			    zio->io_priority == ZIO_PRIORITY_REBUILD ?
			    ZIO_PRIORITY_REBUILD : ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | (unexpected_errors ?
			    ZIO_FLAG_SELF_HEAL : 0), NULL, NULL));
		}
	}

	/*
	 * Scrub or resilver i/o's: overwrite any shadow locations with the
	 * good data.  This ensures that if we've already copied this sector,
	 * it will be corrected if it was damaged.  This writes more than is
	 * necessary, but since expansion is paused during scrub/resilver, at
	 * most a single row will have a shadow location.
	 */
	if (zio->io_error == 0 && spa_writeable(zio->io_spa) &&
	    (zio->io_flags & (ZIO_FLAG_RESILVER | ZIO_FLAG_SCRUB))) {
		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			vdev_t *vd = zio->io_vd;

			if (rc->rc_shadow_devidx == INT_MAX || rc->rc_size == 0)
				continue;
			vdev_t *cvd = vd->vdev_child[rc->rc_shadow_devidx];

			/*
			 * Note: We don't want to update the repair stats
			 * because that would incorrectly indicate that there
			 * was bad data to repair, which we aren't sure about.
			 * By clearing the SCAN_THREAD flag, we prevent this
			 * from happening, despite having the REPAIR flag set.
			 * We need to set SELF_HEAL so that this i/o can't be
			 * bypassed by zio_vdev_io_start().
			 */
			zio_t *cio = zio_vdev_child_io(zio, NULL, cvd,
			    rc->rc_shadow_offset, rc->rc_abd, rc->rc_size,
			    ZIO_TYPE_WRITE, ZIO_PRIORITY_ASYNC_WRITE,
			    ZIO_FLAG_IO_REPAIR | ZIO_FLAG_SELF_HEAL,
			    NULL, NULL);
			cio->io_flags &= ~ZIO_FLAG_SCAN_THREAD;
			zio_nowait(cio);
		}
	}
}

static void
raidz_restore_orig_data(raidz_map_t *rm)
{
	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];
		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			if (rc->rc_need_orig_restore) {
				abd_copy(rc->rc_abd,
				    rc->rc_orig_data, rc->rc_size);
				rc->rc_need_orig_restore = B_FALSE;
			}
		}
	}
}

/*
 * During raidz_reconstruct() for expanded VDEV, we need special consideration
 * failure simulations.  See note in raidz_reconstruct() on simulating failure
 * of a pre-expansion device.
 *
 * Treating logical child i as failed, return TRUE if the given column should
 * be treated as failed.  The idea of logical children allows us to imagine
 * that a disk silently failed before a RAIDZ expansion (reads from this disk
 * succeed but return the wrong data).  Since the expansion doesn't verify
 * checksums, the incorrect data will be moved to new locations spread among
 * the children (going diagonally across them).
 *
 * Higher "logical child failures" (values of `i`) indicate these
 * "pre-expansion failures".  The first physical_width values imagine that a
 * current child failed; the next physical_width-1 values imagine that a
 * child failed before the most recent expansion; the next physical_width-2
 * values imagine a child failed in the expansion before that, etc.
 */
static boolean_t
raidz_simulate_failure(int physical_width, int original_width, int ashift,
    int i, raidz_col_t *rc)
{
	uint64_t sector_id =
	    physical_width * (rc->rc_offset >> ashift) +
	    rc->rc_devidx;

	for (int w = physical_width; w >= original_width; w--) {
		if (i < w) {
			return (sector_id % w == i);
		} else {
			i -= w;
		}
	}
	ASSERT(!"invalid logical child id");
	return (B_FALSE);
}

/*
 * returns EINVAL if reconstruction of the block will not be possible
 * returns ECKSUM if this specific reconstruction failed
 * returns 0 on successful reconstruction
 */
static int
raidz_reconstruct(zio_t *zio, int *ltgts, int ntgts, int nparity)
{
	raidz_map_t *rm = zio->io_vsd;
	int physical_width = zio->io_vd->vdev_children;
	int original_width = (rm->rm_original_width != 0) ?
	    rm->rm_original_width : physical_width;
	int dbgmsg = zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT;

	if (dbgmsg) {
		zfs_dbgmsg("raidz_reconstruct_expanded(zio=%px ltgts=%u,%u,%u "
		    "ntgts=%u", zio, ltgts[0], ltgts[1], ltgts[2], ntgts);
	}

	/* Reconstruct each row */
	for (int r = 0; r < rm->rm_nrows; r++) {
		raidz_row_t *rr = rm->rm_row[r];
		int my_tgts[VDEV_RAIDZ_MAXPARITY]; /* value is child id */
		int t = 0;
		int dead = 0;
		int dead_data = 0;

		if (dbgmsg)
			zfs_dbgmsg("raidz_reconstruct_expanded(row=%u)", r);

		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			ASSERT0(rc->rc_need_orig_restore);
			if (rc->rc_error != 0) {
				dead++;
				if (c >= nparity)
					dead_data++;
				continue;
			}
			if (rc->rc_size == 0)
				continue;
			for (int lt = 0; lt < ntgts; lt++) {
				if (raidz_simulate_failure(physical_width,
				    original_width,
				    zio->io_vd->vdev_top->vdev_ashift,
				    ltgts[lt], rc)) {
					if (rc->rc_orig_data == NULL) {
						rc->rc_orig_data =
						    abd_alloc_linear(
						    rc->rc_size, B_TRUE);
						abd_copy(rc->rc_orig_data,
						    rc->rc_abd, rc->rc_size);
					}
					rc->rc_need_orig_restore = B_TRUE;

					dead++;
					if (c >= nparity)
						dead_data++;
					/*
					 * Note: simulating failure of a
					 * pre-expansion device can hit more
					 * than one column, in which case we
					 * might try to simulate more failures
					 * than can be reconstructed, which is
					 * also more than the size of my_tgts.
					 * This check prevents accessing past
					 * the end of my_tgts.  The "dead >
					 * nparity" check below will fail this
					 * reconstruction attempt.
					 */
					if (t < VDEV_RAIDZ_MAXPARITY) {
						my_tgts[t++] = c;
						if (dbgmsg) {
							zfs_dbgmsg("simulating "
							    "failure of col %u "
							    "devidx %u", c,
							    (int)rc->rc_devidx);
						}
					}
					break;
				}
			}
		}
		if (dead > nparity) {
			/* reconstruction not possible */
			if (dbgmsg) {
				zfs_dbgmsg("reconstruction not possible; "
				    "too many failures");
			}
			raidz_restore_orig_data(rm);
			return (EINVAL);
		}
		if (dead_data > 0)
			vdev_raidz_reconstruct_row(rm, rr, my_tgts, t);
	}

	/* Check for success */
	if (raidz_checksum_verify(zio) == 0) {
		if (zio->io_flags & ZIO_FLAG_DIO_CHKSUM_ERR)
			return (0);

		/* Reconstruction succeeded - report errors */
		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];

			for (int c = 0; c < rr->rr_cols; c++) {
				raidz_col_t *rc = &rr->rr_col[c];
				if (rc->rc_need_orig_restore) {
					/*
					 * Note: if this is a parity column,
					 * we don't really know if it's wrong.
					 * We need to let
					 * vdev_raidz_io_done_verified() check
					 * it, and if we set rc_error, it will
					 * think that it is a "known" error
					 * that doesn't need to be checked
					 * or corrected.
					 */
					if (rc->rc_error == 0 &&
					    c >= rr->rr_firstdatacol) {
						vdev_raidz_checksum_error(zio,
						    rc, rc->rc_orig_data);
						rc->rc_error =
						    SET_ERROR(ECKSUM);
					}
					rc->rc_need_orig_restore = B_FALSE;
				}
			}

			vdev_raidz_io_done_verified(zio, rr);
		}

		zio_checksum_verified(zio);

		if (dbgmsg) {
			zfs_dbgmsg("reconstruction successful "
			    "(checksum verified)");
		}
		return (0);
	}

	/* Reconstruction failed - restore original data */
	raidz_restore_orig_data(rm);
	if (dbgmsg) {
		zfs_dbgmsg("raidz_reconstruct_expanded(zio=%px) checksum "
		    "failed", zio);
	}
	return (ECKSUM);
}

/*
 * Iterate over all combinations of N bad vdevs and attempt a reconstruction.
 * Note that the algorithm below is non-optimal because it doesn't take into
 * account how reconstruction is actually performed. For example, with
 * triple-parity RAID-Z the reconstruction procedure is the same if column 4
 * is targeted as invalid as if columns 1 and 4 are targeted since in both
 * cases we'd only use parity information in column 0.
 *
 * The order that we find the various possible combinations of failed
 * disks is dictated by these rules:
 * - Examine each "slot" (the "i" in tgts[i])
 *   - Try to increment this slot (tgts[i] += 1)
 *   - if we can't increment because it runs into the next slot,
 *     reset our slot to the minimum, and examine the next slot
 *
 *  For example, with a 6-wide RAIDZ3, and no known errors (so we have to choose
 *  3 columns to reconstruct), we will generate the following sequence:
 *
 *  STATE        ACTION
 *  0 1 2        special case: skip since these are all parity
 *  0 1   3      first slot: reset to 0; middle slot: increment to 2
 *  0   2 3      first slot: increment to 1
 *    1 2 3      first: reset to 0; middle: reset to 1; last: increment to 4
 *  0 1     4    first: reset to 0; middle: increment to 2
 *  0   2   4    first: increment to 1
 *    1 2   4    first: reset to 0; middle: increment to 3
 *  0     3 4    first: increment to 1
 *    1   3 4    first: increment to 2
 *      2 3 4    first: reset to 0; middle: reset to 1; last: increment to 5
 *  0 1       5  first: reset to 0; middle: increment to 2
 *  0   2     5  first: increment to 1
 *    1 2     5  first: reset to 0; middle: increment to 3
 *  0     3   5  first: increment to 1
 *    1   3   5  first: increment to 2
 *      2 3   5  first: reset to 0; middle: increment to 4
 *  0       4 5  first: increment to 1
 *    1     4 5  first: increment to 2
 *      2   4 5  first: increment to 3
 *        3 4 5  done
 *
 * This strategy works for dRAID but is less efficient when there are a large
 * number of child vdevs and therefore permutations to check. Furthermore,
 * since the raidz_map_t rows likely do not overlap, reconstruction would be
 * possible as long as there are no more than nparity data errors per row.
 * These additional permutations are not currently checked but could be as
 * a future improvement.
 *
 * Returns 0 on success, ECKSUM on failure.
 */
static int
vdev_raidz_combrec(zio_t *zio)
{
	int nparity = vdev_get_nparity(zio->io_vd);
	raidz_map_t *rm = zio->io_vsd;
	int physical_width = zio->io_vd->vdev_children;
	int original_width = (rm->rm_original_width != 0) ?
	    rm->rm_original_width : physical_width;

	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];
		int total_errors = 0;

		for (int c = 0; c < rr->rr_cols; c++) {
			if (rr->rr_col[c].rc_error)
				total_errors++;
		}

		if (total_errors > nparity)
			return (vdev_raidz_worst_error(rr));
	}

	for (int num_failures = 1; num_failures <= nparity; num_failures++) {
		int tstore[VDEV_RAIDZ_MAXPARITY + 2];
		int *ltgts = &tstore[1]; /* value is logical child ID */


		/*
		 * Determine number of logical children, n.  See comment
		 * above raidz_simulate_failure().
		 */
		int n = 0;
		for (int w = physical_width;
		    w >= original_width; w--) {
			n += w;
		}

		ASSERT3U(num_failures, <=, nparity);
		ASSERT3U(num_failures, <=, VDEV_RAIDZ_MAXPARITY);

		/* Handle corner cases in combrec logic */
		ltgts[-1] = -1;
		for (int i = 0; i < num_failures; i++) {
			ltgts[i] = i;
		}
		ltgts[num_failures] = n;

		for (;;) {
			int err = raidz_reconstruct(zio, ltgts, num_failures,
			    nparity);
			if (err == EINVAL) {
				/*
				 * Reconstruction not possible with this #
				 * failures; try more failures.
				 */
				break;
			} else if (err == 0)
				return (0);

			/* Compute next targets to try */
			for (int t = 0; ; t++) {
				ASSERT3U(t, <, num_failures);
				ltgts[t]++;
				if (ltgts[t] == n) {
					/* try more failures */
					ASSERT3U(t, ==, num_failures - 1);
					if (zfs_flags &
					    ZFS_DEBUG_RAIDZ_RECONSTRUCT) {
						zfs_dbgmsg("reconstruction "
						    "failed for num_failures="
						    "%u; tried all "
						    "combinations",
						    num_failures);
					}
					break;
				}

				ASSERT3U(ltgts[t], <, n);
				ASSERT3U(ltgts[t], <=, ltgts[t + 1]);

				/*
				 * If that spot is available, we're done here.
				 * Try the next combination.
				 */
				if (ltgts[t] != ltgts[t + 1])
					break; // found next combination

				/*
				 * Otherwise, reset this tgt to the minimum,
				 * and move on to the next tgt.
				 */
				ltgts[t] = ltgts[t - 1] + 1;
				ASSERT3U(ltgts[t], ==, t);
			}

			/* Increase the number of failures and keep trying. */
			if (ltgts[num_failures - 1] == n)
				break;
		}
	}
	if (zfs_flags & ZFS_DEBUG_RAIDZ_RECONSTRUCT)
		zfs_dbgmsg("reconstruction failed for all num_failures");
	return (ECKSUM);
}

void
vdev_raidz_reconstruct(raidz_map_t *rm, const int *t, int nt)
{
	for (uint64_t row = 0; row < rm->rm_nrows; row++) {
		raidz_row_t *rr = rm->rm_row[row];
		vdev_raidz_reconstruct_row(rm, rr, t, nt);
	}
}

/*
 * Complete a write IO operation on a RAIDZ VDev
 *
 * Outline:
 *   1. Check for errors on the child IOs.
 *   2. Return, setting an error code if too few child VDevs were written
 *      to reconstruct the data later.  Note that partial writes are
 *      considered successful if they can be reconstructed at all.
 */
static void
vdev_raidz_io_done_write_impl(zio_t *zio, raidz_row_t *rr)
{
	int normal_errors = 0;
	int shadow_errors = 0;

	ASSERT3U(rr->rr_missingparity, <=, rr->rr_firstdatacol);
	ASSERT3U(rr->rr_missingdata, <=, rr->rr_cols - rr->rr_firstdatacol);
	ASSERT3U(zio->io_type, ==, ZIO_TYPE_WRITE);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		if (rc->rc_error != 0) {
			ASSERT(rc->rc_error != ECKSUM);	/* child has no bp */
			normal_errors++;
		}
		if (rc->rc_shadow_error != 0) {
			ASSERT(rc->rc_shadow_error != ECKSUM);
			shadow_errors++;
		}
	}

	/*
	 * Treat partial writes as a success. If we couldn't write enough
	 * columns to reconstruct the data, the I/O failed.  Otherwise, good
	 * enough.  Note that in the case of a shadow write (during raidz
	 * expansion), depending on if we crash, either the normal (old) or
	 * shadow (new) location may become the "real" version of the block,
	 * so both locations must have sufficient redundancy.
	 *
	 * Now that we support write reallocation, it would be better
	 * to treat partial failure as real failure unless there are
	 * no non-degraded top-level vdevs left, and not update DTLs
	 * if we intend to reallocate.
	 */
	if (normal_errors > rr->rr_firstdatacol ||
	    shadow_errors > rr->rr_firstdatacol) {
		zio->io_error = zio_worst_error(zio->io_error,
		    vdev_raidz_worst_error(rr));
	}
}

static void
vdev_raidz_io_done_reconstruct_known_missing(zio_t *zio, raidz_map_t *rm,
    raidz_row_t *rr)
{
	int parity_errors = 0;
	int parity_untried = 0;
	int data_errors = 0;
	int total_errors = 0;

	ASSERT3U(rr->rr_missingparity, <=, rr->rr_firstdatacol);
	ASSERT3U(rr->rr_missingdata, <=, rr->rr_cols - rr->rr_firstdatacol);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];

		/*
		 * If scrubbing and a replacing/sparing child vdev determined
		 * that not all of its children have an identical copy of the
		 * data, then clear the error so the column is treated like
		 * any other read and force a repair to correct the damage.
		 */
		if (rc->rc_error == ECKSUM) {
			ASSERT(zio->io_flags & ZIO_FLAG_SCRUB);
			vdev_raidz_checksum_error(zio, rc, rc->rc_abd);
			rc->rc_force_repair = 1;
			rc->rc_error = 0;
		}

		if (rc->rc_error) {
			if (c < rr->rr_firstdatacol)
				parity_errors++;
			else
				data_errors++;

			total_errors++;
		} else if (c < rr->rr_firstdatacol && !rc->rc_tried) {
			parity_untried++;
		}
	}

	/*
	 * If there were data errors and the number of errors we saw was
	 * correctable -- less than or equal to the number of parity disks read
	 * -- reconstruct based on the missing data.
	 */
	if (data_errors != 0 &&
	    total_errors <= rr->rr_firstdatacol - parity_untried) {
		/*
		 * We either attempt to read all the parity columns or
		 * none of them. If we didn't try to read parity, we
		 * wouldn't be here in the correctable case. There must
		 * also have been fewer parity errors than parity
		 * columns or, again, we wouldn't be in this code path.
		 */
		ASSERT(parity_untried == 0);
		ASSERT(parity_errors < rr->rr_firstdatacol);

		/*
		 * Identify the data columns that reported an error.
		 */
		int n = 0;
		int tgts[VDEV_RAIDZ_MAXPARITY];
		for (int c = rr->rr_firstdatacol; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			if (rc->rc_error != 0) {
				ASSERT(n < VDEV_RAIDZ_MAXPARITY);
				tgts[n++] = c;
			}
		}

		ASSERT(rr->rr_firstdatacol >= n);

		vdev_raidz_reconstruct_row(rm, rr, tgts, n);
	}
}

/*
 * Return the number of reads issued.
 */
static int
vdev_raidz_read_all(zio_t *zio, raidz_row_t *rr)
{
	vdev_t *vd = zio->io_vd;
	int nread = 0;

	rr->rr_missingdata = 0;
	rr->rr_missingparity = 0;

	/*
	 * If this rows contains empty sectors which are not required
	 * for a normal read then allocate an ABD for them now so they
	 * may be read, verified, and any needed repairs performed.
	 */
	if (rr->rr_nempty != 0 && rr->rr_abd_empty == NULL)
		vdev_draid_map_alloc_empty(zio, rr);

	for (int c = 0; c < rr->rr_cols; c++) {
		raidz_col_t *rc = &rr->rr_col[c];
		if (rc->rc_tried || rc->rc_size == 0)
			continue;

		zio_nowait(zio_vdev_child_io(zio, NULL,
		    vd->vdev_child[rc->rc_devidx],
		    rc->rc_offset, rc->rc_abd, rc->rc_size,
		    zio->io_type, zio->io_priority, 0,
		    vdev_raidz_child_done, rc));
		nread++;
	}
	return (nread);
}

/*
 * We're here because either there were too many errors to even attempt
 * reconstruction (total_errors == rm_first_datacol), or vdev_*_combrec()
 * failed. In either case, there is enough bad data to prevent reconstruction.
 * Start checksum ereports for all children which haven't failed.
 */
static void
vdev_raidz_io_done_unrecoverable(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	for (int i = 0; i < rm->rm_nrows; i++) {
		raidz_row_t *rr = rm->rm_row[i];

		for (int c = 0; c < rr->rr_cols; c++) {
			raidz_col_t *rc = &rr->rr_col[c];
			vdev_t *cvd = zio->io_vd->vdev_child[rc->rc_devidx];

			if (rc->rc_error != 0)
				continue;

			zio_bad_cksum_t zbc;
			zbc.zbc_has_cksum = 0;
			zbc.zbc_injected = rm->rm_ecksuminjected;
			mutex_enter(&cvd->vdev_stat_lock);
			cvd->vdev_stat.vs_checksum_errors++;
			mutex_exit(&cvd->vdev_stat_lock);
			(void) zfs_ereport_start_checksum(zio->io_spa,
			    cvd, &zio->io_bookmark, zio, rc->rc_offset,
			    rc->rc_size, &zbc);
		}
	}
}

void
vdev_raidz_io_done(zio_t *zio)
{
	raidz_map_t *rm = zio->io_vsd;

	ASSERT(zio->io_bp != NULL);
	if (zio->io_type == ZIO_TYPE_WRITE) {
		for (int i = 0; i < rm->rm_nrows; i++) {
			vdev_raidz_io_done_write_impl(zio, rm->rm_row[i]);
		}
	} else {
		if (rm->rm_phys_col) {
			/*
			 * This is an aggregated read.  Copy the data and status
			 * from the aggregate abd's to the individual rows.
			 */
			for (int i = 0; i < rm->rm_nrows; i++) {
				raidz_row_t *rr = rm->rm_row[i];

				for (int c = 0; c < rr->rr_cols; c++) {
					raidz_col_t *rc = &rr->rr_col[c];
					if (rc->rc_tried || rc->rc_size == 0)
						continue;

					raidz_col_t *prc =
					    &rm->rm_phys_col[rc->rc_devidx];
					rc->rc_error = prc->rc_error;
					rc->rc_tried = prc->rc_tried;
					rc->rc_skipped = prc->rc_skipped;
					if (c >= rr->rr_firstdatacol) {
						/*
						 * Note: this is slightly faster
						 * than using abd_copy_off().
						 */
						char *physbuf = abd_to_buf(
						    prc->rc_abd);
						void *physloc = physbuf +
						    rc->rc_offset -
						    prc->rc_offset;

						abd_copy_from_buf(rc->rc_abd,
						    physloc, rc->rc_size);
					}
				}
			}
		}

		for (int i = 0; i < rm->rm_nrows; i++) {
			raidz_row_t *rr = rm->rm_row[i];
			vdev_raidz_io_done_reconstruct_known_missing(zio,
			    rm, rr);
		}

		if (raidz_checksum_verify(zio) == 0) {
			if (zio->io_flags & ZIO_FLAG_DIO_CHKSUM_ERR)
				goto done;

			for (int i = 0; i < rm->rm_nrows; i++) {
				raidz_row_t *rr = rm->rm_row[i];
				vdev_raidz_io_done_verified(zio, rr);
			}
			zio_checksum_verified(zio);
		} else {
			/*
			 * A sequential resilver has no checksum which makes
			 * combinatoral reconstruction impossible. This code
			 * path is unreachable since raidz_checksum_verify()
			 * has no checksum to verify and must succeed.
			 */
			ASSERT3U(zio->io_priority, !=, ZIO_PRIORITY_REBUILD);

			/*
			 * This isn't a typical situation -- either we got a
			 * read error or a child silently returned bad data.
			 * Read every block so we can try again with as much
			 * data and parity as we can track down. If we've
			 * already been through once before, all children will
			 * be marked as tried so we'll proceed to combinatorial
			 * reconstruction.
			 */
			int nread = 0;
			for (int i = 0; i < rm->rm_nrows; i++) {
				nread += vdev_raidz_read_all(zio,
				    rm->rm_row[i]);
			}
			if (nread != 0) {
				/*
				 * Normally our stage is VDEV_IO_DONE, but if
				 * we've already called redone(), it will have
				 * changed to VDEV_IO_START, in which case we
				 * don't want to call redone() again.
				 */
				if (zio->io_stage != ZIO_STAGE_VDEV_IO_START)
					zio_vdev_io_redone(zio);
				return;
			}
			/*
			 * It would be too expensive to try every possible
			 * combination of failed sectors in every row, so
			 * instead we try every combination of failed current or
			 * past physical disk. This means that if the incorrect
			 * sectors were all on Nparity disks at any point in the
			 * past, we will find the correct data.  The only known
			 * case where this is less durable than a non-expanded
			 * RAIDZ, is if we have a silent failure during
			 * expansion.  In that case, one block could be
			 * partially in the old format and partially in the
			 * new format, so we'd lost some sectors from the old
			 * format and some from the new format.
			 *
			 * e.g. logical_width=4 physical_width=6
			 * the 15 (6+5+4) possible failed disks are:
			 * width=6 child=0
			 * width=6 child=1
			 * width=6 child=2
			 * width=6 child=3
			 * width=6 child=4
			 * width=6 child=5
			 * width=5 child=0
			 * width=5 child=1
			 * width=5 child=2
			 * width=5 child=3
			 * width=5 child=4
			 * width=4 child=0
			 * width=4 child=1
			 * width=4 child=2
			 * width=4 child=3
			 * And we will try every combination of Nparity of these
			 * failing.
			 *
			 * As a first pass, we can generate every combo,
			 * and try reconstructing, ignoring any known
			 * failures.  If any row has too many known + simulated
			 * failures, then we bail on reconstructing with this
			 * number of simulated failures.  As an improvement,
			 * we could detect the number of whole known failures
			 * (i.e. we have known failures on these disks for
			 * every row; the disks never succeeded), and
			 * subtract that from the max # failures to simulate.
			 * We could go even further like the current
			 * combrec code, but that doesn't seem like it
			 * gains us very much.  If we simulate a failure
			 * that is also a known failure, that's fine.
			 */
			zio->io_error = vdev_raidz_combrec(zio);
			if (zio->io_error == ECKSUM &&
			    !(zio->io_flags & ZIO_FLAG_SPECULATIVE)) {
				vdev_raidz_io_done_unrecoverable(zio);
			}
		}
	}
done:
	if (rm->rm_lr != NULL) {
		zfs_rangelock_exit(rm->rm_lr);
		rm->rm_lr = NULL;
	}
}

static void
vdev_raidz_state_change(vdev_t *vd, int faulted, int degraded)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	if (faulted > vdrz->vd_nparity)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_CANT_OPEN,
		    VDEV_AUX_NO_REPLICAS);
	else if (degraded + faulted != 0)
		vdev_set_state(vd, B_FALSE, VDEV_STATE_DEGRADED, VDEV_AUX_NONE);
	else
		vdev_set_state(vd, B_FALSE, VDEV_STATE_HEALTHY, VDEV_AUX_NONE);
}

/*
 * Determine if any portion of the provided block resides on a child vdev
 * with a dirty DTL and therefore needs to be resilvered.  The function
 * assumes that at least one DTL is dirty which implies that full stripe
 * width blocks must be resilvered.
 */
static boolean_t
vdev_raidz_need_resilver(vdev_t *vd, const dva_t *dva, size_t psize,
    uint64_t phys_birth)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;

	/*
	 * If we're in the middle of a RAIDZ expansion, this block may be in
	 * the old and/or new location.  For simplicity, always resilver it.
	 */
	if (vdrz->vn_vre.vre_state == DSS_SCANNING)
		return (B_TRUE);

	uint64_t dcols = vd->vdev_children;
	uint64_t nparity = vdrz->vd_nparity;
	uint64_t ashift = vd->vdev_top->vdev_ashift;
	/* The starting RAIDZ (parent) vdev sector of the block. */
	uint64_t b = DVA_GET_OFFSET(dva) >> ashift;
	/* The zio's size in units of the vdev's minimum sector size. */
	uint64_t s = ((psize - 1) >> ashift) + 1;
	/* The first column for this stripe. */
	uint64_t f = b % dcols;

	/* Unreachable by sequential resilver. */
	ASSERT3U(phys_birth, !=, TXG_UNKNOWN);

	if (!vdev_dtl_contains(vd, DTL_PARTIAL, phys_birth, 1))
		return (B_FALSE);

	if (s + nparity >= dcols)
		return (B_TRUE);

	for (uint64_t c = 0; c < s + nparity; c++) {
		uint64_t devidx = (f + c) % dcols;
		vdev_t *cvd = vd->vdev_child[devidx];

		/*
		 * dsl_scan_need_resilver() already checked vd with
		 * vdev_dtl_contains(). So here just check cvd with
		 * vdev_dtl_empty(), cheaper and a good approximation.
		 */
		if (!vdev_dtl_empty(cvd, DTL_PARTIAL))
			return (B_TRUE);
	}

	return (B_FALSE);
}

static void
vdev_raidz_xlate(vdev_t *cvd, const zfs_range_seg64_t *logical_rs,
    zfs_range_seg64_t *physical_rs, zfs_range_seg64_t *remain_rs)
{
	(void) remain_rs;

	vdev_t *raidvd = cvd->vdev_parent;
	ASSERT(raidvd->vdev_ops == &vdev_raidz_ops);

	vdev_raidz_t *vdrz = raidvd->vdev_tsd;

	if (vdrz->vn_vre.vre_state == DSS_SCANNING) {
		/*
		 * We're in the middle of expansion, in which case the
		 * translation is in flux.  Any answer we give may be wrong
		 * by the time we return, so it isn't safe for the caller to
		 * act on it.  Therefore we say that this range isn't present
		 * on any children.  The only consumers of this are "zpool
		 * initialize" and trimming, both of which are "best effort"
		 * anyway.
		 */
		physical_rs->rs_start = physical_rs->rs_end = 0;
		remain_rs->rs_start = remain_rs->rs_end = 0;
		return;
	}

	uint64_t width = vdrz->vd_physical_width;
	uint64_t tgt_col = cvd->vdev_id;
	uint64_t ashift = raidvd->vdev_top->vdev_ashift;

	/* make sure the offsets are block-aligned */
	ASSERT0(logical_rs->rs_start % (1 << ashift));
	ASSERT0(logical_rs->rs_end % (1 << ashift));
	uint64_t b_start = logical_rs->rs_start >> ashift;
	uint64_t b_end = logical_rs->rs_end >> ashift;

	uint64_t start_row = 0;
	if (b_start > tgt_col) /* avoid underflow */
		start_row = ((b_start - tgt_col - 1) / width) + 1;

	uint64_t end_row = 0;
	if (b_end > tgt_col)
		end_row = ((b_end - tgt_col - 1) / width) + 1;

	physical_rs->rs_start = start_row << ashift;
	physical_rs->rs_end = end_row << ashift;

	ASSERT3U(physical_rs->rs_start, <=, logical_rs->rs_start);
	ASSERT3U(physical_rs->rs_end - physical_rs->rs_start, <=,
	    logical_rs->rs_end - logical_rs->rs_start);
}

static void
raidz_reflow_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = arg;
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	vdev_raidz_expand_t *vre = spa->spa_raidz_expand;

	/*
	 * Ensure there are no i/os to the range that is being committed.
	 */
	uint64_t old_offset = RRSS_GET_OFFSET(&spa->spa_uberblock);
	ASSERT3U(vre->vre_offset_pertxg[txgoff], >=, old_offset);

	mutex_enter(&vre->vre_lock);
	uint64_t new_offset =
	    MIN(vre->vre_offset_pertxg[txgoff], vre->vre_failed_offset);
	/*
	 * We should not have committed anything that failed.
	 */
	VERIFY3U(vre->vre_failed_offset, >=, old_offset);
	mutex_exit(&vre->vre_lock);

	zfs_locked_range_t *lr = zfs_rangelock_enter(&vre->vre_rangelock,
	    old_offset, new_offset - old_offset,
	    RL_WRITER);

	/*
	 * Update the uberblock that will be written when this txg completes.
	 */
	RAIDZ_REFLOW_SET(&spa->spa_uberblock,
	    RRSS_SCRATCH_INVALID_SYNCED_REFLOW, new_offset);
	vre->vre_offset_pertxg[txgoff] = 0;
	zfs_rangelock_exit(lr);

	mutex_enter(&vre->vre_lock);
	vre->vre_bytes_copied += vre->vre_bytes_copied_pertxg[txgoff];
	vre->vre_bytes_copied_pertxg[txgoff] = 0;
	mutex_exit(&vre->vre_lock);

	vdev_t *vd = vdev_lookup_top(spa, vre->vre_vdev_id);
	VERIFY0(zap_update(spa->spa_meta_objset,
	    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_BYTES_COPIED,
	    sizeof (vre->vre_bytes_copied), 1, &vre->vre_bytes_copied, tx));
}

static void
raidz_reflow_complete_sync(void *arg, dmu_tx_t *tx)
{
	spa_t *spa = arg;
	vdev_raidz_expand_t *vre = spa->spa_raidz_expand;
	vdev_t *raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);
	vdev_raidz_t *vdrz = raidvd->vdev_tsd;

	for (int i = 0; i < TXG_SIZE; i++)
		VERIFY0(vre->vre_offset_pertxg[i]);

	reflow_node_t *re = kmem_zalloc(sizeof (*re), KM_SLEEP);
	re->re_txg = tx->tx_txg + TXG_CONCURRENT_STATES;
	re->re_logical_width = vdrz->vd_physical_width;
	mutex_enter(&vdrz->vd_expand_lock);
	avl_add(&vdrz->vd_expand_txgs, re);
	mutex_exit(&vdrz->vd_expand_lock);

	vdev_t *vd = vdev_lookup_top(spa, vre->vre_vdev_id);

	/*
	 * Dirty the config so that the updated ZPOOL_CONFIG_RAIDZ_EXPAND_TXGS
	 * will get written (based on vd_expand_txgs).
	 */
	vdev_config_dirty(vd);

	/*
	 * Before we change vre_state, the on-disk state must reflect that we
	 * have completed all copying, so that vdev_raidz_io_start() can use
	 * vre_state to determine if the reflow is in progress.  See also the
	 * end of spa_raidz_expand_thread().
	 */
	VERIFY3U(RRSS_GET_OFFSET(&spa->spa_ubsync), ==,
	    raidvd->vdev_ms_count << raidvd->vdev_ms_shift);

	vre->vre_end_time = gethrestime_sec();
	vre->vre_state = DSS_FINISHED;

	uint64_t state = vre->vre_state;
	VERIFY0(zap_update(spa->spa_meta_objset,
	    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_STATE,
	    sizeof (state), 1, &state, tx));

	uint64_t end_time = vre->vre_end_time;
	VERIFY0(zap_update(spa->spa_meta_objset,
	    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_END_TIME,
	    sizeof (end_time), 1, &end_time, tx));

	spa->spa_uberblock.ub_raidz_reflow_info = 0;

	spa_history_log_internal(spa, "raidz vdev expansion completed",  tx,
	    "%s vdev %llu new width %llu", spa_name(spa),
	    (unsigned long long)vd->vdev_id,
	    (unsigned long long)vd->vdev_children);

	spa->spa_raidz_expand = NULL;
	raidvd->vdev_rz_expanding = B_FALSE;

	spa_async_request(spa, SPA_ASYNC_INITIALIZE_RESTART);
	spa_async_request(spa, SPA_ASYNC_TRIM_RESTART);
	spa_async_request(spa, SPA_ASYNC_AUTOTRIM_RESTART);

	spa_notify_waiters(spa);

	/*
	 * While we're in syncing context take the opportunity to
	 * setup a scrub. All the data has been sucessfully copied
	 * but we have not validated any checksums.
	 */
	setup_sync_arg_t setup_sync_arg = {
		.func = POOL_SCAN_SCRUB,
		.txgstart = 0,
		.txgend = 0,
	};
	if (zfs_scrub_after_expand &&
	    dsl_scan_setup_check(&setup_sync_arg.func, tx) == 0) {
		dsl_scan_setup_sync(&setup_sync_arg, tx);
	}
}

/*
 * State of one copy batch.
 */
typedef struct raidz_reflow_arg {
	vdev_raidz_expand_t *rra_vre;	/* Global expantion state. */
	zfs_locked_range_t *rra_lr;	/* Range lock of this batch. */
	uint64_t rra_txg;	/* TXG of this batch. */
	uint_t rra_ashift;	/* Ashift of the vdev. */
	uint32_t rra_tbd;	/* Number of in-flight ZIOs. */
	uint32_t rra_writes;	/* Number of write ZIOs. */
	zio_t *rra_zio[];	/* Write ZIO pointers. */
} raidz_reflow_arg_t;

/*
 * Write of the new location on one child is done.  Once all of them are done
 * we can unlock and free everything.
 */
static void
raidz_reflow_write_done(zio_t *zio)
{
	raidz_reflow_arg_t *rra = zio->io_private;
	vdev_raidz_expand_t *vre = rra->rra_vre;

	abd_free(zio->io_abd);

	mutex_enter(&vre->vre_lock);
	if (zio->io_error != 0) {
		/* Force a reflow pause on errors */
		vre->vre_failed_offset =
		    MIN(vre->vre_failed_offset, rra->rra_lr->lr_offset);
	}
	ASSERT3U(vre->vre_outstanding_bytes, >=, zio->io_size);
	vre->vre_outstanding_bytes -= zio->io_size;
	if (rra->rra_lr->lr_offset + rra->rra_lr->lr_length <
	    vre->vre_failed_offset) {
		vre->vre_bytes_copied_pertxg[rra->rra_txg & TXG_MASK] +=
		    zio->io_size;
	}
	cv_signal(&vre->vre_cv);
	boolean_t done = (--rra->rra_tbd == 0);
	mutex_exit(&vre->vre_lock);

	if (!done)
		return;
	spa_config_exit(zio->io_spa, SCL_STATE, zio->io_spa);
	zfs_rangelock_exit(rra->rra_lr);
	kmem_free(rra, sizeof (*rra) + sizeof (zio_t *) * rra->rra_writes);
}

/*
 * Read of the old location on one child is done.  Once all of them are done
 * writes should have all the data and we can issue them.
 */
static void
raidz_reflow_read_done(zio_t *zio)
{
	raidz_reflow_arg_t *rra = zio->io_private;
	vdev_raidz_expand_t *vre = rra->rra_vre;

	/* Reads of only one block use write ABDs.  For bigger free gangs. */
	if (zio->io_size > (1 << rra->rra_ashift))
		abd_free(zio->io_abd);

	/*
	 * If the read failed, or if it was done on a vdev that is not fully
	 * healthy (e.g. a child that has a resilver in progress), we may not
	 * have the correct data.  Note that it's OK if the write proceeds.
	 * It may write garbage but the location is otherwise unused and we
	 * will retry later due to vre_failed_offset.
	 */
	if (zio->io_error != 0 || !vdev_dtl_empty(zio->io_vd, DTL_MISSING)) {
		zfs_dbgmsg("reflow read failed off=%llu size=%llu txg=%llu "
		    "err=%u partial_dtl_empty=%u missing_dtl_empty=%u",
		    (long long)rra->rra_lr->lr_offset,
		    (long long)rra->rra_lr->lr_length,
		    (long long)rra->rra_txg,
		    zio->io_error,
		    vdev_dtl_empty(zio->io_vd, DTL_PARTIAL),
		    vdev_dtl_empty(zio->io_vd, DTL_MISSING));
		mutex_enter(&vre->vre_lock);
		/* Force a reflow pause on errors */
		vre->vre_failed_offset =
		    MIN(vre->vre_failed_offset, rra->rra_lr->lr_offset);
		mutex_exit(&vre->vre_lock);
	}

	if (atomic_dec_32_nv(&rra->rra_tbd) > 0)
		return;
	uint32_t writes = rra->rra_tbd = rra->rra_writes;
	for (uint64_t i = 0; i < writes; i++)
		zio_nowait(rra->rra_zio[i]);
}

static void
raidz_reflow_record_progress(vdev_raidz_expand_t *vre, uint64_t offset,
    dmu_tx_t *tx)
{
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;

	if (offset == 0)
		return;

	mutex_enter(&vre->vre_lock);
	ASSERT3U(vre->vre_offset, <=, offset);
	vre->vre_offset = offset;
	mutex_exit(&vre->vre_lock);

	if (vre->vre_offset_pertxg[txgoff] == 0) {
		dsl_sync_task_nowait(dmu_tx_pool(tx), raidz_reflow_sync,
		    spa, tx);
	}
	vre->vre_offset_pertxg[txgoff] = offset;
}

static boolean_t
vdev_raidz_expand_child_replacing(vdev_t *raidz_vd)
{
	for (int i = 0; i < raidz_vd->vdev_children; i++) {
		/* Quick check if a child is being replaced */
		if (!raidz_vd->vdev_child[i]->vdev_ops->vdev_op_leaf)
			return (B_TRUE);
	}
	return (B_FALSE);
}

static boolean_t
raidz_reflow_impl(vdev_t *vd, vdev_raidz_expand_t *vre, zfs_range_tree_t *rt,
    dmu_tx_t *tx)
{
	spa_t *spa = vd->vdev_spa;
	uint_t ashift = vd->vdev_top->vdev_ashift;

	zfs_range_seg_t *rs = zfs_range_tree_first(rt);
	if (rt == NULL)
		return (B_FALSE);
	uint64_t offset = zfs_rs_get_start(rs, rt);
	ASSERT(IS_P2ALIGNED(offset, 1 << ashift));
	uint64_t size = zfs_rs_get_end(rs, rt) - offset;
	ASSERT3U(size, >=, 1 << ashift);
	ASSERT(IS_P2ALIGNED(size, 1 << ashift));

	uint64_t blkid = offset >> ashift;
	uint_t old_children = vd->vdev_children - 1;

	/*
	 * We can only progress to the point that writes will not overlap
	 * with blocks whose progress has not yet been recorded on disk.
	 * Since partially-copied rows are still read from the old location,
	 * we need to stop one row before the sector-wise overlap, to prevent
	 * row-wise overlap.
	 *
	 * Note that even if we are skipping over a large unallocated region,
	 * we can't move the on-disk progress to `offset`, because concurrent
	 * writes/allocations could still use the currently-unallocated
	 * region.
	 */
	uint64_t ubsync_blkid =
	    RRSS_GET_OFFSET(&spa->spa_ubsync) >> ashift;
	uint64_t next_overwrite_blkid = ubsync_blkid +
	    ubsync_blkid / old_children - old_children;
	VERIFY3U(next_overwrite_blkid, >, ubsync_blkid);
	if (blkid >= next_overwrite_blkid) {
		raidz_reflow_record_progress(vre,
		    next_overwrite_blkid << ashift, tx);
		return (B_TRUE);
	}

	size = MIN(size, raidz_expand_max_copy_bytes);
	size = MIN(size, (uint64_t)old_children *
	    MIN(zfs_max_recordsize, SPA_MAXBLOCKSIZE));
	size = MAX(size, 1 << ashift);
	uint_t blocks = MIN(size >> ashift, next_overwrite_blkid - blkid);
	size = (uint64_t)blocks << ashift;

	zfs_range_tree_remove(rt, offset, size);

	uint_t reads = MIN(blocks, old_children);
	uint_t writes = MIN(blocks, vd->vdev_children);
	raidz_reflow_arg_t *rra = kmem_zalloc(sizeof (*rra) +
	    sizeof (zio_t *) * writes, KM_SLEEP);
	rra->rra_vre = vre;
	rra->rra_lr = zfs_rangelock_enter(&vre->vre_rangelock,
	    offset, size, RL_WRITER);
	rra->rra_txg = dmu_tx_get_txg(tx);
	rra->rra_ashift = ashift;
	rra->rra_tbd = reads;
	rra->rra_writes = writes;

	raidz_reflow_record_progress(vre, offset + size, tx);

	/*
	 * SCL_STATE will be released when the read and write are done,
	 * by raidz_reflow_write_done().
	 */
	spa_config_enter(spa, SCL_STATE, spa, RW_READER);

	/* check if a replacing vdev was added, if so treat it as an error */
	if (vdev_raidz_expand_child_replacing(vd)) {
		zfs_dbgmsg("replacing vdev encountered, reflow paused at "
		    "offset=%llu txg=%llu",
		    (long long)rra->rra_lr->lr_offset,
		    (long long)rra->rra_txg);

		mutex_enter(&vre->vre_lock);
		vre->vre_failed_offset =
		    MIN(vre->vre_failed_offset, rra->rra_lr->lr_offset);
		cv_signal(&vre->vre_cv);
		mutex_exit(&vre->vre_lock);

		/* drop everything we acquired */
		spa_config_exit(spa, SCL_STATE, spa);
		zfs_rangelock_exit(rra->rra_lr);
		kmem_free(rra, sizeof (*rra) + sizeof (zio_t *) * writes);
		return (B_TRUE);
	}

	mutex_enter(&vre->vre_lock);
	vre->vre_outstanding_bytes += size;
	mutex_exit(&vre->vre_lock);

	/* Allocate ABD and ZIO for each child we write. */
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	zio_t *pio = spa->spa_txg_zio[txgoff];
	uint_t b = blocks / vd->vdev_children;
	uint_t bb = blocks % vd->vdev_children;
	for (uint_t i = 0; i < writes; i++) {
		uint_t n = b + (i < bb);
		abd_t *abd = abd_alloc_for_io(n << ashift, B_FALSE);
		rra->rra_zio[i] = zio_vdev_child_io(pio, NULL,
		    vd->vdev_child[(blkid + i) % vd->vdev_children],
		    ((blkid + i) / vd->vdev_children) << ashift,
		    abd, n << ashift, ZIO_TYPE_WRITE, ZIO_PRIORITY_REMOVAL,
		    ZIO_FLAG_CANFAIL, raidz_reflow_write_done, rra);
	}

	/*
	 * Allocate and issue ZIO for each child we read.  For reads of only
	 * one block we can use respective writer ABDs, since they will also
	 * have only one block.  For bigger reads create gang ABDs and fill
	 * them with respective blocks from writer ABDs.
	 */
	b = blocks / old_children;
	bb = blocks % old_children;
	for (uint_t i = 0; i < reads; i++) {
		uint_t n = b + (i < bb);
		abd_t *abd;
		if (n > 1) {
			abd = abd_alloc_gang();
			for (uint_t j = 0; j < n; j++) {
				uint_t b = j * old_children + i;
				abd_t *cabd = abd_get_offset_size(
				    rra->rra_zio[b % vd->vdev_children]->io_abd,
				    (b / vd->vdev_children) << ashift,
				    1 << ashift);
				abd_gang_add(abd, cabd, B_TRUE);
			}
		} else {
			abd = rra->rra_zio[i]->io_abd;
		}
		zio_nowait(zio_vdev_child_io(pio, NULL,
		    vd->vdev_child[(blkid + i) % old_children],
		    ((blkid + i) / old_children) << ashift, abd,
		    n << ashift, ZIO_TYPE_READ, ZIO_PRIORITY_REMOVAL,
		    ZIO_FLAG_CANFAIL, raidz_reflow_read_done, rra));
	}

	return (B_FALSE);
}

/*
 * For testing (ztest specific)
 */
static void
raidz_expand_pause(uint_t pause_point)
{
	while (raidz_expand_pause_point != 0 &&
	    raidz_expand_pause_point <= pause_point)
		delay(hz);
}

static void
raidz_scratch_child_done(zio_t *zio)
{
	zio_t *pio = zio->io_private;

	mutex_enter(&pio->io_lock);
	pio->io_error = zio_worst_error(pio->io_error, zio->io_error);
	mutex_exit(&pio->io_lock);
}

/*
 * Reflow the beginning portion of the vdev into an intermediate scratch area
 * in memory and on disk. This operation must be persisted on disk before we
 * proceed to overwrite the beginning portion with the reflowed data.
 *
 * This multi-step task can fail to complete if disk errors are encountered
 * and we can return here after a pause (waiting for disk to become healthy).
 */
static void
raidz_reflow_scratch_sync(void *arg, dmu_tx_t *tx)
{
	vdev_raidz_expand_t *vre = arg;
	spa_t *spa = dmu_tx_pool(tx)->dp_spa;
	zio_t *pio;
	int error;

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	vdev_t *raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);
	int ashift = raidvd->vdev_ashift;
	uint64_t write_size = P2ALIGN_TYPED(VDEV_BOOT_SIZE, 1 << ashift,
	    uint64_t);
	uint64_t logical_size = write_size * raidvd->vdev_children;
	uint64_t read_size =
	    P2ROUNDUP(DIV_ROUND_UP(logical_size, (raidvd->vdev_children - 1)),
	    1 << ashift);

	/*
	 * The scratch space must be large enough to get us to the point
	 * that one row does not overlap itself when moved.  This is checked
	 * by vdev_raidz_attach_check().
	 */
	VERIFY3U(write_size, >=, raidvd->vdev_children << ashift);
	VERIFY3U(write_size, <=, VDEV_BOOT_SIZE);
	VERIFY3U(write_size, <=, read_size);

	zfs_locked_range_t *lr = zfs_rangelock_enter(&vre->vre_rangelock,
	    0, logical_size, RL_WRITER);

	abd_t **abds = kmem_alloc(raidvd->vdev_children * sizeof (abd_t *),
	    KM_SLEEP);
	for (int i = 0; i < raidvd->vdev_children; i++) {
		abds[i] = abd_alloc_linear(read_size, B_FALSE);
	}

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_PRE_SCRATCH_1);

	/*
	 * If we have already written the scratch area then we must read from
	 * there, since new writes were redirected there while we were paused
	 * or the original location may have been partially overwritten with
	 * reflowed data.
	 */
	if (RRSS_GET_STATE(&spa->spa_ubsync) == RRSS_SCRATCH_VALID) {
		VERIFY3U(RRSS_GET_OFFSET(&spa->spa_ubsync), ==, logical_size);
		/*
		 * Read from scratch space.
		 */
		pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
		for (int i = 0; i < raidvd->vdev_children; i++) {
			/*
			 * Note: zio_vdev_child_io() adds VDEV_LABEL_START_SIZE
			 * to the offset to calculate the physical offset to
			 * write to.  Passing in a negative offset makes us
			 * access the scratch area.
			 */
			zio_nowait(zio_vdev_child_io(pio, NULL,
			    raidvd->vdev_child[i],
			    VDEV_BOOT_OFFSET - VDEV_LABEL_START_SIZE, abds[i],
			    write_size, ZIO_TYPE_READ, ZIO_PRIORITY_REMOVAL,
			    ZIO_FLAG_CANFAIL, raidz_scratch_child_done, pio));
		}
		error = zio_wait(pio);
		if (error != 0) {
			zfs_dbgmsg("reflow: error %d reading scratch location",
			    error);
			goto io_error_exit;
		}
		goto overwrite;
	}

	/*
	 * Read from original location.
	 */
	pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	for (int i = 0; i < raidvd->vdev_children - 1; i++) {
		ASSERT0(vdev_is_dead(raidvd->vdev_child[i]));
		zio_nowait(zio_vdev_child_io(pio, NULL, raidvd->vdev_child[i],
		    0, abds[i], read_size, ZIO_TYPE_READ,
		    ZIO_PRIORITY_REMOVAL, ZIO_FLAG_CANFAIL,
		    raidz_scratch_child_done, pio));
	}
	error = zio_wait(pio);
	if (error != 0) {
		zfs_dbgmsg("reflow: error %d reading original location", error);
io_error_exit:
		for (int i = 0; i < raidvd->vdev_children; i++)
			abd_free(abds[i]);
		kmem_free(abds, raidvd->vdev_children * sizeof (abd_t *));
		zfs_rangelock_exit(lr);
		spa_config_exit(spa, SCL_STATE, FTAG);
		return;
	}

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_PRE_SCRATCH_2);

	/*
	 * Reflow in memory.
	 */
	uint64_t logical_sectors = logical_size >> ashift;
	for (int i = raidvd->vdev_children - 1; i < logical_sectors; i++) {
		int oldchild = i % (raidvd->vdev_children - 1);
		uint64_t oldoff = (i / (raidvd->vdev_children - 1)) << ashift;

		int newchild = i % raidvd->vdev_children;
		uint64_t newoff = (i / raidvd->vdev_children) << ashift;

		/* a single sector should not be copying over itself */
		ASSERT(!(newchild == oldchild && newoff == oldoff));

		abd_copy_off(abds[newchild], abds[oldchild],
		    newoff, oldoff, 1 << ashift);
	}

	/*
	 * Verify that we filled in everything we intended to (write_size on
	 * each child).
	 */
	VERIFY0(logical_sectors % raidvd->vdev_children);
	VERIFY3U((logical_sectors / raidvd->vdev_children) << ashift, ==,
	    write_size);

	/*
	 * Write to scratch location (boot area).
	 */
	pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	for (int i = 0; i < raidvd->vdev_children; i++) {
		/*
		 * Note: zio_vdev_child_io() adds VDEV_LABEL_START_SIZE to
		 * the offset to calculate the physical offset to write to.
		 * Passing in a negative offset lets us access the boot area.
		 */
		zio_nowait(zio_vdev_child_io(pio, NULL, raidvd->vdev_child[i],
		    VDEV_BOOT_OFFSET - VDEV_LABEL_START_SIZE, abds[i],
		    write_size, ZIO_TYPE_WRITE, ZIO_PRIORITY_REMOVAL,
		    ZIO_FLAG_CANFAIL, raidz_scratch_child_done, pio));
	}
	error = zio_wait(pio);
	if (error != 0) {
		zfs_dbgmsg("reflow: error %d writing scratch location", error);
		goto io_error_exit;
	}
	pio = zio_root(spa, NULL, NULL, 0);
	zio_flush(pio, raidvd);
	zio_wait(pio);

	zfs_dbgmsg("reflow: wrote %llu bytes (logical) to scratch area",
	    (long long)logical_size);

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_PRE_SCRATCH_3);

	/*
	 * Update uberblock to indicate that scratch space is valid.  This is
	 * needed because after this point, the real location may be
	 * overwritten.  If we crash, we need to get the data from the
	 * scratch space, rather than the real location.
	 *
	 * Note: ub_timestamp is bumped so that vdev_uberblock_compare()
	 * will prefer this uberblock.
	 */
	RAIDZ_REFLOW_SET(&spa->spa_ubsync, RRSS_SCRATCH_VALID, logical_size);
	spa->spa_ubsync.ub_timestamp++;
	ASSERT0(vdev_uberblock_sync_list(&spa->spa_root_vdev, 1,
	    &spa->spa_ubsync, ZIO_FLAG_CONFIG_WRITER));
	if (spa_multihost(spa))
		mmp_update_uberblock(spa, &spa->spa_ubsync);

	zfs_dbgmsg("reflow: uberblock updated "
	    "(txg %llu, SCRATCH_VALID, size %llu, ts %llu)",
	    (long long)spa->spa_ubsync.ub_txg,
	    (long long)logical_size,
	    (long long)spa->spa_ubsync.ub_timestamp);

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_SCRATCH_VALID);

	/*
	 * Overwrite with reflow'ed data.
	 */
overwrite:
	pio = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
	for (int i = 0; i < raidvd->vdev_children; i++) {
		zio_nowait(zio_vdev_child_io(pio, NULL, raidvd->vdev_child[i],
		    0, abds[i], write_size, ZIO_TYPE_WRITE,
		    ZIO_PRIORITY_REMOVAL, ZIO_FLAG_CANFAIL,
		    raidz_scratch_child_done, pio));
	}
	error = zio_wait(pio);
	if (error != 0) {
		/*
		 * When we exit early here and drop the range lock, new
		 * writes will go into the scratch area so we'll need to
		 * read from there when we return after pausing.
		 */
		zfs_dbgmsg("reflow: error %d writing real location", error);
		/*
		 * Update the uberblock that is written when this txg completes.
		 */
		RAIDZ_REFLOW_SET(&spa->spa_uberblock, RRSS_SCRATCH_VALID,
		    logical_size);
		goto io_error_exit;
	}
	pio = zio_root(spa, NULL, NULL, 0);
	zio_flush(pio, raidvd);
	zio_wait(pio);

	zfs_dbgmsg("reflow: overwrote %llu bytes (logical) to real location",
	    (long long)logical_size);
	for (int i = 0; i < raidvd->vdev_children; i++)
		abd_free(abds[i]);
	kmem_free(abds, raidvd->vdev_children * sizeof (abd_t *));

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_SCRATCH_REFLOWED);

	/*
	 * Update uberblock to indicate that the initial part has been
	 * reflow'ed.  This is needed because after this point (when we exit
	 * the rangelock), we allow regular writes to this region, which will
	 * be written to the new location only (because reflow_offset_next ==
	 * reflow_offset_synced).  If we crashed and re-copied from the
	 * scratch space, we would lose the regular writes.
	 */
	RAIDZ_REFLOW_SET(&spa->spa_ubsync, RRSS_SCRATCH_INVALID_SYNCED,
	    logical_size);
	spa->spa_ubsync.ub_timestamp++;
	ASSERT0(vdev_uberblock_sync_list(&spa->spa_root_vdev, 1,
	    &spa->spa_ubsync, ZIO_FLAG_CONFIG_WRITER));
	if (spa_multihost(spa))
		mmp_update_uberblock(spa, &spa->spa_ubsync);

	zfs_dbgmsg("reflow: uberblock updated "
	    "(txg %llu, SCRATCH_NOT_IN_USE, size %llu, ts %llu)",
	    (long long)spa->spa_ubsync.ub_txg,
	    (long long)logical_size,
	    (long long)spa->spa_ubsync.ub_timestamp);

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_SCRATCH_POST_REFLOW_1);

	/*
	 * Update progress.
	 */
	vre->vre_offset = logical_size;
	zfs_rangelock_exit(lr);
	spa_config_exit(spa, SCL_STATE, FTAG);

	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	vre->vre_offset_pertxg[txgoff] = vre->vre_offset;
	vre->vre_bytes_copied_pertxg[txgoff] = vre->vre_bytes_copied;
	/*
	 * Note - raidz_reflow_sync() will update the uberblock state to
	 * RRSS_SCRATCH_INVALID_SYNCED_REFLOW
	 */
	raidz_reflow_sync(spa, tx);

	raidz_expand_pause(RAIDZ_EXPAND_PAUSE_SCRATCH_POST_REFLOW_2);
}

/*
 * We crashed in the middle of raidz_reflow_scratch_sync(); complete its work
 * here.  No other i/o can be in progress, so we don't need the vre_rangelock.
 */
void
vdev_raidz_reflow_copy_scratch(spa_t *spa)
{
	vdev_raidz_expand_t *vre = spa->spa_raidz_expand;
	uint64_t logical_size = RRSS_GET_OFFSET(&spa->spa_uberblock);
	ASSERT3U(RRSS_GET_STATE(&spa->spa_uberblock), ==, RRSS_SCRATCH_VALID);

	spa_config_enter(spa, SCL_STATE, FTAG, RW_READER);
	vdev_t *raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);
	ASSERT0(logical_size % raidvd->vdev_children);
	uint64_t write_size = logical_size / raidvd->vdev_children;

	zio_t *pio;

	/*
	 * Read from scratch space.
	 */
	abd_t **abds = kmem_alloc(raidvd->vdev_children * sizeof (abd_t *),
	    KM_SLEEP);
	for (int i = 0; i < raidvd->vdev_children; i++) {
		abds[i] = abd_alloc_linear(write_size, B_FALSE);
	}

	pio = zio_root(spa, NULL, NULL, 0);
	for (int i = 0; i < raidvd->vdev_children; i++) {
		/*
		 * Note: zio_vdev_child_io() adds VDEV_LABEL_START_SIZE to
		 * the offset to calculate the physical offset to write to.
		 * Passing in a negative offset lets us access the boot area.
		 */
		zio_nowait(zio_vdev_child_io(pio, NULL, raidvd->vdev_child[i],
		    VDEV_BOOT_OFFSET - VDEV_LABEL_START_SIZE, abds[i],
		    write_size, ZIO_TYPE_READ, ZIO_PRIORITY_REMOVAL, 0,
		    raidz_scratch_child_done, pio));
	}
	zio_wait(pio);

	/*
	 * Overwrite real location with reflow'ed data.
	 */
	pio = zio_root(spa, NULL, NULL, 0);
	for (int i = 0; i < raidvd->vdev_children; i++) {
		zio_nowait(zio_vdev_child_io(pio, NULL, raidvd->vdev_child[i],
		    0, abds[i], write_size, ZIO_TYPE_WRITE,
		    ZIO_PRIORITY_REMOVAL, 0,
		    raidz_scratch_child_done, pio));
	}
	zio_wait(pio);
	pio = zio_root(spa, NULL, NULL, 0);
	zio_flush(pio, raidvd);
	zio_wait(pio);

	zfs_dbgmsg("reflow recovery: overwrote %llu bytes (logical) "
	    "to real location", (long long)logical_size);

	for (int i = 0; i < raidvd->vdev_children; i++)
		abd_free(abds[i]);
	kmem_free(abds, raidvd->vdev_children * sizeof (abd_t *));

	/*
	 * Update uberblock.
	 */
	RAIDZ_REFLOW_SET(&spa->spa_ubsync,
	    RRSS_SCRATCH_INVALID_SYNCED_ON_IMPORT, logical_size);
	spa->spa_ubsync.ub_timestamp++;
	VERIFY0(vdev_uberblock_sync_list(&spa->spa_root_vdev, 1,
	    &spa->spa_ubsync, ZIO_FLAG_CONFIG_WRITER));
	if (spa_multihost(spa))
		mmp_update_uberblock(spa, &spa->spa_ubsync);

	zfs_dbgmsg("reflow recovery: uberblock updated "
	    "(txg %llu, SCRATCH_NOT_IN_USE, size %llu, ts %llu)",
	    (long long)spa->spa_ubsync.ub_txg,
	    (long long)logical_size,
	    (long long)spa->spa_ubsync.ub_timestamp);

	dmu_tx_t *tx = dmu_tx_create_assigned(spa->spa_dsl_pool,
	    spa_first_txg(spa));
	int txgoff = dmu_tx_get_txg(tx) & TXG_MASK;
	vre->vre_offset = logical_size;
	vre->vre_offset_pertxg[txgoff] = vre->vre_offset;
	vre->vre_bytes_copied_pertxg[txgoff] = vre->vre_bytes_copied;
	/*
	 * Note that raidz_reflow_sync() will update the uberblock once more
	 */
	raidz_reflow_sync(spa, tx);

	dmu_tx_commit(tx);

	spa_config_exit(spa, SCL_STATE, FTAG);
}

static boolean_t
spa_raidz_expand_thread_check(void *arg, zthr_t *zthr)
{
	(void) zthr;
	spa_t *spa = arg;

	return (spa->spa_raidz_expand != NULL &&
	    !spa->spa_raidz_expand->vre_waiting_for_resilver);
}

/*
 * RAIDZ expansion background thread
 *
 * Can be called multiple times if the reflow is paused
 */
static void
spa_raidz_expand_thread(void *arg, zthr_t *zthr)
{
	spa_t *spa = arg;
	vdev_raidz_expand_t *vre = spa->spa_raidz_expand;

	if (RRSS_GET_STATE(&spa->spa_ubsync) == RRSS_SCRATCH_VALID)
		vre->vre_offset = 0;
	else
		vre->vre_offset = RRSS_GET_OFFSET(&spa->spa_ubsync);

	/* Reflow the begining portion using the scratch area */
	if (vre->vre_offset == 0) {
		VERIFY0(dsl_sync_task(spa_name(spa),
		    NULL, raidz_reflow_scratch_sync,
		    vre, 0, ZFS_SPACE_CHECK_NONE));

		/* if we encountered errors then pause */
		if (vre->vre_offset == 0) {
			mutex_enter(&vre->vre_lock);
			vre->vre_waiting_for_resilver = B_TRUE;
			mutex_exit(&vre->vre_lock);
			return;
		}
	}

	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	vdev_t *raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);

	uint64_t guid = raidvd->vdev_guid;

	/* Iterate over all the remaining metaslabs */
	for (uint64_t i = vre->vre_offset >> raidvd->vdev_ms_shift;
	    i < raidvd->vdev_ms_count &&
	    !zthr_iscancelled(zthr) &&
	    vre->vre_failed_offset == UINT64_MAX; i++) {
		metaslab_t *msp = raidvd->vdev_ms[i];

		metaslab_disable(msp);
		mutex_enter(&msp->ms_lock);

		/*
		 * The metaslab may be newly created (for the expanded
		 * space), in which case its trees won't exist yet,
		 * so we need to bail out early.
		 */
		if (msp->ms_new) {
			mutex_exit(&msp->ms_lock);
			metaslab_enable(msp, B_FALSE, B_FALSE);
			continue;
		}

		VERIFY0(metaslab_load(msp));

		/*
		 * We want to copy everything except the free (allocatable)
		 * space.  Note that there may be a little bit more free
		 * space (e.g. in ms_defer), and it's fine to copy that too.
		 */
		uint64_t shift, start;
		zfs_range_seg_type_t type = metaslab_calculate_range_tree_type(
		    raidvd, msp, &start, &shift);
		zfs_range_tree_t *rt = zfs_range_tree_create(NULL, type, NULL,
		    start, shift);
		zfs_range_tree_add(rt, msp->ms_start, msp->ms_size);
		zfs_range_tree_walk(msp->ms_allocatable, zfs_range_tree_remove,
		    rt);
		mutex_exit(&msp->ms_lock);

		/*
		 * Force the last sector of each metaslab to be copied.  This
		 * ensures that we advance the on-disk progress to the end of
		 * this metaslab while the metaslab is disabled.  Otherwise, we
		 * could move past this metaslab without advancing the on-disk
		 * progress, and then an allocation to this metaslab would not
		 * be copied.
		 */
		int sectorsz = 1 << raidvd->vdev_ashift;
		uint64_t ms_last_offset = msp->ms_start +
		    msp->ms_size - sectorsz;
		if (!zfs_range_tree_contains(rt, ms_last_offset, sectorsz)) {
			zfs_range_tree_add(rt, ms_last_offset, sectorsz);
		}

		/*
		 * When we are resuming from a paused expansion (i.e.
		 * when importing a pool with a expansion in progress),
		 * discard any state that we have already processed.
		 */
		if (vre->vre_offset > msp->ms_start) {
			zfs_range_tree_clear(rt, msp->ms_start,
			    vre->vre_offset - msp->ms_start);
		}

		while (!zthr_iscancelled(zthr) &&
		    !zfs_range_tree_is_empty(rt) &&
		    vre->vre_failed_offset == UINT64_MAX) {

			/*
			 * We need to periodically drop the config lock so that
			 * writers can get in.  Additionally, we can't wait
			 * for a txg to sync while holding a config lock
			 * (since a waiting writer could cause a 3-way deadlock
			 * with the sync thread, which also gets a config
			 * lock for reader).  So we can't hold the config lock
			 * while calling dmu_tx_assign().
			 */
			spa_config_exit(spa, SCL_CONFIG, FTAG);

			/*
			 * If requested, pause the reflow when the amount
			 * specified by raidz_expand_max_reflow_bytes is reached
			 *
			 * This pause is only used during testing or debugging.
			 */
			while (raidz_expand_max_reflow_bytes != 0 &&
			    raidz_expand_max_reflow_bytes <=
			    vre->vre_bytes_copied && !zthr_iscancelled(zthr)) {
				delay(hz);
			}

			mutex_enter(&vre->vre_lock);
			while (vre->vre_outstanding_bytes >
			    raidz_expand_max_copy_bytes) {
				cv_wait(&vre->vre_cv, &vre->vre_lock);
			}
			mutex_exit(&vre->vre_lock);

			dmu_tx_t *tx =
			    dmu_tx_create_dd(spa_get_dsl(spa)->dp_mos_dir);

			VERIFY0(dmu_tx_assign(tx, TXG_WAIT));
			uint64_t txg = dmu_tx_get_txg(tx);

			/*
			 * Reacquire the vdev_config lock.  Theoretically, the
			 * vdev_t that we're expanding may have changed.
			 */
			spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
			raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);

			boolean_t needsync =
			    raidz_reflow_impl(raidvd, vre, rt, tx);

			dmu_tx_commit(tx);

			if (needsync) {
				spa_config_exit(spa, SCL_CONFIG, FTAG);
				txg_wait_synced(spa->spa_dsl_pool, txg);
				spa_config_enter(spa, SCL_CONFIG, FTAG,
				    RW_READER);
			}
		}

		spa_config_exit(spa, SCL_CONFIG, FTAG);

		metaslab_enable(msp, B_FALSE, B_FALSE);
		zfs_range_tree_vacate(rt, NULL, NULL);
		zfs_range_tree_destroy(rt);

		spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
		raidvd = vdev_lookup_top(spa, vre->vre_vdev_id);
	}

	spa_config_exit(spa, SCL_CONFIG, FTAG);

	/*
	 * The txg_wait_synced() here ensures that all reflow zio's have
	 * completed, and vre_failed_offset has been set if necessary.  It
	 * also ensures that the progress of the last raidz_reflow_sync() is
	 * written to disk before raidz_reflow_complete_sync() changes the
	 * in-memory vre_state.  vdev_raidz_io_start() uses vre_state to
	 * determine if a reflow is in progress, in which case we may need to
	 * write to both old and new locations.  Therefore we can only change
	 * vre_state once this is not necessary, which is once the on-disk
	 * progress (in spa_ubsync) has been set past any possible writes (to
	 * the end of the last metaslab).
	 */
	txg_wait_synced(spa->spa_dsl_pool, 0);

	if (!zthr_iscancelled(zthr) &&
	    vre->vre_offset == raidvd->vdev_ms_count << raidvd->vdev_ms_shift) {
		/*
		 * We are not being canceled or paused, so the reflow must be
		 * complete. In that case also mark it as completed on disk.
		 */
		ASSERT3U(vre->vre_failed_offset, ==, UINT64_MAX);
		VERIFY0(dsl_sync_task(spa_name(spa), NULL,
		    raidz_reflow_complete_sync, spa,
		    0, ZFS_SPACE_CHECK_NONE));
		(void) vdev_online(spa, guid, ZFS_ONLINE_EXPAND, NULL);
	} else {
		/*
		 * Wait for all copy zio's to complete and for all the
		 * raidz_reflow_sync() synctasks to be run.
		 */
		spa_history_log_internal(spa, "reflow pause",
		    NULL, "offset=%llu failed_offset=%lld",
		    (long long)vre->vre_offset,
		    (long long)vre->vre_failed_offset);
		mutex_enter(&vre->vre_lock);
		if (vre->vre_failed_offset != UINT64_MAX) {
			/*
			 * Reset progress so that we will retry everything
			 * after the point that something failed.
			 */
			vre->vre_offset = vre->vre_failed_offset;
			vre->vre_failed_offset = UINT64_MAX;
			vre->vre_waiting_for_resilver = B_TRUE;
		}
		mutex_exit(&vre->vre_lock);
	}
}

void
spa_start_raidz_expansion_thread(spa_t *spa)
{
	ASSERT3P(spa->spa_raidz_expand_zthr, ==, NULL);
	spa->spa_raidz_expand_zthr = zthr_create("raidz_expand",
	    spa_raidz_expand_thread_check, spa_raidz_expand_thread,
	    spa, defclsyspri);
}

void
raidz_dtl_reassessed(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	if (spa->spa_raidz_expand != NULL) {
		vdev_raidz_expand_t *vre = spa->spa_raidz_expand;
		/*
		 * we get called often from vdev_dtl_reassess() so make
		 * sure it's our vdev and any replacing is complete
		 */
		if (vd->vdev_top->vdev_id == vre->vre_vdev_id &&
		    !vdev_raidz_expand_child_replacing(vd->vdev_top)) {
			mutex_enter(&vre->vre_lock);
			if (vre->vre_waiting_for_resilver) {
				vdev_dbgmsg(vd, "DTL reassessed, "
				    "continuing raidz expansion");
				vre->vre_waiting_for_resilver = B_FALSE;
				zthr_wakeup(spa->spa_raidz_expand_zthr);
			}
			mutex_exit(&vre->vre_lock);
		}
	}
}

int
vdev_raidz_attach_check(vdev_t *new_child)
{
	vdev_t *raidvd = new_child->vdev_parent;
	uint64_t new_children = raidvd->vdev_children;

	/*
	 * We use the "boot" space as scratch space to handle overwriting the
	 * initial part of the vdev.  If it is too small, then this expansion
	 * is not allowed.  This would be very unusual (e.g. ashift > 13 and
	 * >200 children).
	 */
	if (new_children << raidvd->vdev_ashift > VDEV_BOOT_SIZE) {
		return (EINVAL);
	}
	return (0);
}

void
vdev_raidz_attach_sync(void *arg, dmu_tx_t *tx)
{
	vdev_t *new_child = arg;
	spa_t *spa = new_child->vdev_spa;
	vdev_t *raidvd = new_child->vdev_parent;
	vdev_raidz_t *vdrz = raidvd->vdev_tsd;
	ASSERT3P(raidvd->vdev_ops, ==, &vdev_raidz_ops);
	ASSERT3P(raidvd->vdev_top, ==, raidvd);
	ASSERT3U(raidvd->vdev_children, >, vdrz->vd_original_width);
	ASSERT3U(raidvd->vdev_children, ==, vdrz->vd_physical_width + 1);
	ASSERT3P(raidvd->vdev_child[raidvd->vdev_children - 1], ==,
	    new_child);

	spa_feature_incr(spa, SPA_FEATURE_RAIDZ_EXPANSION, tx);

	vdrz->vd_physical_width++;

	VERIFY0(spa->spa_uberblock.ub_raidz_reflow_info);
	vdrz->vn_vre.vre_vdev_id = raidvd->vdev_id;
	vdrz->vn_vre.vre_offset = 0;
	vdrz->vn_vre.vre_failed_offset = UINT64_MAX;
	spa->spa_raidz_expand = &vdrz->vn_vre;
	zthr_wakeup(spa->spa_raidz_expand_zthr);

	/*
	 * Dirty the config so that ZPOOL_CONFIG_RAIDZ_EXPANDING will get
	 * written to the config.
	 */
	vdev_config_dirty(raidvd);

	vdrz->vn_vre.vre_start_time = gethrestime_sec();
	vdrz->vn_vre.vre_end_time = 0;
	vdrz->vn_vre.vre_state = DSS_SCANNING;
	vdrz->vn_vre.vre_bytes_copied = 0;

	uint64_t state = vdrz->vn_vre.vre_state;
	VERIFY0(zap_update(spa->spa_meta_objset,
	    raidvd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_STATE,
	    sizeof (state), 1, &state, tx));

	uint64_t start_time = vdrz->vn_vre.vre_start_time;
	VERIFY0(zap_update(spa->spa_meta_objset,
	    raidvd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_START_TIME,
	    sizeof (start_time), 1, &start_time, tx));

	(void) zap_remove(spa->spa_meta_objset,
	    raidvd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_END_TIME, tx);
	(void) zap_remove(spa->spa_meta_objset,
	    raidvd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_BYTES_COPIED, tx);

	spa_history_log_internal(spa, "raidz vdev expansion started",  tx,
	    "%s vdev %llu new width %llu", spa_name(spa),
	    (unsigned long long)raidvd->vdev_id,
	    (unsigned long long)raidvd->vdev_children);
}

int
vdev_raidz_load(vdev_t *vd)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	int err;

	uint64_t state = DSS_NONE;
	uint64_t start_time = 0;
	uint64_t end_time = 0;
	uint64_t bytes_copied = 0;

	if (vd->vdev_top_zap != 0) {
		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_STATE,
		    sizeof (state), 1, &state);
		if (err != 0 && err != ENOENT)
			return (err);

		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_START_TIME,
		    sizeof (start_time), 1, &start_time);
		if (err != 0 && err != ENOENT)
			return (err);

		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_END_TIME,
		    sizeof (end_time), 1, &end_time);
		if (err != 0 && err != ENOENT)
			return (err);

		err = zap_lookup(vd->vdev_spa->spa_meta_objset,
		    vd->vdev_top_zap, VDEV_TOP_ZAP_RAIDZ_EXPAND_BYTES_COPIED,
		    sizeof (bytes_copied), 1, &bytes_copied);
		if (err != 0 && err != ENOENT)
			return (err);
	}

	/*
	 * If we are in the middle of expansion, vre_state should have
	 * already been set by vdev_raidz_init().
	 */
	EQUIV(vdrz->vn_vre.vre_state == DSS_SCANNING, state == DSS_SCANNING);
	vdrz->vn_vre.vre_state = (dsl_scan_state_t)state;
	vdrz->vn_vre.vre_start_time = start_time;
	vdrz->vn_vre.vre_end_time = end_time;
	vdrz->vn_vre.vre_bytes_copied = bytes_copied;

	return (0);
}

int
spa_raidz_expand_get_stats(spa_t *spa, pool_raidz_expand_stat_t *pres)
{
	vdev_raidz_expand_t *vre = spa->spa_raidz_expand;

	if (vre == NULL) {
		/* no removal in progress; find most recent completed */
		for (int c = 0; c < spa->spa_root_vdev->vdev_children; c++) {
			vdev_t *vd = spa->spa_root_vdev->vdev_child[c];
			if (vd->vdev_ops == &vdev_raidz_ops) {
				vdev_raidz_t *vdrz = vd->vdev_tsd;

				if (vdrz->vn_vre.vre_end_time != 0 &&
				    (vre == NULL ||
				    vdrz->vn_vre.vre_end_time >
				    vre->vre_end_time)) {
					vre = &vdrz->vn_vre;
				}
			}
		}
	}

	if (vre == NULL) {
		return (SET_ERROR(ENOENT));
	}

	pres->pres_state = vre->vre_state;
	pres->pres_expanding_vdev = vre->vre_vdev_id;

	vdev_t *vd = vdev_lookup_top(spa, vre->vre_vdev_id);
	pres->pres_to_reflow = vd->vdev_stat.vs_alloc;

	mutex_enter(&vre->vre_lock);
	pres->pres_reflowed = vre->vre_bytes_copied;
	for (int i = 0; i < TXG_SIZE; i++)
		pres->pres_reflowed += vre->vre_bytes_copied_pertxg[i];
	mutex_exit(&vre->vre_lock);

	pres->pres_start_time = vre->vre_start_time;
	pres->pres_end_time = vre->vre_end_time;
	pres->pres_waiting_for_resilver = vre->vre_waiting_for_resilver;

	return (0);
}

/*
 * Initialize private RAIDZ specific fields from the nvlist.
 */
static int
vdev_raidz_init(spa_t *spa, nvlist_t *nv, void **tsd)
{
	uint_t children;
	nvlist_t **child;
	int error = nvlist_lookup_nvlist_array(nv,
	    ZPOOL_CONFIG_CHILDREN, &child, &children);
	if (error != 0)
		return (SET_ERROR(EINVAL));

	uint64_t nparity;
	if (nvlist_lookup_uint64(nv, ZPOOL_CONFIG_NPARITY, &nparity) == 0) {
		if (nparity == 0 || nparity > VDEV_RAIDZ_MAXPARITY)
			return (SET_ERROR(EINVAL));

		/*
		 * Previous versions could only support 1 or 2 parity
		 * device.
		 */
		if (nparity > 1 && spa_version(spa) < SPA_VERSION_RAIDZ2)
			return (SET_ERROR(EINVAL));
		else if (nparity > 2 && spa_version(spa) < SPA_VERSION_RAIDZ3)
			return (SET_ERROR(EINVAL));
	} else {
		/*
		 * We require the parity to be specified for SPAs that
		 * support multiple parity levels.
		 */
		if (spa_version(spa) >= SPA_VERSION_RAIDZ2)
			return (SET_ERROR(EINVAL));

		/*
		 * Otherwise, we default to 1 parity device for RAID-Z.
		 */
		nparity = 1;
	}

	vdev_raidz_t *vdrz = kmem_zalloc(sizeof (*vdrz), KM_SLEEP);
	vdrz->vn_vre.vre_vdev_id = -1;
	vdrz->vn_vre.vre_offset = UINT64_MAX;
	vdrz->vn_vre.vre_failed_offset = UINT64_MAX;
	mutex_init(&vdrz->vn_vre.vre_lock, NULL, MUTEX_DEFAULT, NULL);
	cv_init(&vdrz->vn_vre.vre_cv, NULL, CV_DEFAULT, NULL);
	zfs_rangelock_init(&vdrz->vn_vre.vre_rangelock, NULL, NULL);
	mutex_init(&vdrz->vd_expand_lock, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&vdrz->vd_expand_txgs, vdev_raidz_reflow_compare,
	    sizeof (reflow_node_t), offsetof(reflow_node_t, re_link));

	vdrz->vd_physical_width = children;
	vdrz->vd_nparity = nparity;

	/* note, the ID does not exist when creating a pool */
	(void) nvlist_lookup_uint64(nv, ZPOOL_CONFIG_ID,
	    &vdrz->vn_vre.vre_vdev_id);

	boolean_t reflow_in_progress =
	    nvlist_exists(nv, ZPOOL_CONFIG_RAIDZ_EXPANDING);
	if (reflow_in_progress) {
		spa->spa_raidz_expand = &vdrz->vn_vre;
		vdrz->vn_vre.vre_state = DSS_SCANNING;
	}

	vdrz->vd_original_width = children;
	uint64_t *txgs;
	unsigned int txgs_size = 0;
	error = nvlist_lookup_uint64_array(nv, ZPOOL_CONFIG_RAIDZ_EXPAND_TXGS,
	    &txgs, &txgs_size);
	if (error == 0) {
		for (int i = 0; i < txgs_size; i++) {
			reflow_node_t *re = kmem_zalloc(sizeof (*re), KM_SLEEP);
			re->re_txg = txgs[txgs_size - i - 1];
			re->re_logical_width = vdrz->vd_physical_width - i;

			if (reflow_in_progress)
				re->re_logical_width--;

			avl_add(&vdrz->vd_expand_txgs, re);
		}

		vdrz->vd_original_width = vdrz->vd_physical_width - txgs_size;
	}
	if (reflow_in_progress) {
		vdrz->vd_original_width--;
		zfs_dbgmsg("reflow_in_progress, %u wide, %d prior expansions",
		    children, txgs_size);
	}

	*tsd = vdrz;

	return (0);
}

static void
vdev_raidz_fini(vdev_t *vd)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	if (vd->vdev_spa->spa_raidz_expand == &vdrz->vn_vre)
		vd->vdev_spa->spa_raidz_expand = NULL;
	reflow_node_t *re;
	void *cookie = NULL;
	avl_tree_t *tree = &vdrz->vd_expand_txgs;
	while ((re = avl_destroy_nodes(tree, &cookie)) != NULL)
		kmem_free(re, sizeof (*re));
	avl_destroy(&vdrz->vd_expand_txgs);
	mutex_destroy(&vdrz->vd_expand_lock);
	mutex_destroy(&vdrz->vn_vre.vre_lock);
	cv_destroy(&vdrz->vn_vre.vre_cv);
	zfs_rangelock_fini(&vdrz->vn_vre.vre_rangelock);
	kmem_free(vdrz, sizeof (*vdrz));
}

/*
 * Add RAIDZ specific fields to the config nvlist.
 */
static void
vdev_raidz_config_generate(vdev_t *vd, nvlist_t *nv)
{
	ASSERT3P(vd->vdev_ops, ==, &vdev_raidz_ops);
	vdev_raidz_t *vdrz = vd->vdev_tsd;

	/*
	 * Make sure someone hasn't managed to sneak a fancy new vdev
	 * into a crufty old storage pool.
	 */
	ASSERT(vdrz->vd_nparity == 1 ||
	    (vdrz->vd_nparity <= 2 &&
	    spa_version(vd->vdev_spa) >= SPA_VERSION_RAIDZ2) ||
	    (vdrz->vd_nparity <= 3 &&
	    spa_version(vd->vdev_spa) >= SPA_VERSION_RAIDZ3));

	/*
	 * Note that we'll add these even on storage pools where they
	 * aren't strictly required -- older software will just ignore
	 * it.
	 */
	fnvlist_add_uint64(nv, ZPOOL_CONFIG_NPARITY, vdrz->vd_nparity);

	if (vdrz->vn_vre.vre_state == DSS_SCANNING) {
		fnvlist_add_boolean(nv, ZPOOL_CONFIG_RAIDZ_EXPANDING);
	}

	mutex_enter(&vdrz->vd_expand_lock);
	if (!avl_is_empty(&vdrz->vd_expand_txgs)) {
		uint64_t count = avl_numnodes(&vdrz->vd_expand_txgs);
		uint64_t *txgs = kmem_alloc(sizeof (uint64_t) * count,
		    KM_SLEEP);
		uint64_t i = 0;

		for (reflow_node_t *re = avl_first(&vdrz->vd_expand_txgs);
		    re != NULL; re = AVL_NEXT(&vdrz->vd_expand_txgs, re)) {
			txgs[i++] = re->re_txg;
		}

		fnvlist_add_uint64_array(nv, ZPOOL_CONFIG_RAIDZ_EXPAND_TXGS,
		    txgs, count);

		kmem_free(txgs, sizeof (uint64_t) * count);
	}
	mutex_exit(&vdrz->vd_expand_lock);
}

static uint64_t
vdev_raidz_nparity(vdev_t *vd)
{
	vdev_raidz_t *vdrz = vd->vdev_tsd;
	return (vdrz->vd_nparity);
}

static uint64_t
vdev_raidz_ndisks(vdev_t *vd)
{
	return (vd->vdev_children);
}

vdev_ops_t vdev_raidz_ops = {
	.vdev_op_init = vdev_raidz_init,
	.vdev_op_fini = vdev_raidz_fini,
	.vdev_op_open = vdev_raidz_open,
	.vdev_op_close = vdev_raidz_close,
	.vdev_op_asize = vdev_raidz_asize,
	.vdev_op_min_asize = vdev_raidz_min_asize,
	.vdev_op_min_alloc = NULL,
	.vdev_op_io_start = vdev_raidz_io_start,
	.vdev_op_io_done = vdev_raidz_io_done,
	.vdev_op_state_change = vdev_raidz_state_change,
	.vdev_op_need_resilver = vdev_raidz_need_resilver,
	.vdev_op_hold = NULL,
	.vdev_op_rele = NULL,
	.vdev_op_remap = NULL,
	.vdev_op_xlate = vdev_raidz_xlate,
	.vdev_op_rebuild_asize = NULL,
	.vdev_op_metaslab_init = NULL,
	.vdev_op_config_generate = vdev_raidz_config_generate,
	.vdev_op_nparity = vdev_raidz_nparity,
	.vdev_op_ndisks = vdev_raidz_ndisks,
	.vdev_op_type = VDEV_TYPE_RAIDZ,	/* name of this vdev type */
	.vdev_op_leaf = B_FALSE			/* not a leaf vdev */
};

ZFS_MODULE_PARAM(zfs_vdev, raidz_, expand_max_reflow_bytes, ULONG, ZMOD_RW,
	"For testing, pause RAIDZ expansion after reflowing this many bytes");
ZFS_MODULE_PARAM(zfs_vdev, raidz_, expand_max_copy_bytes, ULONG, ZMOD_RW,
	"Max amount of concurrent i/o for RAIDZ expansion");
ZFS_MODULE_PARAM(zfs_vdev, raidz_, io_aggregate_rows, ULONG, ZMOD_RW,
	"For expanded RAIDZ, aggregate reads that have more rows than this");
ZFS_MODULE_PARAM(zfs, zfs_, scrub_after_expand, INT, ZMOD_RW,
	"For expanded RAIDZ, automatically start a pool scrub when expansion "
	"completes");
