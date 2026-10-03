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
 * CDDL HEADER END
 */

#ifndef	_ZMETAD_H
#define	_ZMETAD_H

#include <sys/types.h>
#include <limits.h>
#include <libnvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Default configuration values */
#define	ZMETAD_DEFAULT_DB_PATH		"/var/lib/zfs/zmetad.db"
#define	ZMETAD_DEFAULT_POLL_INTERVAL	30	/* seconds */
#define	ZMETAD_DEFAULT_RETENTION_DAYS	90
/*
 * Upper bound for -r/--retention: keeps retention_days * 86400 far
 * inside 64-bit range and rejects nonsense values at option-parse
 * time (see also the defensive check in zmetad_db_cleanup).
 */
#define	ZMETAD_MAX_RETENTION_DAYS	36500	/* ~100 years */

/* Configuration structure */
typedef struct zmetad_config {
	char		db_path[PATH_MAX];
	char		schema_path[PATH_MAX];
	int		poll_interval;
	int		retention_days;
	boolean_t	foreground;
	int		verbose;
	char		*export_schema_path;
	char		*check_schema_path;
	char		*purge_dataset;
	boolean_t	force;
} zmetad_config_t;

/*
 * Opaque database handle
 */
typedef struct zmetad_db zmetad_db_t;

/*
 * Database operations (zmetad_db.c)
 */

/* Open or create the SQLite database for the given event schema */
typedef struct zmetad_schema zmetad_schema_t;
int zmetad_db_open(zmetad_db_t **dbp, const char *path,
    const zmetad_schema_t *zs);

/*
 * Install the runtime warning sink for db-layer warnings (poll-path
 * errors, migration progress).  The daemon passes its daemon_warn-
 * backed sink so warnings survive daemonization; NULL (the default)
 * falls back to stderr.  cb receives a single formatted line.
 */
void zmetad_db_set_warn(zmetad_db_t *db, void (*cb)(const char *msg));

/* Close the database */
void zmetad_db_close(zmetad_db_t *db);

/*
 * Explicit transaction control.  begin issues BEGIN IMMEDIATE so a
 * concurrent writer fails fast into the busy timeout instead of
 * deadlocking on lock upgrade; commit/rollback check their result.
 * Used to make a batch of writes (event inserts, migrations, purge)
 * atomic.
 */
int zmetad_db_begin(zmetad_db_t *db);
int zmetad_db_commit(zmetad_db_t *db);
int zmetad_db_rollback(zmetad_db_t *db);

/* Insert an event record */
int zmetad_db_insert_event(zmetad_db_t *db, const char *dataset,
    nvlist_t *event);

/*
 * Remember the dataset's root object id as reported by the kernel
 * (GET_EVENTS "root_objid") AND persist it in sync_state.root_id
 * (DB layout 7).  The full-path resolver uses the id -- cache or
 * persisted fallback -- to treat that ancestor as the path terminus
 * instead of guessing from graph emptiness; the persisted copy lets
 * a legacy-kernel reply (no root_objid key) keep resolving against
 * the last learned root.
 */
void zmetad_db_set_root_id(zmetad_db_t *db, const char *dataset,
    uint64_t id);

/*
 * Get the last synced offset for a dataset.  Returns 0 and sets
 * *offset when a sync_state row exists, ENOENT with *offset = 0 when
 * the dataset was never synced (distinct from a query error, which
 * returns EIO so callers do not mistake a failure for offset 0 and
 * emit a spurious regression row).
 */
int zmetad_db_get_last_offset(zmetad_db_t *db, const char *dataset,
    uint64_t *offset);

/*
 * Get the stored event-log ring identity for a dataset.
 * 0 = unknown (no row yet, a pre-v3 row, or only legacy replies).
 */
uint64_t zmetad_db_get_ring_guid(zmetad_db_t *db, const char *dataset);

/*
 * Set the last synced offset for a dataset, persisting the ring
 * identity in the same write.  ring_guid 0 stores NULL (unknown).
 */
int zmetad_db_set_last_offset(zmetad_db_t *db, const char *dataset,
    uint64_t offset, uint64_t ring_guid);

