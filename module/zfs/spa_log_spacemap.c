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
 * Copyright (c) 2018, 2019 by Delphix. All rights reserved.
 */

#include <sys/dmu_objset.h>
#include <sys/metaslab.h>
#include <sys/metaslab_impl.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/spa_log_spacemap.h>
#include <sys/vdev_impl.h>
#include <sys/zap.h>

/*
 * Log Space Maps
 *
 * Log space maps are an optimization in ZFS metadata allocations for pools
 * whose workloads are primarily random-writes. Random-write workloads are also
 * typically random-free, meaning that they are freeing from locations scattered
 * throughout the pool. This means that each TXG we will have to append some
 * FREE records to almost every metaslab. With log space maps, we hold their
 * changes in memory and log them altogether in one pool-wide space map on-disk
 * for persistence. As more blocks are accumulated in the log space maps and
 * more unflushed changes are accounted in memory, we flush a selected group
 * of metaslabs every TXG to relieve memory pressure and potential overheads
 * when loading the pool. Flushing a metaslab to disk relieves memory as we
 * flush any unflushed changes from memory to disk (i.e. the metaslab's space
 * map) and saves import time by making old log space maps obsolete and
 * eventually destroying them. [A log space map is said to be obsolete when all
 * its entries have made it to their corresponding metaslab space maps].
 *
 * == On disk data structures used ==
 *
 * - The pool has a new feature flag and a new entry in the MOS. The feature
 *   is activated when we create the first log space map and remains active
 *   for the lifetime of the pool. The new entry in the MOS Directory [refer
 *   to DMU_POOL_LOG_SPACEMAP_ZAP] is populated with a ZAP whose key-value
 *   pairs are of the form <key: txg, value: log space map object for that txg>.
 *   This entry is our on-disk reference of the log space maps that exist in
 *   the pool for each TXG and it is used during import to load all the
 *   metaslab unflushed changes in memory. To see how this structure is first
 *   created and later populated refer to spa_generate_syncing_log_sm(). To see
 *   how it is used during import time refer to spa_ld_log_sm_metadata().
 *
 * - Each vdev has a new entry in its vdev_top_zap (see field
 *   VDEV_TOP_ZAP_MS_UNFLUSHED_PHYS_TXGS) which holds the msp_unflushed_txg of
 *   each metaslab in this vdev. This field is the on-disk counterpart of the
 *   in-memory field ms_unflushed_txg which tells us from which TXG and onwards
 *   the metaslab haven't had its changes flushed. During import, we use this
 *   to ignore any entries in the space map log that are for this metaslab but
 *   from a TXG before msp_unflushed_txg. At that point, we also populate its
 *   in-memory counterpart and from there both fields are updated every time
 *   we flush that metaslab.
 *
 * - A space map is created every TXG and, during that TXG, it is used to log
 *   all incoming changes (the log space map). When created, the log space map
 *   is referenced in memory by spa_syncing_log_sm and its object ID is inserted
 *   to the space map ZAP mentioned above. The log space map is closed at the
 *   end of the TXG and will be destroyed when it becomes fully obsolete. We
 *   know when a log space map has become obsolete by looking at the oldest
 *   (and smallest) ms_unflushed_txg in the pool. If the value of that is bigger
 *   than the log space map's TXG, then it means that there is no metaslab who
 *   doesn't have the changes from that log and we can therefore destroy it.
 *   [see spa_cleanup_old_sm_logs()].
 *
 * == Important in-memory structures ==
 *
 * - The per-spa field spa_metaslabs_by_flushed sorts all the metaslabs in
 *   the pool by their ms_unflushed_txg field. It is primarily used for three
 *   reasons. First of all, it is used during flushing where we try to flush
 *   metaslabs in-order from the oldest-flushed to the most recently flushed
 *   every TXG. Secondly, it helps us to lookup the ms_unflushed_txg of the
 *   oldest flushed metaslab to distinguish which log space maps have become
 *   obsolete and which ones are still relevant. Finally it tells us which
 *   metaslabs have unflushed changes in a pool where this feature was just
 *   enabled, as we don't immediately add all of the pool's metaslabs but we
 *   add them over time as they go through metaslab_sync(). The reason that
 *   we do that is to ease these pools into the behavior of the flushing
 *   algorithm (described later on).
 *
 * - The per-spa field spa_sm_logs_by_txg can be thought as the in-memory
 *   counterpart of the space map ZAP mentioned above. It's an AVL tree whose
 *   nodes represent the log space maps in the pool. This in-memory
 *   representation of log space maps in the pool sorts the log space maps by
 *   the TXG that they were created (which is also the TXG of their unflushed
 *   changes). It also contains the following extra information for each
 *   space map:
 *   [1] The number of metaslabs that were last flushed on that TXG. This is
 *       important because if that counter is zero and this is the oldest
 *       log then it means that it is also obsolete.
 *   [2] The number of blocks of that space map. This field is used by the
 *       block heuristic of our flushing algorithm (described later on).
 *       It represents how many blocks of metadata changes ZFS had to write
 *       to disk for that TXG.
 *
 * - The per-spa field spa_log_summary is a list of entries that summarizes
 *   the metaslab and block counts of all the nodes of the spa_sm_logs_by_txg
 *   AVL tree mentioned above. The reason this exists is that our flushing
 *   algorithm (described later) tries to estimate how many metaslabs to flush
 *   in each TXG by iterating over all the log space maps and looking at their
 *   block counts. Summarizing that information means that don't have to
 *   iterate through each space map, minimizing the runtime overhead of the
 *   flushing algorithm which would be induced in syncing context. In terms of
 *   implementation the log summary is used as a queue:
 *   * we modify or pop entries from its head when we flush metaslabs
 *   * we modify or append entries to its tail when we sync changes.
 *
 * - Each metaslab has two new range trees that hold its unflushed changes,
 *   ms_unflushed_allocs and ms_unflushed_frees. These are always disjoint.
 *
 * == Flushing algorithm ==
 *
 * The decision of how many metaslabs to flush on a give TXG is guided by
 * two heuristics:
 *
 * [1] The memory heuristic -
 * We keep track of the memory used by the unflushed trees from all the
 * metaslabs [see sus_memused of spa_unflushed_stats] and we ensure that it
 * stays below a certain threshold which is determined by an arbitrary hard
 * limit and an arbitrary percentage of the system's memory [see
 * spa_log_exceeds_memlimit()]. When we see that the memory usage of the
 * unflushed changes are passing that threshold, we flush metaslabs, which
 * empties their unflushed range trees, reducing the memory used.
 *
 * [2] The block heuristic -
 * We try to keep the total number of blocks in the log space maps in check
 * so the log doesn't grow indefinitely and we don't induce a lot of overhead
 * when loading the pool. At the same time we don't want to flush a lot of
 * metaslabs too often as this would defeat the purpose of the log space map.
 * As a result we set a limit in the amount of blocks that we think it's
 * acceptable for the log space maps to have and try not to cross it.
 * [see sus_blocklimit from spa_unflushed_stats].
 *
 * In order to stay below the block limit every TXG we have to estimate how
 * many metaslabs we need to flush based on the current rate of incoming blocks
 * and our history of log space map blocks. The main idea here is to answer
 * the question of how many metaslabs do we need to flush in order to get rid
 * at least an X amount of log space map blocks. We can answer this question
 * by iterating backwards from the oldest log space map to the newest one
 * and looking at their metaslab and block counts. At this point the log summary
 * mentioned above comes handy as it reduces the amount of things that we have
 * to iterate (even though it may reduce the preciseness of our estimates due
 * to its aggregation of data). So with that in mind, we project the incoming
 * rate of the current TXG into the future and attempt to approximate how many
 * metaslabs would we need to flush from now in order to avoid exceeding our
 * block limit in different points in the future (granted that we would keep
 * flushing the same number of metaslabs for every TXG). Then we take the
 * maximum number from all these estimates to be on the safe side. For the
 * exact implementation details of algorithm refer to
 * spa_estimate_metaslabs_to_flush.
 */

/*
 * This is used as the block size for the space maps used for the
 * log space map feature. These space maps benefit from a bigger
 * block size as we expect to be writing a lot of data to them at
 * once.
 */
static const unsigned long zfs_log_sm_blksz = 1ULL << 17;

/*
 * Percentage of the overall system's memory that ZFS allows to be
 * used for unflushed changes (e.g. the sum of size of all the nodes
 * in the unflushed trees).
 *
 * Note that this value is calculated over 1000000 for finer granularity
 * (thus the _ppm suffix; reads as "parts per million"). As an example,
 * the default of 1000 allows 0.1% of memory to be used.
 */
static uint64_t zfs_unflushed_max_mem_ppm = 1000;

/*
 * Specific hard-limit in memory that ZFS allows to be used for
 * unflushed changes.
 */
static uint64_t zfs_unflushed_max_mem_amt = 1ULL << 30;

/*
 * The following tunable determines the number of blocks that can be used for
 * the log space maps. It is expressed as a percentage of the total number of
 * metaslabs in the pool (i.e. the default of 400 means that the number of log
 * blocks is capped at 4 times the number of metaslabs).
 *
 * This value exists to tune our flushing algorithm, with higher values
 * flushing metaslabs less often (doing less I/Os) per TXG versus lower values
 * flushing metaslabs more aggressively with the upside of saving overheads
 * when loading the pool. Another factor in this tradeoff is that flushing
 * less often can potentially lead to better utilization of the metaslab space
 * map's block size as we accumulate more changes per flush.
 *
 * Given that this tunable indirectly controls the flush rate (metaslabs
 * flushed per txg) and that's why making it a percentage in terms of the
 * number of metaslabs in the pool makes sense here.
 *
 * As a rule of thumb we default this tunable to 400% based on the following:
 *
 * 1] Assuming a constant flush rate and a constant incoming rate of log blocks
 *    it is reasonable to expect that the amount of obsolete entries changes
 *    linearly from txg to txg (e.g. the oldest log should have the most
 *    obsolete entries, and the most recent one the least). With this we could
 *    say that, at any given time, about half of the entries in the whole space
 *    map log are obsolete. Thus for every two entries for a metaslab in the
 *    log space map, only one of them is valid and actually makes it to the
 *    metaslab's space map.
 *    [factor of 2]
 * 2] Each entry in the log space map is guaranteed to be two words while
 *    entries in metaslab space maps are generally single-word.
 *    [an extra factor of 2 - 400% overall]
 * 3] Even if [1] and [2] are slightly less than 2 each, we haven't taken into
 *    account any consolidation of segments from the log space map to the
 *    unflushed range trees nor their history (e.g. a segment being allocated,
 *    then freed, then allocated again means 3 log space map entries but 0
 *    metaslab space map entries). Depending on the workload, we've seen ~1.8
 *    non-obsolete log space map entries per metaslab entry, for a total of
 *    ~600%. Since most of these estimates though are workload dependent, we
 *    default on 400% to be conservative.
 *
 *    Thus we could say that even in the worst
 *    case of [1] and [2], the factor should end up being 4.
 *
 * That said, regardless of the number of metaslabs in the pool we need to
 * provide upper and lower bounds for the log block limit.
 * [see zfs_unflushed_log_block_{min,max}]
 */
static uint_t zfs_unflushed_log_block_pct = 400;

/*
 * If the number of metaslabs is small and our incoming rate is high, we could
 * get into a situation that we are flushing all our metaslabs every TXG. Thus
 * we always allow at least this many log blocks.
 */
static uint64_t zfs_unflushed_log_block_min = 1000;

/*
 * If the log becomes too big, the import time of the pool can take a hit in
 * terms of performance. Thus we have a hard limit in the size of the log in
 * terms of blocks.
 */
static uint64_t zfs_unflushed_log_block_max = (1ULL << 17);

/*
 * Also we have a hard limit in the size of the log in terms of dirty TXGs.
 */
static uint64_t zfs_unflushed_log_txg_max = 1000;

/*
 * Max # of rows allowed for the log_summary. The tradeoff here is accuracy and
 * stability of the flushing algorithm (longer summary) vs its runtime overhead
 * (smaller summary is faster to traverse).
 */
static uint64_t zfs_max_logsm_summary_length = 10;

/*
 * Tunable that sets the lower bound on the metaslabs to flush every TXG.
 *
 * Setting this to 0 has no effect since if the pool is idle we won't even be
 * creating log space maps and therefore we won't be flushing. On the other
 * hand if the pool has any incoming workload our block heuristic will start
 * flushing metaslabs anyway.
 *
 * The point of this tunable is to be used in extreme cases where we really
 * want to flush more metaslabs than our adaptable heuristic plans to flush.
 */
static uint64_t zfs_min_metaslabs_to_flush = 1;

/*
 * Tunable that specifies how far in the past do we want to look when trying to
 * estimate the incoming log blocks for the current TXG.
 *
 * Setting this too high may not only increase runtime but also minimize the
 * effect of the incoming rates from the most recent TXGs as we take the
 * average over all the blocks that we walk
 * [see spa_estimate_incoming_log_blocks].
 */
static uint64_t zfs_max_log_walking = 5;

/*
 * This tunable exists solely for testing purposes. It ensures that the log
 * spacemaps are not flushed and destroyed during export in order for the
 * relevant log spacemap import code paths to be tested (effectively simulating
 * a crash).
 */
int zfs_keep_log_spacemaps_at_export = 0;

static uint64_t
spa_estimate_incoming_log_blocks(spa_t *spa)
{
	ASSERT3U(spa_sync_pass(spa), ==, 1);
	uint64_t steps = 0, sum = 0;
	for (spa_log_sm_t *sls = avl_last(&spa->spa_sm_logs_by_txg);
	    sls != NULL && steps < zfs_max_log_walking;
	    sls = AVL_PREV(&spa->spa_sm_logs_by_txg, sls)) {
		if (sls->sls_txg == spa_syncing_txg(spa)) {
			/*
			 * skip the log created in this TXG as this would
			 * make our estimations inaccurate.
			 */
			continue;
		}
		sum += sls->sls_nblocks;
		steps++;
	}
	return ((steps > 0) ? DIV_ROUND_UP(sum, steps) : 0);
}