/*
 * Stored records_lost baseline for a dataset.  Returns 0 and writes
 * *havep = B_TRUE with *lostp when a non-NULL value is stored;
 * *havep = B_FALSE and ENOENT mean no baseline yet (no row, or a
 * pre-v6 NULL): the first observation must not invent a gap.
 */
int zmetad_db_get_last_lost(zmetad_db_t *db, const char *dataset,
    boolean_t *havep, uint64_t *lostp);

/* Persist the records_lost baseline (or clear it with B_FALSE). */
int zmetad_db_set_last_lost(zmetad_db_t *db, const char *dataset,
    boolean_t have, uint64_t last_lost);

/*
 * Per-dataset purge epoch: bumped by every successful
 * `zmetad --purge <dataset>` (meta key 'purge_epoch:<dataset>',
 * one atomic upsert per bump).  The daemon compares ONLY its own
 * dataset's epoch each poll so its in-memory loss state for that
 * dataset can be re-armed after another process removed its
 * history; purging one dataset does not re-arm the others.  Returns
 * 0 and writes *epochp; ENOENT means no purge has ever been
 * recorded for the dataset (fresh per-dataset epoch starts at 0;
 * the pre-layout-7 global 'purge_epoch' key is read once as the
 * initial baseline for migration continuity).
 */
int zmetad_db_get_purge_epoch(zmetad_db_t *db, const char *dataset,
    uint64_t *epochp);

/* Atomically bump the dataset's purge epoch (creates it at 1). */
int zmetad_db_bump_purge_epoch(zmetad_db_t *db, const char *dataset);

/*
 * Delete every row belonging to "dataset" from the events, gaps,
 * sync_state and objmap tables.  Deleted row counts are reported
 * through counts[] in events, gaps, sync_state, objmap order.  The
 * deletes run in a single transaction.  Does not touch the kernel
 * event ring (see zmetad --purge).
 */
int zmetad_db_purge_dataset(zmetad_db_t *db, const char *dataset,
    long long counts[4]);

/*
 * Record an event-log gap for a dataset: records between
 * "from_offset" and "to_offset" were never captured.  "lost"
 * semantics:
 *   > 0   that many records lost (collector lag / queue overflow)
 *   0     watermark regression; no countable loss (count unknown)
 *   (uint64_t)-1  ring replaced (identity swap; count unknown);
 *                 stored as -1 in the gaps table
 *
 * gaps rows are the permanent completeness record: they are never
 * rewritten or deleted by retention cleanup (see zmetad_db_cleanup);
 * only zmetad --purge removes them, by dataset.
 */
int zmetad_db_insert_gap(zmetad_db_t *db, const char *dataset,
    uint64_t from_offset, uint64_t to_offset, uint64_t lost);

/*
 * Count a dataset's gaps rows by lost value: counts[0] = lost < 0
 * (ring replacements), counts[1] = lost == 0 (regressions),
 * counts[2] = lost > 0 (recorded loss counts).  Returns 0 on
 * success, nonzero on database error (counts then zeroed).
 */
int zmetad_db_gap_stats(zmetad_db_t *db, const char *dataset,
    long long counts[3]);

/*
 * Record (or refresh) a dataset's mountpoint in the datasets table,
 * stamping last_seen with the current wall-clock second.  Called
 * each collect; INSERT OR REPLACE keeps one row per dataset so
 * mountpoint changes self-heal.
 */
int zmetad_db_upsert_mountpoint(zmetad_db_t *db, const char *dataset,
    const char *mountpoint);

/*
 * Delete datasets rows not refreshed since "cycle_start" (wall-clock
 * seconds, as stamped by zmetad_db_upsert_mountpoint): one
 * parameterized statement per poll cycle, pruning datasets whose
 * events were disabled or that were destroyed.
 */
int zmetad_db_prune_stale_datasets(zmetad_db_t *db, int64_t cycle_start);

/* Cleanup events older than retention_days */
int zmetad_db_cleanup(zmetad_db_t *db, int retention_days);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMETAD_H */