uint64_t
spa_log_sm_blocklimit(spa_t *spa)
{
	return (spa->spa_unflushed_stats.sus_blocklimit);
}

void
spa_log_sm_set_blocklimit(spa_t *spa)
{
	if (!spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP)) {
		ASSERT0(spa_log_sm_blocklimit(spa));
		return;
	}

	uint64_t msdcount = 0;
	for (log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	    e; e = list_next(&spa->spa_log_summary, e))
		msdcount += e->lse_msdcount;

	uint64_t limit = msdcount * zfs_unflushed_log_block_pct / 100;
	spa->spa_unflushed_stats.sus_blocklimit = MIN(MAX(limit,
	    zfs_unflushed_log_block_min), zfs_unflushed_log_block_max);
}

uint64_t
spa_log_sm_nblocks(spa_t *spa)
{
	return (spa->spa_unflushed_stats.sus_nblocks);
}

/*
 * Ensure that the in-memory log space map structures and the summary
 * have the same block and metaslab counts.
 */
static void
spa_log_summary_verify_counts(spa_t *spa)
{
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP));

	if ((zfs_flags & ZFS_DEBUG_LOG_SPACEMAP) == 0)
		return;

	uint64_t ms_in_avl = avl_numnodes(&spa->spa_metaslabs_by_flushed);

	uint64_t ms_in_summary = 0, blk_in_summary = 0;
	for (log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	    e; e = list_next(&spa->spa_log_summary, e)) {
		ms_in_summary += e->lse_mscount;
		blk_in_summary += e->lse_blkcount;
	}

	uint64_t ms_in_logs = 0, blk_in_logs = 0;
	for (spa_log_sm_t *sls = avl_first(&spa->spa_sm_logs_by_txg);
	    sls; sls = AVL_NEXT(&spa->spa_sm_logs_by_txg, sls)) {
		ms_in_logs += sls->sls_mscount;
		blk_in_logs += sls->sls_nblocks;
	}

	VERIFY3U(ms_in_logs, ==, ms_in_summary);
	VERIFY3U(ms_in_logs, ==, ms_in_avl);
	VERIFY3U(blk_in_logs, ==, blk_in_summary);
	VERIFY3U(blk_in_logs, ==, spa_log_sm_nblocks(spa));
}

static boolean_t
summary_entry_is_full(spa_t *spa, log_summary_entry_t *e, uint64_t txg)
{
	if (e->lse_end == txg)
		return (0);
	if (e->lse_txgcount >= DIV_ROUND_UP(zfs_unflushed_log_txg_max,
	    zfs_max_logsm_summary_length))
		return (1);
	uint64_t blocks_per_row = MAX(1,
	    DIV_ROUND_UP(spa_log_sm_blocklimit(spa),
	    zfs_max_logsm_summary_length));
	return (blocks_per_row <= e->lse_blkcount);
}

/*
 * Update the log summary information to reflect the fact that a metaslab
 * was flushed or destroyed (e.g due to device removal or pool export/destroy).
 *
 * We typically flush the oldest flushed metaslab so the first (and oldest)
 * entry of the summary is updated. However if that metaslab is getting loaded
 * we may flush the second oldest one which may be part of an entry later in
 * the summary. Moreover, if we call into this function from metaslab_fini()
 * the metaslabs probably won't be ordered by ms_unflushed_txg. Thus we ask
 * for a txg as an argument so we can locate the appropriate summary entry for
 * the metaslab.
 */
void
spa_log_summary_decrement_mscount(spa_t *spa, uint64_t txg, boolean_t dirty)
{
	/*
	 * We don't track summary data for read-only pools and this function
	 * can be called from metaslab_fini(). In that case return immediately.
	 */
	if (!spa_writeable(spa))
		return;

	log_summary_entry_t *target = NULL;
	for (log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	    e != NULL; e = list_next(&spa->spa_log_summary, e)) {
		if (e->lse_start > txg)
			break;
		target = e;
	}

	if (target == NULL || target->lse_mscount == 0) {
		/*
		 * We didn't find a summary entry for this metaslab. We must be
		 * at the teardown of a spa_load() attempt that got an error
		 * while reading the log space maps.
		 */
		VERIFY3S(spa_load_state(spa), ==, SPA_LOAD_ERROR);
		return;
	}

	target->lse_mscount--;
	if (dirty)
		target->lse_msdcount--;
}

/*
 * Update the log summary information to reflect the fact that we destroyed
 * old log space maps. Since we can only destroy the oldest log space maps,
 * we decrement the block count of the oldest summary entry and potentially
 * destroy it when that count hits 0.
 *
 * This function is called after a metaslab is flushed and typically that
 * metaslab is the oldest flushed, which means that this function will
 * typically decrement the block count of the first entry of the summary and
 * potentially free it if the block count gets to zero (its metaslab count
 * should be zero too at that point).
 *
 * There are certain scenarios though that don't work exactly like that so we
 * need to account for them:
 *
 * Scenario [1]: It is possible that after we flushed the oldest flushed
 * metaslab and we destroyed the oldest log space map, more recent logs had 0
 * metaslabs pointing to them so we got rid of them too. This can happen due
 * to metaslabs being destroyed through device removal, or because the oldest
 * flushed metaslab was loading but we kept flushing more recently flushed
 * metaslabs due to the memory pressure of unflushed changes. Because of that,
 * we always iterate from the beginning of the summary and if blocks_gone is
 * bigger than the block_count of the current entry we free that entry (we
 * expect its metaslab count to be zero), we decrement blocks_gone and on to
 * the next entry repeating this procedure until blocks_gone gets decremented
 * to 0. Doing this also works for the typical case mentioned above.
 *
 * Scenario [2]: The oldest flushed metaslab isn't necessarily accounted by
 * the first (and oldest) entry in the summary. If the first few entries of
 * the summary were only accounting metaslabs from a device that was just
 * removed, then the current oldest flushed metaslab could be accounted by an
 * entry somewhere in the middle of the summary. Moreover flushing that
 * metaslab will destroy all the log space maps older than its ms_unflushed_txg
 * because they became obsolete after the removal. Thus, iterating as we did
 * for scenario [1] works out for this case too.
 *
 * Scenario [3]: At times we decide to flush all the metaslabs in the pool
 * in one TXG (either because we are exporting the pool or because our flushing
 * heuristics decided to do so). When that happens all the log space maps get
 * destroyed except the one created for the current TXG which doesn't have
 * any log blocks yet. As log space maps get destroyed with every metaslab that
 * we flush, entries in the summary are also destroyed. This brings a weird
 * corner-case when we flush the last metaslab and the log space map of the
 * current TXG is in the same summary entry with other log space maps that
 * are older. When that happens we are eventually left with this one last
 * summary entry whose blocks are gone (blocks_gone equals the entry's block
 * count) but its metaslab count is non-zero (because it accounts all the
 * metaslabs in the pool as they all got flushed). Under this scenario we can't
 * free this last summary entry as it's referencing all the metaslabs in the
 * pool and its block count will get incremented at the end of this sync (when
 * we close the syncing log space map). Thus we just decrement its current
 * block count and leave it alone. In the case that the pool gets exported,
 * its metaslab count will be decremented over time as we call metaslab_fini()
 * for all the metaslabs in the pool and the entry will be freed at
 * spa_unload_log_sm_metadata().
 */
void
spa_log_summary_decrement_blkcount(spa_t *spa, uint64_t blocks_gone)
{
	log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	ASSERT3P(e, !=, NULL);
	if (e->lse_txgcount > 0)
		e->lse_txgcount--;
	for (; e != NULL; e = list_head(&spa->spa_log_summary)) {
		if (e->lse_blkcount > blocks_gone) {
			e->lse_blkcount -= blocks_gone;
			blocks_gone = 0;
			break;
		} else if (e->lse_mscount == 0) {
			/* remove obsolete entry */
			blocks_gone -= e->lse_blkcount;
			list_remove(&spa->spa_log_summary, e);
			kmem_free(e, sizeof (log_summary_entry_t));
		} else {
			/* Verify that this is scenario [3] mentioned above. */
			VERIFY3U(blocks_gone, ==, e->lse_blkcount);

			/*
			 * Assert that this is scenario [3] further by ensuring
			 * that this is the only entry in the summary.
			 */
			VERIFY3P(e, ==, list_tail(&spa->spa_log_summary));
			ASSERT3P(e, ==, list_head(&spa->spa_log_summary));

			blocks_gone = e->lse_blkcount = 0;
			break;
		}
	}

	/*
	 * Ensure that there is no way we are trying to remove more blocks
	 * than the # of blocks in the summary.
	 */
	ASSERT0(blocks_gone);
}

void
spa_log_sm_decrement_mscount(spa_t *spa, uint64_t txg)
{
	spa_log_sm_t target = { .sls_txg = txg };
	spa_log_sm_t *sls = avl_find(&spa->spa_sm_logs_by_txg,
	    &target, NULL);

	if (sls == NULL) {
		/*
		 * We must be at the teardown of a spa_load() attempt that
		 * got an error while reading the log space maps.
		 */
		VERIFY3S(spa_load_state(spa), ==, SPA_LOAD_ERROR);
		return;
	}

	ASSERT(sls->sls_mscount > 0);
	sls->sls_mscount--;
}

void
spa_log_sm_increment_current_mscount(spa_t *spa)
{
	spa_log_sm_t *last_sls = avl_last(&spa->spa_sm_logs_by_txg);
	ASSERT3U(last_sls->sls_txg, ==, spa_syncing_txg(spa));
	last_sls->sls_mscount++;
}

static void
summary_add_data(spa_t *spa, uint64_t txg, uint64_t metaslabs_flushed,
    uint64_t metaslabs_dirty, uint64_t nblocks)
{
	log_summary_entry_t *e = list_tail(&spa->spa_log_summary);

	if (e == NULL || summary_entry_is_full(spa, e, txg)) {
		e = kmem_zalloc(sizeof (log_summary_entry_t), KM_SLEEP);
		e->lse_start = e->lse_end = txg;
		e->lse_txgcount = 1;
		list_insert_tail(&spa->spa_log_summary, e);
	}

	ASSERT3U(e->lse_start, <=, txg);
	if (e->lse_end < txg) {
		e->lse_end = txg;
		e->lse_txgcount++;
	}
	e->lse_mscount += metaslabs_flushed;
	e->lse_msdcount += metaslabs_dirty;
	e->lse_blkcount += nblocks;
}

static void
spa_log_summary_add_incoming_blocks(spa_t *spa, uint64_t nblocks)
{
	summary_add_data(spa, spa_syncing_txg(spa), 0, 0, nblocks);
}

void
spa_log_summary_add_flushed_metaslab(spa_t *spa, boolean_t dirty)
{
	summary_add_data(spa, spa_syncing_txg(spa), 1, dirty ? 1 : 0, 0);
}

void
spa_log_summary_dirty_flushed_metaslab(spa_t *spa, uint64_t txg)
{
	log_summary_entry_t *target = NULL;
	for (log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	    e != NULL; e = list_next(&spa->spa_log_summary, e)) {
		if (e->lse_start > txg)
			break;
		target = e;
	}
	ASSERT3P(target, !=, NULL);
	ASSERT3U(target->lse_mscount, !=, 0);
	target->lse_msdcount++;
}

/*
 * This function attempts to estimate how many metaslabs should
 * we flush to satisfy our block heuristic for the log spacemap
 * for the upcoming TXGs.
 *
 * Specifically, it first tries to estimate the number of incoming
 * blocks in this TXG. Then by projecting that incoming rate to
 * future TXGs and using the log summary, it figures out how many
 * flushes we would need to do for future TXGs individually to
 * stay below our block limit and returns the maximum number of
 * flushes from those estimates.
 */
static uint64_t
spa_estimate_metaslabs_to_flush(spa_t *spa)
{
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP));
	ASSERT3U(spa_sync_pass(spa), ==, 1);
	ASSERT(spa_log_sm_blocklimit(spa) != 0);

	/*
	 * This variable contains the incoming rate that will be projected
	 * and used for our flushing estimates in the future.
	 */
	uint64_t incoming = spa_estimate_incoming_log_blocks(spa);

	/*
	 * At any point in time this variable tells us how many
	 * TXGs in the future we are so we can make our estimations.
	 */
	uint64_t txgs_in_future = 1;

	/*
	 * This variable tells us how much room do we have until we hit
	 * our limit. When it goes negative, it means that we've exceeded
	 * our limit and we need to flush.
	 *
	 * Note that since we start at the first TXG in the future (i.e.
	 * txgs_in_future starts from 1) we already decrement this
	 * variable by the incoming rate.
	 */
	int64_t available_blocks =
	    spa_log_sm_blocklimit(spa) - spa_log_sm_nblocks(spa) - incoming;

	int64_t available_txgs = zfs_unflushed_log_txg_max;
	for (log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	    e; e = list_next(&spa->spa_log_summary, e))
		available_txgs -= e->lse_txgcount;

	/*
	 * This variable tells us the total number of flushes needed to
	 * keep the log size within the limit when we reach txgs_in_future.
	 */
	uint64_t total_flushes = 0;

	/* Holds the current maximum of our estimates so far. */
	uint64_t max_flushes_pertxg = zfs_min_metaslabs_to_flush;

	/*
	 * For our estimations we only look as far in the future
	 * as the summary allows us.
	 */
	for (log_summary_entry_t *e = list_head(&spa->spa_log_summary);
	    e; e = list_next(&spa->spa_log_summary, e)) {

		/*
		 * If there is still room before we exceed our limit
		 * then keep skipping TXGs accumulating more blocks
		 * based on the incoming rate until we exceed it.
		 */
		if (available_blocks >= 0 && available_txgs >= 0) {
			uint64_t skip_txgs = (incoming == 0) ?
			    available_txgs + 1 : MIN(available_txgs + 1,
			    (available_blocks / incoming) + 1);
			available_blocks -= (skip_txgs * incoming);
			available_txgs -= skip_txgs;
			txgs_in_future += skip_txgs;
			ASSERT3S(available_blocks, >=, -incoming);
			ASSERT3S(available_txgs, >=, -1);
		}

		/*
		 * At this point we're far enough into the future where
		 * the limit was just exceeded and we flush metaslabs
		 * based on the current entry in the summary, updating
		 * our available_blocks.
		 */
		ASSERT(available_blocks < 0 || available_txgs < 0);
		available_blocks += e->lse_blkcount;
		available_txgs += e->lse_txgcount;
		total_flushes += e->lse_msdcount;

		/*
		 * Keep the running maximum of the total_flushes that
		 * we've done so far over the number of TXGs in the
		 * future that we are. The idea here is to estimate
		 * the average number of flushes that we should do
		 * every TXG so that when we are that many TXGs in the
		 * future we stay under the limit.
		 */
		max_flushes_pertxg = MAX(max_flushes_pertxg,
		    DIV_ROUND_UP(total_flushes, txgs_in_future));
	}
	return (max_flushes_pertxg);
}

uint64_t
spa_log_sm_memused(spa_t *spa)
{
	return (spa->spa_unflushed_stats.sus_memused);
}

static boolean_t
spa_log_exceeds_memlimit(spa_t *spa)
{
	if (spa_log_sm_memused(spa) > zfs_unflushed_max_mem_amt)
		return (B_TRUE);

	uint64_t system_mem_allowed = ((physmem * PAGESIZE) *
	    zfs_unflushed_max_mem_ppm) / 1000000;
	if (spa_log_sm_memused(spa) > system_mem_allowed)
		return (B_TRUE);

	return (B_FALSE);
}

boolean_t
spa_flush_all_logs_requested(spa_t *spa)
{
	return (spa->spa_log_flushall_txg != 0);
}

void
spa_flush_metaslabs(spa_t *spa, dmu_tx_t *tx)
{
	uint64_t txg = dmu_tx_get_txg(tx);

	if (spa_sync_pass(spa) != 1)
		return;

	if (!spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP))
		return;

	/*
	 * If we don't have any metaslabs with unflushed changes
	 * return immediately.
	 */
	if (avl_numnodes(&spa->spa_metaslabs_by_flushed) == 0)
		return;

	/*
	 * During SPA export we leave a few empty TXGs to go by [see
	 * spa_final_dirty_txg() to understand why]. For this specific
	 * case, it is important to not flush any metaslabs as that
	 * would dirty this TXG.
	 *
	 * That said, during one of these dirty TXGs that is less or
	 * equal to spa_final_dirty(), spa_unload() will request that
	 * we try to flush all the metaslabs for that TXG before
	 * exporting the pool, thus we ensure that we didn't get a
	 * request of flushing everything before we attempt to return
	 * immediately.
	 */
	if (spa->spa_uberblock.ub_rootbp.blk_birth < txg &&
	    !dmu_objset_is_dirty(spa_meta_objset(spa), txg) &&
	    !spa_flush_all_logs_requested(spa))
		return;

	/*
	 * We need to generate a log space map before flushing because this
	 * will set up the in-memory data (i.e. node in spa_sm_logs_by_txg)
	 * for this TXG's flushed metaslab count (aka sls_mscount which is
	 * manipulated in many ways down the metaslab_flush() codepath).
	 *
	 * That is not to say that we may generate a log space map when we
	 * don't need it. If we are flushing metaslabs, that means that we
	 * were going to write changes to disk anyway, so even if we were
	 * not flushing, a log space map would have been created anyway in
	 * metaslab_sync().
	 */
	spa_generate_syncing_log_sm(spa, tx);

	/*
	 * This variable tells us how many metaslabs we want to flush based
	 * on the block-heuristic of our flushing algorithm (see block comment
	 * of log space map feature). We also decrement this as we flush
	 * metaslabs and attempt to destroy old log space maps.
	 */
	uint64_t want_to_flush;
	if (spa_flush_all_logs_requested(spa)) {
		ASSERT3S(spa_state(spa), ==, POOL_STATE_EXPORTED);
		want_to_flush = UINT64_MAX;
	} else {
		want_to_flush = spa_estimate_metaslabs_to_flush(spa);
	}

	/* Used purely for verification purposes */
	uint64_t visited = 0;

	/*
	 * Ideally we would only iterate through spa_metaslabs_by_flushed
	 * using only one variable (curr). We can't do that because
	 * metaslab_flush() mutates position of curr in the AVL when
	 * it flushes that metaslab by moving it to the end of the tree.
	 * Thus we always keep track of the original next node of the
	 * current node (curr) in another variable (next).
	 */
	metaslab_t *next = NULL;
	for (metaslab_t *curr = avl_first(&spa->spa_metaslabs_by_flushed);
	    curr != NULL; curr = next) {
		next = AVL_NEXT(&spa->spa_metaslabs_by_flushed, curr);

		/*
		 * If this metaslab has been flushed this txg then we've done
		 * a full circle over the metaslabs.
		 */
		if (metaslab_unflushed_txg(curr) == txg)
			break;

		/*
		 * If we are done flushing for the block heuristic and the
		 * unflushed changes don't exceed the memory limit just stop.
		 */
		if (want_to_flush == 0 && !spa_log_exceeds_memlimit(spa))
			break;

		if (metaslab_unflushed_dirty(curr)) {
			mutex_enter(&curr->ms_sync_lock);
			mutex_enter(&curr->ms_lock);
			metaslab_flush(curr, tx);
			mutex_exit(&curr->ms_lock);
			mutex_exit(&curr->ms_sync_lock);
			if (want_to_flush > 0)
				want_to_flush--;
		} else
			metaslab_unflushed_bump(curr, tx, B_FALSE);

		visited++;
	}
	ASSERT3U(avl_numnodes(&spa->spa_metaslabs_by_flushed), >=, visited);

	spa_log_sm_set_blocklimit(spa);
}

/*
 * Close the log space map for this TXG and update the block counts
 * for the log's in-memory structure and the summary.
 */
void
spa_sync_close_syncing_log_sm(spa_t *spa)
{
	if (spa_syncing_log_sm(spa) == NULL)
		return;
	ASSERT(spa_feature_is_active(spa, SPA_FEATURE_LOG_SPACEMAP));

	spa_log_sm_t *sls = avl_last(&spa->spa_sm_logs_by_txg);
	ASSERT3U(sls->sls_txg, ==, spa_syncing_txg(spa));

	sls->sls_nblocks = space_map_nblocks(spa_syncing_log_sm(spa));
	spa->spa_unflushed_stats.sus_nblocks += sls->sls_nblocks;

	/*
	 * Note that we can't assert that sls_mscount is not 0,
	 * because there is the case where the first metaslab
	 * in spa_metaslabs_by_flushed is loading and we were
	 * not able to flush any metaslabs the current TXG.
	 */
	ASSERT(sls->sls_nblocks != 0);

	spa_log_summary_add_incoming_blocks(spa, sls->sls_nblocks);
	spa_log_summary_verify_counts(spa);

	space_map_close(spa->spa_syncing_log_sm);
	spa->spa_syncing_log_sm = NULL;

	/*
	 * At this point we tried to flush as many metaslabs as we
	 * can as the pool is getting exported. Reset the "flush all"
	 * so the last few TXGs before closing the pool can be empty
	 * (e.g. not dirty).
	 */
	if (spa_flush_all_logs_requested(spa)) {
		ASSERT3S(spa_state(spa), ==, POOL_STATE_EXPORTED);
		spa->spa_log_flushall_txg = 0;
	}
}

void
spa_cleanup_old_sm_logs(spa_t *spa, dmu_tx_t *tx)
{
	objset_t *mos = spa_meta_objset(spa);

	uint64_t spacemap_zap;
	int error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_LOG_SPACEMAP_ZAP, sizeof (spacemap_zap), 1, &spacemap_zap);
	if (error == ENOENT) {
		ASSERT(avl_is_empty(&spa->spa_sm_logs_by_txg));
		return;
	}
	VERIFY0(error);

	metaslab_t *oldest = avl_first(&spa->spa_metaslabs_by_flushed);
	uint64_t oldest_flushed_txg = metaslab_unflushed_txg(oldest);

	/* Free all log space maps older than the oldest_flushed_txg. */
	for (spa_log_sm_t *sls = avl_first(&spa->spa_sm_logs_by_txg);
	    sls && sls->sls_txg < oldest_flushed_txg;
	    sls = avl_first(&spa->spa_sm_logs_by_txg)) {
		ASSERT0(sls->sls_mscount);
		avl_remove(&spa->spa_sm_logs_by_txg, sls);
		space_map_free_obj(mos, sls->sls_sm_obj, tx);
		VERIFY0(zap_remove_int(mos, spacemap_zap, sls->sls_txg, tx));
		spa_log_summary_decrement_blkcount(spa, sls->sls_nblocks);
		spa->spa_unflushed_stats.sus_nblocks -= sls->sls_nblocks;
		kmem_free(sls, sizeof (spa_log_sm_t));
	}
}

static spa_log_sm_t *
spa_log_sm_alloc(uint64_t sm_obj, uint64_t txg)
{
	spa_log_sm_t *sls = kmem_zalloc(sizeof (*sls), KM_SLEEP);
	sls->sls_sm_obj = sm_obj;
	sls->sls_txg = txg;
	return (sls);
}

void
spa_generate_syncing_log_sm(spa_t *spa, dmu_tx_t *tx)
{
	uint64_t txg = dmu_tx_get_txg(tx);
	objset_t *mos = spa_meta_objset(spa);

	if (spa_syncing_log_sm(spa) != NULL)
		return;

	if (!spa_feature_is_enabled(spa, SPA_FEATURE_LOG_SPACEMAP))
		return;

	uint64_t spacemap_zap;
	int error = zap_lookup(mos, DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_LOG_SPACEMAP_ZAP, sizeof (spacemap_zap), 1, &spacemap_zap);
	if (error == ENOENT) {
		ASSERT(avl_is_empty(&spa->spa_sm_logs_by_txg));

		error = 0;
		spacemap_zap = zap_create(mos,
		    DMU_OTN_ZAP_METADATA, DMU_OT_NONE, 0, tx);
		VERIFY0(zap_add(mos, DMU_POOL_DIRECTORY_OBJECT,
		    DMU_POOL_LOG_SPACEMAP_ZAP, sizeof (spacemap_zap), 1,
		    &spacemap_zap, tx));
		spa_feature_incr(spa, SPA_FEATURE_LOG_SPACEMAP, tx);
	}
	VERIFY0(error);

	uint64_t sm_obj;
	ASSERT3U(zap_lookup_int_key(mos, spacemap_zap, txg, &sm_obj),
	    ==, ENOENT);
	sm_obj = space_map_alloc(mos, zfs_log_sm_blksz, tx);
	VERIFY0(zap_add_int_key(mos, spacemap_zap, txg, sm_obj, tx));
	avl_add(&spa->spa_sm_logs_by_txg, spa_log_sm_alloc(sm_obj, txg));

	/*
	 * We pass UINT64_MAX as the space map's representation size
	 * and SPA_MINBLOCKSHIFT as the shift, to make the space map
	 * accept any sorts of segments since there's no real advantage
	 * to being more restrictive (given that we're already going
	 * to be using 2-word entries).
	 */
	VERIFY0(space_map_open(&spa->spa_syncing_log_sm, mos, sm_obj,
	    0, UINT64_MAX, SPA_MINBLOCKSHIFT));

	spa_log_sm_set_blocklimit(spa);
}

/*
 * Find all the log space maps stored in the space map ZAP and sort
 * them by their TXG in spa_sm_logs_by_txg.
 */
static int
spa_ld_log_sm_metadata(spa_t *spa)
{
	int error;
	uint64_t spacemap_zap;

	ASSERT(avl_is_empty(&spa->spa_sm_logs_by_txg));

	error = zap_lookup(spa_meta_objset(spa), DMU_POOL_DIRECTORY_OBJECT,
	    DMU_POOL_LOG_SPACEMAP_ZAP, sizeof (spacemap_zap), 1, &spacemap_zap);
	if (error == ENOENT) {
		/* the space map ZAP doesn't exist yet */
		return (0);
	} else if (error != 0) {
		spa_load_failed(spa, "spa_ld_log_sm_metadata(): failed at "
		    "zap_lookup(DMU_POOL_DIRECTORY_OBJECT) [error %d]",
		    error);
		return (error);
	}

	zap_cursor_t zc;
	zap_attribute_t za;
	for (zap_cursor_init(&zc, spa_meta_objset(spa), spacemap_zap);
	    (error = zap_cursor_retrieve(&zc, &za)) == 0;
	    zap_cursor_advance(&zc)) {
		uint64_t log_txg = zfs_strtonum(za.za_name, NULL);
		spa_log_sm_t *sls =
		    spa_log_sm_alloc(za.za_first_integer, log_txg);
		avl_add(&spa->spa_sm_logs_by_txg, sls);
	}
	zap_cursor_fini(&zc);
	if (error != ENOENT) {
		spa_load_failed(spa, "spa_ld_log_sm_metadata(): failed at "
		    "zap_cursor_retrieve(spacemap_zap) [error %d]",
		    error);
		return (error);
	}

	for (metaslab_t *m = avl_first(&spa->spa_metaslabs_by_flushed);
	    m; m = AVL_NEXT(&spa->spa_metaslabs_by_flushed, m)) {
		spa_log_sm_t target = { .sls_txg = metaslab_unflushed_txg(m) };
		spa_log_sm_t *sls = avl_find(&spa->spa_sm_logs_by_txg,
		    &target, NULL);

		/*
		 * At this point if sls is zero it means that a bug occurred
		 * in ZFS the last time the pool was open or earlier in the
		 * import code path. In general, we would have placed a
		 * VERIFY() here or in this case just let the kernel panic
		 * with NULL pointer dereference when incrementing sls_mscount,
		 * but since this is the import code path we can be a bit more
		 * lenient. Thus, for DEBUG bits we always cause a panic, while
		 * in production we log the error and just fail the import.
		 */
		ASSERT(sls != NULL);
		if (sls == NULL) {
			spa_load_failed(spa, "spa_ld_log_sm_metadata(): bug "
			    "encountered: could not find log spacemap for "
			    "TXG %llu [error %d]",
			    (u_longlong_t)metaslab_unflushed_txg(m), ENOENT);
			return (ENOENT);
		}
		sls->sls_mscount++;
	}

	return (0);
}

typedef struct spa_ld_log_sm_arg {
	spa_t *slls_spa;
	uint64_t slls_txg;
} spa_ld_log_sm_arg_t;

static int
spa_ld_log_sm_cb(space_map_entry_t *sme, void *arg)
{
	uint64_t offset = sme->sme_offset;
	uint64_t size = sme->sme_run;
	uint32_t vdev_id = sme->sme_vdev;

	spa_ld_log_sm_arg_t *slls = arg;
	spa_t *spa = slls->slls_spa;

	vdev_t *vd = vdev_lookup_top(spa, vdev_id);

	/*
	 * If the vdev has been removed (i.e. it is indirect or a hole)
	 * skip this entry. The contents of this vdev have already moved
	 * elsewhere.
	 */
	if (!vdev_is_concrete(vd))
		return (0);

	metaslab_t *ms = vd->vdev_ms[offset >> vd->vdev_ms_shift];
	ASSERT(!ms->ms_loaded);

	/*
	 * If we have already flushed entries for this TXG to this
	 * metaslab's space map, then ignore it. Note that we flush
	 * before processing any allocations/frees for that TXG, so
	 * the metaslab's space map only has entries from *before*
	 * the unflushed TXG.
	 */
	if (slls->slls_txg < metaslab_unflushed_txg(ms))
		return (0);

	switch (sme->sme_type) {
	case SM_ALLOC:
		range_tree_remove_xor_add_segment(offset, offset + size,
		    ms->ms_unflushed_frees, ms->ms_unflushed_allocs);
		break;
	case SM_FREE:
		range_tree_remove_xor_add_segment(offset, offset + size,
		    ms->ms_unflushed_allocs, ms->ms_unflushed_frees);
		break;
	default:
		panic("invalid maptype_t");
		break;
	}
	if (!metaslab_unflushed_dirty(ms)) {
		metaslab_set_unflushed_dirty(ms, B_TRUE);
		spa_log_summary_dirty_flushed_metaslab(spa,
		    metaslab_unflushed_txg(ms));
	}
	return (0);
}

static int
spa_ld_log_sm_data(spa_t *spa)
{
	spa_log_sm_t *sls, *psls;
	int error = 0;

	/*
	 * If we are not going to do any writes there is no need
	 * to read the log space maps.
	 */
	if (!spa_writeable(spa))
		return (0);

	ASSERT0(spa->spa_unflushed_stats.sus_nblocks);
	ASSERT0(spa->spa_unflushed_stats.sus_memused);

	hrtime_t read_logs_starttime = gethrtime();

	/* Prefetch log spacemaps dnodes. */
	for (sls = avl_first(&spa->spa_sm_logs_by_txg); sls;
	    sls = AVL_NEXT(&spa->spa_sm_logs_by_txg, sls)) {
		dmu_prefetch_dnode(spa_meta_objset(spa), sls->sls_sm_obj,
		    ZIO_PRIORITY_SYNC_READ);
	}

	uint_t pn = 0;
	uint64_t ps = 0;
	uint64_t nsm = 0;
	psls = sls = avl_first(&spa->spa_sm_logs_by_txg);
	while (sls != NULL) {
		/* Prefetch log spacemaps up to 16 TXGs or MBs ahead. */
		if (psls != NULL && pn < 16 &&
		    (pn < 2 || ps < 2 * dmu_prefetch_max)) {
			error = space_map_open(&psls->sls_sm,
			    spa_meta_objset(spa), psls->sls_sm_obj, 0,
			    UINT64_MAX, SPA_MINBLOCKSHIFT);
			if (error != 0) {
				spa_load_failed(spa, "spa_ld_log_sm_data(): "
				    "failed at space_map_open(obj=%llu) "
				    "[error %d]",
				    (u_longlong_t)sls->sls_sm_obj, error);
				goto out;
			}
			dmu_prefetch(spa_meta_objset(spa), psls->sls_sm_obj,
			    0, 0, space_map_length(psls->sls_sm),
			    ZIO_PRIORITY_ASYNC_READ);
			pn++;
			ps += space_map_length(psls->sls_sm);
			psls = AVL_NEXT(&spa->spa_sm_logs_by_txg, psls);
			continue;
		}

		/* Load TXG log spacemap into ms_unflushed_allocs/frees. */
		kpreempt(KPREEMPT_SYNC);
		ASSERT0(sls->sls_nblocks);
		sls->sls_nblocks = space_map_nblocks(sls->sls_sm);
		spa->spa_unflushed_stats.sus_nblocks += sls->sls_nblocks;
		summary_add_data(spa, sls->sls_txg,
		    sls->sls_mscount, 0, sls->sls_nblocks);

		spa_import_progress_set_notes_nolog(spa,
		    "Read %llu of %lu log space maps", (u_longlong_t)nsm,
		    avl_numnodes(&spa->spa_sm_logs_by_txg));

		struct spa_ld_log_sm_arg vla = {
			.slls_spa = spa,
			.slls_txg = sls->sls_txg
		};
		error = space_map_iterate(sls->sls_sm,
		    space_map_length(sls->sls_sm), spa_ld_log_sm_cb, &vla);
		if (error != 0) {
			spa_load_failed(spa, "spa_ld_log_sm_data(): failed "
			    "at space_map_iterate(obj=%llu) [error %d]",
			    (u_longlong_t)sls->sls_sm_obj, error);
			goto out;
		}

		pn--;
		ps -= space_map_length(sls->sls_sm);
		nsm++;
		space_map_close(sls->sls_sm);
		sls->sls_sm = NULL;
		sls = AVL_NEXT(&spa->spa_sm_logs_by_txg, sls);

		/* Update log block limits considering just loaded. */
		spa_log_sm_set_blocklimit(spa);
	}

	hrtime_t read_logs_endtime = gethrtime();
	spa_load_note(spa,
	    "Read %lu log space maps (%llu total blocks - blksz = %llu bytes) "
	    "in %lld ms", avl_numnodes(&spa->spa_sm_logs_by_txg),
	    (u_longlong_t)spa_log_sm_nblocks(spa),
	    (u_longlong_t)zfs_log_sm_blksz,
	    (longlong_t)NSEC2MSEC(read_logs_endtime - read_logs_starttime));

out:
	if (error != 0) {
		for (spa_log_sm_t *sls = avl_first(&spa->spa_sm_logs_by_txg);
		    sls; sls = AVL_NEXT(&spa->spa_sm_logs_by_txg, sls)) {
			if (sls->sls_sm) {
				space_map_close(sls->sls_sm);
				sls->sls_sm = NULL;
			}
		}
	} else {
		ASSERT0(pn);
		ASSERT0(ps);
	}
	/*
	 * Now that the metaslabs contain their unflushed changes:
	 * [1] recalculate their actual allocated space
	 * [2] recalculate their weights
	 * [3] sum up the memory usage of their unflushed range trees
	 * [4] optionally load them, if debug_load is set
	 *
	 * Note that even in the case where we get here because of an
	 * error (e.g. error != 0), we still want to update the fields
	 * below in order to have a proper teardown in spa_unload().
	 */
	for (metaslab_t *m = avl_first(&spa->spa_metaslabs_by_flushed);
	    m != NULL; m = AVL_NEXT(&spa->spa_metaslabs_by_flushed, m)) {
		mutex_enter(&m->ms_lock);
		m->ms_allocated_space = space_map_allocated(m->ms_sm) +
		    range_tree_space(m->ms_unflushed_allocs) -
		    range_tree_space(m->ms_unflushed_frees);

		vdev_t *vd = m->ms_group->mg_vd;
		metaslab_space_update(vd, m->ms_group->mg_class,
		    range_tree_space(m->ms_unflushed_allocs), 0, 0);
		metaslab_space_update(vd, m->ms_group->mg_class,
		    -range_tree_space(m->ms_unflushed_frees), 0, 0);

		ASSERT0(m->ms_weight & METASLAB_ACTIVE_MASK);
		metaslab_recalculate_weight_and_sort(m);

		spa->spa_unflushed_stats.sus_memused +=
		    metaslab_unflushed_changes_memused(m);

		if (metaslab_debug_load && m->ms_sm != NULL) {
			VERIFY0(metaslab_load(m));
			metaslab_set_selected_txg(m, 0);
		}
		mutex_exit(&m->ms_lock);
	}

	return (error);
}

static int
spa_ld_unflushed_txgs(vdev_t *vd)
{
	spa_t *spa = vd->vdev_spa;
	objset_t *mos = spa_meta_objset(spa);

	if (vd->vdev_top_zap == 0)
		return (0);

	uint64_t object = 0;
	int error = zap_lookup(mos, vd->vdev_top_zap,
	    VDEV_TOP_ZAP_MS_UNFLUSHED_PHYS_TXGS,
	    sizeof (uint64_t), 1, &object);
	if (error == ENOENT)
		return (0);
	else if (error != 0) {
		spa_load_failed(spa, "spa_ld_unflushed_txgs(): failed at "
		    "zap_lookup(vdev_top_zap=%llu) [error %d]",
		    (u_longlong_t)vd->vdev_top_zap, error);
		return (error);
	}

	for (uint64_t m = 0; m < vd->vdev_ms_count; m++) {
		metaslab_t *ms = vd->vdev_ms[m];
		ASSERT(ms != NULL);

		metaslab_unflushed_phys_t entry;
		uint64_t entry_size = sizeof (entry);
		uint64_t entry_offset = ms->ms_id * entry_size;

		error = dmu_read(mos, object,
		    entry_offset, entry_size, &entry, 0);
		if (error != 0) {
			spa_load_failed(spa, "spa_ld_unflushed_txgs(): "
			    "failed at dmu_read(obj=%llu) [error %d]",
			    (u_longlong_t)object, error);
			return (error);
		}

		ms->ms_unflushed_txg = entry.msp_unflushed_txg;
		ms->ms_unflushed_dirty = B_FALSE;
		ASSERT(range_tree_is_empty(ms->ms_unflushed_allocs));
		ASSERT(range_tree_is_empty(ms->ms_unflushed_frees));
		if (ms->ms_unflushed_txg != 0) {
			mutex_enter(&spa->spa_flushed_ms_lock);
			avl_add(&spa->spa_metaslabs_by_flushed, ms);
			mutex_exit(&spa->spa_flushed_ms_lock);
		}
	}
	return (0);
}

/*
 * Read all the log space map entries into their respective
 * metaslab unflushed trees and keep them sorted by TXG in the
 * SPA's metadata. In addition, setup all the metadata for the
 * memory and the block heuristics.
 */
int
spa_ld_log_spacemaps(spa_t *spa)
{
	int error;

	spa_log_sm_set_blocklimit(spa);

	for (uint64_t c = 0; c < spa->spa_root_vdev->vdev_children; c++) {
		vdev_t *vd = spa->spa_root_vdev->vdev_child[c];
		error = spa_ld_unflushed_txgs(vd);
		if (error != 0)
			return (error);
	}

	error = spa_ld_log_sm_metadata(spa);
	if (error != 0)
		return (error);

	/*
	 * Note: we don't actually expect anything to change at this point
	 * but we grab the config lock so we don't fail any assertions
	 * when using vdev_lookup_top().
	 */
	spa_config_enter(spa, SCL_CONFIG, FTAG, RW_READER);
	error = spa_ld_log_sm_data(spa);
	spa_config_exit(spa, SCL_CONFIG, FTAG);

	return (error);
}

/* BEGIN CSTYLED */
ZFS_MODULE_PARAM(zfs, zfs_, unflushed_max_mem_amt, U64, ZMOD_RW,
	"Specific hard-limit in memory that ZFS allows to be used for "
	"unflushed changes");

ZFS_MODULE_PARAM(zfs, zfs_, unflushed_max_mem_ppm, U64, ZMOD_RW,
	"Percentage of the overall system memory that ZFS allows to be "
	"used for unflushed changes (value is calculated over 1000000 for "
	"finer granularity)");

ZFS_MODULE_PARAM(zfs, zfs_, unflushed_log_block_max, U64, ZMOD_RW,
	"Hard limit (upper-bound) in the size of the space map log "
	"in terms of blocks.");

ZFS_MODULE_PARAM(zfs, zfs_, unflushed_log_block_min, U64, ZMOD_RW,
	"Lower-bound limit for the maximum amount of blocks allowed in "
	"log spacemap (see zfs_unflushed_log_block_max)");

ZFS_MODULE_PARAM(zfs, zfs_, unflushed_log_txg_max, U64, ZMOD_RW,
    "Hard limit (upper-bound) in the size of the space map log "
    "in terms of dirty TXGs.");

ZFS_MODULE_PARAM(zfs, zfs_, unflushed_log_block_pct, UINT, ZMOD_RW,
	"Tunable used to determine the number of blocks that can be used for "
	"the spacemap log, expressed as a percentage of the total number of "
	"metaslabs in the pool (e.g. 400 means the number of log blocks is "
	"capped at 4 times the number of metaslabs)");

ZFS_MODULE_PARAM(zfs, zfs_, max_log_walking, U64, ZMOD_RW,
	"The number of past TXGs that the flushing algorithm of the log "
	"spacemap feature uses to estimate incoming log blocks");

ZFS_MODULE_PARAM(zfs, zfs_, keep_log_spacemaps_at_export, INT, ZMOD_RW,
	"Prevent the log spacemaps from being flushed and destroyed "
	"during pool export/destroy");
/* END CSTYLED */

ZFS_MODULE_PARAM(zfs, zfs_, max_logsm_summary_length, U64, ZMOD_RW,
	"Maximum number of rows allowed in the summary of the spacemap log");

ZFS_MODULE_PARAM(zfs, zfs_, min_metaslabs_to_flush, U64, ZMOD_RW,
	"Minimum number of metaslabs to flush per dirty TXG");
