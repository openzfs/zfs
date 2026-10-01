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

/*
 * zmetad - ZFS Metadata Export Daemon
 *
 * Polls ZFS datasets for file events and exports them to a SQLite database
 * for efficient querying, retention management, and external tool integration.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <stdarg.h>
#include <syslog.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <libnvpair.h>

#include "zmetad.h"
#include "zmetad_schema.h"
#include "schema_blob.h"

/* Global state */
static libzfs_handle_t *g_zfs;
static zmetad_config_t g_config;
static zmetad_schema_t *g_schema;
static zmetad_db_t *g_db;
static volatile sig_atomic_t g_shutdown = 0;
static volatile sig_atomic_t g_reload = 0;
static volatile sig_atomic_t g_force_collect = 0;
static boolean_t g_daemonized = B_FALSE;

static void
signal_handler(int sig)
{
	switch (sig) {
	case SIGTERM:
	case SIGINT:
		g_shutdown = 1;
		break;
	case SIGHUP:
		g_reload = 1;
		break;
	}
}

static void
on_sigusr1(int sig)
{
	(void) sig;
	g_force_collect = 1;
}

/*
 * Warn channel for loss/regression events.  After daemonize() stderr
 * is /dev/null, so a fprintf there is invisible and the only realtime
 * loss signal would be lost with it; mirror the message to syslog in
 * that case.  In the foreground stderr is the real terminal (and the
 * e2e suite reads it via journalctl), so keep it.
 */
static void
zmd_warn(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);

	if (g_daemonized) {
		va_start(ap, fmt);
		vsyslog(LOG_WARNING, fmt, ap);
		va_end(ap);
	}
}

static void
setup_signals(void)
{
	struct sigaction sa;

	memset(&sa, 0, sizeof (sa));
	sa.sa_handler = signal_handler;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGTERM, &sa, NULL);
	sigaction(SIGINT, &sa, NULL);
	sigaction(SIGHUP, &sa, NULL);

	sa.sa_handler = on_sigusr1;
	sigaction(SIGUSR1, &sa, NULL);

	/* Ignore SIGPIPE */
	sa.sa_handler = SIG_IGN;
	sigaction(SIGPIPE, &sa, NULL);
}

static void
config_init(zmetad_config_t *cfg)
{
	memset(cfg, 0, sizeof (*cfg));
	cfg->poll_interval = ZMETAD_DEFAULT_POLL_INTERVAL;
	cfg->retention_days = ZMETAD_DEFAULT_RETENTION_DAYS;
	cfg->max_size_mb = ZMETAD_DEFAULT_MAX_SIZE_MB;
	strlcpy(cfg->db_path, ZMETAD_DEFAULT_DB_PATH, sizeof (cfg->db_path));
	cfg->schema_path[0] = '\0';
	cfg->foreground = B_FALSE;
	cfg->verbose = 0;
	cfg->export_schema_path = NULL;
	cfg->check_schema_path = NULL;
	cfg->purge_dataset = NULL;
	cfg->force = B_FALSE;
}

/*
 * One-shot mode: write the embedded canonical schema JSON to
 * cfg->export_schema_path.  Refuses to clobber an existing file unless
 * --force was given.  Never enters the polling loop.
 */
static int
run_export_schema(const zmetad_config_t *cfg)
{
	FILE *fp;

	if (!cfg->force) {
		struct stat st;

		if (stat(cfg->export_schema_path, &st) == 0) {
			fprintf(stderr, "refusing to overwrite existing "
			    "file %s (use --force)\n",
			    cfg->export_schema_path);
			return (1);
		}
		if (errno != ENOENT) {
			fprintf(stderr, "cannot stat %s: %s\n",
			    cfg->export_schema_path, strerror(errno));
			return (1);
		}
	}

	fp = fopen(cfg->export_schema_path, "w");
	if (fp == NULL) {
		fprintf(stderr, "cannot open %s for writing: %s\n",
		    cfg->export_schema_path, strerror(errno));
		return (1);
	}

	if (fwrite(ZMETAD_EMBEDDED_SCHEMA_JSON, 1,
	    strlen(ZMETAD_EMBEDDED_SCHEMA_JSON), fp) !=
	    strlen(ZMETAD_EMBEDDED_SCHEMA_JSON)) {
		fprintf(stderr, "short write to %s: %s\n",
		    cfg->export_schema_path, strerror(errno));
		(void) fclose(fp);
		return (1);
	}
	if (fclose(fp) != 0) {
		fprintf(stderr, "error closing %s: %s\n",
		    cfg->export_schema_path, strerror(errno));
		return (1);
	}
	if (chmod(cfg->export_schema_path, 0644) != 0) {
		fprintf(stderr, "cannot chmod %s: %s\n",
		    cfg->export_schema_path, strerror(errno));
		return (1);
	}

	printf("schema written to %s\n", cfg->export_schema_path);
	return (0);
}

/*
 * One-shot mode: validate a schema file against the embedded canonical
 * schema.  Reports version drift by name.  Never enters the polling
 * loop.
 */
static int
run_check_schema(const zmetad_config_t *cfg)
{
	char errbuf[256];
	char eb2[256];
	zmetad_schema_t *zs;
	zmetad_schema_t *emb;
	uint64_t file_version;
	int rc;

	zs = zmetad_schema_load(cfg->check_schema_path, errbuf);
	if (zs == NULL) {
		fprintf(stderr, "schema check failed: %s\n", errbuf);
		return (1);
	}

	file_version = zmetad_schema_version(zs);

	emb = zmetad_schema_load(NULL, eb2);
	if (emb == NULL) {
		fprintf(stderr, "schema check failed: embedded schema "
		    "unusable: %s\n", eb2);
		zmetad_schema_free(zs);
		return (1);
	}

	/*
	 * The drift check must require an EXACT version match: unlike
	 * the wire path (where an older kernel is tolerated because
	 * schema versions only add fields), an on-disk schema file
	 * older than the embedded one means the file is stale and
	 * would silently mask the schema this daemon was built with.
	 */
	if (file_version == zmetad_schema_version(emb)) {
		printf("schema file OK (version %llu)\n",
		    (u_longlong_t)file_version);
		rc = 0;
	} else {
		fprintf(stderr, "schema version mismatch: file %s has "
		    "version %llu, embedded schema is version %llu\n",
		    cfg->check_schema_path, (u_longlong_t)file_version,
		    (u_longlong_t)zmetad_schema_version(emb));
		rc = 1;
	}

	zmetad_schema_free(emb);
	zmetad_schema_free(zs);
	return (rc);
}

/*
 * One-shot mode: delete every events/gaps/sync_state row belonging to
 * cfg->purge_dataset, then clear the dataset's kernel event ring.
 * Exits after: 0 success, 2 unknown dataset, 1 lzc or database error.
 * Never enters the polling loop.
 */
static int
run_purge(const zmetad_config_t *cfg)
{
	const char *ds = cfg->purge_dataset;
	long long counts[4];
	int err;

	if (!zfs_dataset_exists(g_zfs, ds, ZFS_TYPE_FILESYSTEM)) {
		fprintf(stderr, "cannot purge %s: dataset not found\n", ds);
		return (2);
	}

	err = zmetad_db_purge_dataset(g_db, ds, counts);
	if (err != 0) {
		fprintf(stderr, "cannot purge %s: database error\n", ds);
		return (1);
	}

	/*
	 * libzfs_init() already ran libzfs_core_init(); the extra call
	 * here is a harmless refcount bump kept for clarity that this
	 * path uses lzc ioctls directly.
	 */
	err = libzfs_core_init();
	if (err != 0) {
		fprintf(stderr, "cannot purge %s: libzfs_core init "
		    "failed: %s\n", ds, strerror(err));
		return (1);
	}

	nvlist_t *outnvl = NULL;
	err = lzc_clear_events(ds, &outnvl);
	if (outnvl != NULL)
		nvlist_free(outnvl);
	if (err == ENOENT) {
		fprintf(stderr, "cannot purge %s: dataset not found\n", ds);
		return (2);
	}
	if (err != 0) {
		fprintf(stderr, "cannot purge %s: kernel ring clear "
		    "failed: %s\n", ds, strerror(err));
		return (1);
	}

	/*
	 * Bump the purge epoch so a LIVE daemon re-arms its in-memory
	 * loss state (watermark + records_lost baseline) on its next
	 * poll: this process cannot reset another process's loss_state,
	 * and without a signal the daemon would fire a spurious
	 * regression gap for the deliberately-removed history.
	 */
	{
		uint64_t epoch = 0;

		if (zmetad_db_get_purge_epoch(g_db, &epoch) != 0)
			epoch = 0;
		if (zmetad_db_set_purge_epoch(g_db, epoch + 1) != 0) {
			fprintf(stderr, "cannot purge %s: cannot record "
			    "purge epoch\n", ds);
			return (1);
		}
	}

	printf("purged %s: %lld events, %lld gaps removed; "
	    "kernel ring cleared\n", ds, counts[0], counts[1]);
	return (0);
}

/*
 * Per-dataset loss-detection state: the previous poll's records_lost
 * counter and the highest watermark ever seen (so a regression can
 * be distinguished from the kernel's offset-0 "log exhausted"
 * sentinel).  The records_lost baseline is also stored in
 * sync_state.last_lost; this cache is only the process-local copy.
 * Keyed by dataset name; entries live for the process lifetime
 * (datasets are few).
 */
struct loss_state {
	char		*dataset;
	uint64_t	last_lost;
	boolean_t	have_lost;
	uint64_t	high_water;
	boolean_t	have_high_water;
	uint64_t	stored_guid;
	uint64_t	epoch;
	boolean_t	have_epoch;
	struct loss_state *next;
};

static struct loss_state *g_loss_states;

static struct loss_state *
loss_state_get(const char *dataset)
{
	struct loss_state *ls;

	for (ls = g_loss_states; ls != NULL; ls = ls->next) {
		if (strcmp(ls->dataset, dataset) == 0)
			return (ls);
	}

	ls = calloc(1, sizeof (*ls));
	if (ls == NULL)
		return (NULL);
	ls->dataset = strdup(dataset);
	if (ls->dataset == NULL) {
		free(ls);
		return (NULL);
	}
	ls->next = g_loss_states;
	g_loss_states = ls;
	return (ls);
}

/*
 * Detect event-log loss for this poll: a records_lost delta against
 * the previous poll (ring wrap or drain-queue overflow) or a
 * watermark regression (next_offset below the highest offset ever
 * observed, after the kernel's offset-0 exhaustion sentinel is
 * excluded).  Records a gap row and warns on stderr; returns the
 * lost count (0 = no loss).
 */
static uint64_t
detect_loss(const char *dataset, zmetad_db_t *db, struct loss_state *ls,
    uint64_t last_offset, uint64_t next_offset, uint64_t records_lost)
{
	uint64_t lost_delta = 0;
	uint64_t regression_from = 0;
	boolean_t regression = B_FALSE;

	if (ls->have_lost && records_lost >= ls->last_lost)
		lost_delta = records_lost - ls->last_lost;
	ls->last_lost = records_lost;
	ls->have_lost = B_TRUE;

	if (ls->have_high_water && next_offset != 0 &&
	    next_offset < ls->high_water) {
		regression = B_TRUE;
		regression_from = ls->high_water;
	} else if (last_offset != 0 && next_offset != 0 &&
	    next_offset < last_offset) {
		/*
		 * Covers the first poll after a restart whose stored
		 * watermark predates a ring reset: the in-memory
		 * high-water mark is gone but sync_state is not.
		 */
		regression = B_TRUE;
		regression_from = last_offset;
	}
	if (next_offset != 0 &&
	    (!ls->have_high_water || next_offset > ls->high_water)) {
		ls->high_water = next_offset;
		ls->have_high_water = B_TRUE;
	}

	if (lost_delta == 0 && !regression)
		return (0);

	if (regression) {
		/*
		 * Move the watermark down to the new cursor. Leaving
		 * it above next_offset makes every later poll look
		 * like another regression.
		 */
		ls->high_water = next_offset;
		ls->have_high_water = (next_offset != 0);
		zmd_warn("event log watermark regression on %s: "
		    "next_offset=%llu below high-water %llu; records "
		    "since offset %llu were not captured\n",
		    dataset, (unsigned long long)next_offset,
		    (unsigned long long)regression_from,
		    (unsigned long long)next_offset);
	} else {
		zmd_warn("event log loss on %s: %llu records lost "
		    "since last poll (cumulative %llu); records from "
		    "logical offset %llu onward were affected\n",
		    dataset, (unsigned long long)lost_delta,
		    (unsigned long long)records_lost,
		    (unsigned long long)last_offset);
	}

	/*
	 * Sole loss-path insert: writes the cumulative-delta
	 * count, so gaps.lost > 0 is the true count of records
	 * lost since the previous poll, and gaps.lost == 0 means
	 * the only signal was a watermark regression (count
	 * unknown).  Never writes the -1 sentinel; that is
	 * emitted only by the ring-replace path below.
	 */
	(void) zmetad_db_insert_gap(db, dataset,
	    regression ? next_offset : last_offset,
	    regression ? regression_from : next_offset,
	    lost_delta);

	return (lost_delta > 0 ? lost_delta : 1);
}

static int
collect_dataset_events(const char *dataset, zmetad_db_t *db)
{
	nvlist_t *events = NULL;
	uint64_t last_offset;
	struct loss_state *ls;
	int err;
	boolean_t insert_failed = B_FALSE;

	ls = loss_state_get(dataset);
	if (ls == NULL)
		return (ENOMEM);

	/*
	 * First sighting this process: reload the records_lost
	 * baseline.  A wrap while this daemon was down is then a
	 * delta against the stored counter, not a silent re-arm.
	 * NULL means no baseline yet.
	 */
	if (!ls->have_lost) {
		uint64_t stored_lost;

		if (zmetad_db_get_last_lost(db, dataset,
		    &stored_lost) == 0) {
			ls->last_lost = stored_lost;
			ls->have_lost = B_TRUE;
		}
	}

	/*
	 * A concurrent `zmetad --purge` deletes sync_state but cannot
	 * reach this process's in-memory loss state: without a re-arm
	 * the next legitimate post-purge offset would sit below the
	 * old high-water mark and fire a spurious regression gap.  The
	 * purge epoch moves on every purge, so a changed value means
	 * this dataset's history may have been removed and both the
	 * watermark and the loss baseline must start over.
	 */
	{
		uint64_t epoch = 0;

		if (zmetad_db_get_purge_epoch(db, &epoch) == 0) {
			if (ls->have_epoch && epoch != ls->epoch) {
				ls->last_lost = 0;
				ls->have_lost = B_FALSE;
				ls->high_water = 0;
				ls->have_high_water = B_FALSE;
			}
			ls->epoch = epoch;
			ls->have_epoch = B_TRUE;
		}
	}

	/* Get last synced offset for this dataset */
	last_offset = zmetad_db_get_last_offset(db, dataset);

	/* Fetch events since last offset */
	err = lzc_get_events(dataset, 0, last_offset, &events);
	if (err != 0) {
		if (err == ENOENT) {
			/* Dataset doesn't exist or events not enabled */
			return (0);
		}
		zmd_warn("Failed to get events for %s: %s\n",
		    dataset, strerror(err));
		return (err);
	}

	if (events == NULL) {
		return (0);
	}

	/*
	 * Ring identity: the reply's ring_guid identifies the kernel
	 * event log instance.  A change against a previously stored
	 * (nonzero) guid means the ring was replaced (destroy/
	 * recreate, receive) and every record before this poll
	 * belonged to a different log.  A reply without the key is a
	 * legacy kernel: no identity logic at all.
	 */
	uint64_t ring_guid = 0;
	int guid_err = nvlist_lookup_uint64(events, "ring_guid", &ring_guid);
	boolean_t have_guid = (guid_err == 0 && ring_guid != 0);

	if (have_guid) {
		uint64_t stored = ls->stored_guid;

		if (stored == 0) {
			/*
			 * First sighting this process: fall back to
			 * the stored value so a daemon restart does
			 * not mistake a long-lived ring for a new one.
			 */
			stored = zmetad_db_get_ring_guid(db, dataset);
			ls->stored_guid = stored;
		}
		if (stored != 0 && stored != ring_guid) {
			fprintf(stderr, "ring replaced on %s: guid %llu "
			    "-> %llu; history before offset %llu belongs "
			    "to the previous log\n", dataset,
			    (u_longlong_t)stored, (u_longlong_t)ring_guid,
			    (u_longlong_t)((ls->have_high_water &&
			    ls->high_water > last_offset) ?
			    ls->high_water : last_offset));
			/*
			 * Ring-replace path: the previous log's
			 * history is uncountably gone, so this
			 * writes the lost = -1 sentinel (stored
			 * as (uint64_t)-1) -- the only site that
			 * may emit it.  A -1 row means "ring
			 * replaced, count unknown", never a
			 * numeric loss.
			 */
			(void) zmetad_db_insert_gap(db, dataset, 0,
			    last_offset, (uint64_t)-1);
			/*
			 * The reply's records are from the NEW ring,
			 * so the watermark must restart at 0 and the
			 * loss_state counter baseline must re-arm on
			 * the new ring's records_lost, or the next
			 * poll fires a bogus delta.  The watermark
			 * write below persists offset 0 + the new
			 * guid together.
			 */
			last_offset = 0;
			ls->last_lost = 0;
			ls->have_lost = B_FALSE;
			ls->high_water = 0;
			ls->have_high_water = B_FALSE;
		}
		ls->stored_guid = ring_guid;
	}

	/*
	 * Clear does not rotate the ring GUID (that identity survives
	 * a clear by contract) but it does reset logical eof to 0.
	 * A stored cursor past the current eof is a cleared ring:
	 * record the hole and resume at 0, or collection stalls until
	 * the new log grows past the pre-clear cursor and skips
	 * everything written in between.
	 */
	{
		uint64_t log_eof = 0;

		if (nvlist_lookup_uint64(events, "log_eof", &log_eof) == 0 &&
		    last_offset > log_eof) {
			fprintf(stderr, "event log cleared on %s: cursor "
			    "%llu past eof %llu; resuming at 0\n", dataset,
			    (unsigned long long)last_offset,
			    (unsigned long long)log_eof);
			(void) zmetad_db_insert_gap(db, dataset, log_eof,
			    last_offset, 0);
			last_offset = 0;
			ls->last_lost = 0;
			ls->have_lost = B_FALSE;
			ls->high_water = 0;
			ls->have_high_water = B_FALSE;
		}
	}

	/*
	 * Negotiate the record schema version.  Kernels that predate
	 * schema version exposure omit the key; wire == 0 is treated as
	 * compatible with any loaded schema.
	 */
	uint64_t wire_version = 0;
	(void) nvlist_lookup_uint64(events, "schema_version", &wire_version);
	if (zmetad_schema_check_version(g_schema, wire_version) != 0) {
		fprintf(stderr, "schema version mismatch: daemon=%llu "
		    "wire=%llu\n",
		    (unsigned long long)zmetad_schema_version(g_schema),
		    (unsigned long long)wire_version);
		nvlist_free(events);
		return (EINVAL);
	}

	/*
	 * The reply nests the records under "events": each child nvlist
	 * is one event record. Iterate that child, not the outer reply
	 * (whose only nvlist-valued pair would otherwise be mistaken
	 * for a single event and yield empty rows).
	 */
	nvlist_t *records = NULL;
	if (nvlist_lookup_nvlist(events, "events", &records) != 0)
		records = NULL;

	uint_t count = 0;
	nvpair_t *elem = NULL;
	nvlist_t *event;

	while (records != NULL &&
	    (elem = nvlist_next_nvpair(records, elem)) != NULL) {
		if (nvpair_type(elem) != DATA_TYPE_NVLIST)
			continue;

		if (nvpair_value_nvlist(elem, &event) != 0)
			continue;

		err = zmetad_db_insert_event(db, dataset, event);
		if (err != 0) {
			fprintf(stderr, "Failed to insert event: %s\n",
			    strerror(err));
			insert_failed = B_TRUE;
		} else {
			count++;
		}
	}

	if (count > 0 && g_config.verbose) {
		printf("Collected %u events from %s\n", count, dataset);
	}

	/*
	 * Loss detection BEFORE advancing the watermark: compare the
	 * cumulative records_lost with the previous poll and check the
	 * returned watermark against the high-water mark.  The
	 * surviving records above are still inserted; the gap row
	 * records what was lost between the previous watermark and
	 * this poll's.
	 */
	uint64_t records_lost = 0;
	uint64_t next_offset = 0;

	(void) nvlist_lookup_uint64(events, "records_lost", &records_lost);
	(void) nvlist_lookup_uint64(events, "next_offset", &next_offset);

	if (detect_loss(dataset, db, ls, last_offset, next_offset,
	    records_lost) > 0 && g_config.verbose) {
		printf("Loss detected on %s; gap recorded\n", dataset);
	}

	/*
	 * Advance the watermark from the returned next_offset,
	 * persisting the ring identity in the same write.  When the
	 * ring was replaced above, last_offset was reset to 0: the
	 * new ring's records start from its own offset space.  On a
	 * legacy reply (no guid) the stored identity is left alone
	 * (0 binds NULL, and INSERT OR REPLACE keeps last_sync
	 * fresh without disturbing identity tracking).
	 */
	if (next_offset > 0 && !insert_failed) {
		zmetad_db_set_last_offset(db, dataset, next_offset,
		    have_guid ? ring_guid : 0);
	} else if (have_guid) {
		/*
		 * Even with nothing new to sync, remember the
		 * identity once seen so a later swap is detectable.
		 */
		zmetad_db_set_last_offset(db, dataset, last_offset,
		    ring_guid);
	}

	/*
	 * Persist the baseline even when the cursor did not move.
	 * A wrap that only bumps records_lost must still be visible
	 * after a restart.
	 */
	if (ls->have_lost)
		(void) zmetad_db_set_last_lost(db, dataset, ls->last_lost);

	nvlist_free(events);
	return (0);
}

static int
collect_callback(zfs_handle_t *zhp, void *arg)
{
	zmetad_db_t *db = arg;
	const char *name = zfs_get_name(zhp);
	uint64_t events_enabled;

	/* Check if events are enabled on this dataset */
	events_enabled = zfs_prop_get_int(zhp, ZFS_PROP_EVENTS);
	if (events_enabled) {
		char mountpoint[512];

		/*
		 * Record the mountpoint for path -> dataset
		 * resolution.  Non-filesystem datasets yield
		 * "-" or "legacy"; store as-is (consumers filter
		 * by '/' prefix).  Skip silently on error.
		 */
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0) {
			if (zmetad_db_upsert_mountpoint(db, name,
			    mountpoint) != 0 && g_config.verbose) {
				fprintf(stderr, "failed to record "
				    "mountpoint for %s\n", name);
			}
		} else if (g_config.verbose) {
			fprintf(stderr, "no mountpoint for %s\n", name);
		}

		collect_dataset_events(name, db);

		/*
		 * Verbose summary: gaps rows by sentinel class for
		 * this dataset (ring replacements, regressions,
		 * recorded loss counts).  Gaps are never deleted
		 * by retention, so these are lifetime counts.
		 */
		if (g_config.verbose) {
			long long gap_counts[3];

			if (zmetad_db_gap_stats(db, name,
			    gap_counts) == 0) {
				printf("%s: gaps: %lld ring replacement(s), "
				    "%lld regression(s), %lld loss record(s)\n",
				    name, gap_counts[0], gap_counts[1],
				    gap_counts[2]);
			}
		}
	}

	/* Recurse into children */
	zfs_iter_filesystems_v2(zhp, 0, collect_callback, arg);

	zfs_close(zhp);
	return (0);
}

static void
collect_all_events(zmetad_db_t *db)
{
	/* Iterate all pools and datasets */
	zfs_iter_root(g_zfs, collect_callback, db);
}

static void
daemon_loop(zmetad_db_t *db)
{
	time_t last_collect = 0;
	time_t last_cleanup = 0;

	while (!g_shutdown) {
		time_t now = time(NULL);

		/* Reload config on SIGHUP */
		if (g_reload) {
			g_reload = 0;
			/* TODO: reload config file */
			if (g_config.verbose) {
				printf("Configuration reloaded\n");
			}
		}

		/*
		 * Forced collect: SIGUSR1 requests an out-of-band
		 * cycle, independent of the poll interval.  Drain in
		 * a loop so multiple pending signals (they do not
		 * queue as a count) do not leave a stale flag behind.
		 */
		while (g_force_collect) {
			g_force_collect = 0;
			if (g_config.verbose) {
				printf("forced collect (SIGUSR1)\n");
			}
			collect_all_events(db);
			last_collect = now;
		}

		/* Collect events at poll interval */
		if (now - last_collect >= g_config.poll_interval) {
			collect_all_events(db);
			last_collect = now;
		}

		/* Cleanup old events daily */
		if (now - last_cleanup >= 86400) {
			if (g_config.retention_days > 0) {
				zmetad_db_cleanup(db, g_config.retention_days);
			}
			last_cleanup = now;
		}

		/* Sleep for a bit */
		sleep(1);
	}
}

static void
daemonize(void)
{
	pid_t pid;

	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		/* Parent exits */
		exit(EXIT_SUCCESS);
	}

	/* Create new session */
	if (setsid() < 0) {
		perror("setsid");
		exit(EXIT_FAILURE);
	}

	/* Fork again to prevent acquiring a controlling terminal */
	pid = fork();
	if (pid < 0) {
		perror("fork");
		exit(EXIT_FAILURE);
	}
	if (pid > 0) {
		exit(EXIT_SUCCESS);
	}

	/* Set working directory */
	if (chdir("/") < 0) {
		perror("chdir");
		exit(EXIT_FAILURE);
	}

	/* Close standard file descriptors */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	/* Redirect to /dev/null */
	open("/dev/null", O_RDONLY);
	open("/dev/null", O_WRONLY);
	open("/dev/null", O_WRONLY);

	/*
	 * stderr now goes to /dev/null, so zmd_warn mirrors loss
	 * warnings into syslog from here on.
	 */
	g_daemonized = B_TRUE;
	openlog("zmetad", LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

static void
usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [options]\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -c, --config <file>    Config file path\n");
	fprintf(stderr, "  -d, --database <path>  SQLite database path\n");
	fprintf(stderr, "  -f, --foreground       Run in foreground\n");
	fprintf(stderr, "  -i, --interval <sec>   Poll interval "
	    "(default: %d)\n", ZMETAD_DEFAULT_POLL_INTERVAL);
	fprintf(stderr, "  -r, --retention <days> Retention days "
	    "(default: %d)\n", ZMETAD_DEFAULT_RETENTION_DAYS);
	fprintf(stderr, "  --schema <file>        Event schema JSON path "
	    "(default: embedded)\n");
	fprintf(stderr, "  --export-schema <file> Write embedded schema "
	    "JSON to file and exit\n");
	fprintf(stderr, "  --check-schema <file>  Validate schema file "
	    "against embedded and exit\n");
	fprintf(stderr, "  --purge <dataset>      Delete dataset's stored "
	    "events/gaps/sync_state rows,\n");
	fprintf(stderr, "                         clear its kernel event "
	    "ring, and exit\n");
	fprintf(stderr, "  --force                Allow --export-schema to "
	    "overwrite existing file\n");
	fprintf(stderr, "  -v, --verbose          Verbose output\n");
	fprintf(stderr, "  -h, --help             Show this help\n");
}

static struct option longopts[] = {
	{ "config",		required_argument,	NULL,	'c' },
	{ "database",		required_argument,	NULL,	'd' },
	{ "export-schema",	required_argument,	NULL,	'e' },
	{ "check-schema",	required_argument,	NULL,	'k' },
	{ "purge",		required_argument,	NULL,	'p' },
	{ "force",		no_argument,		NULL,	0x100 },
	{ "foreground",		no_argument,		NULL,	'f' },
	{ "interval",		required_argument,	NULL,	'i' },
	{ "retention",		required_argument,	NULL,	'r' },
	{ "schema",		required_argument,	NULL,	's' },
	{ "verbose",		no_argument,		NULL,	'v' },
	{ "help",		no_argument,		NULL,	'h' },
	{ NULL,			0,			NULL,	0 }
};

int
main(int argc, char **argv)
{
	zmetad_db_t *db = NULL;
	int opt;
	int err;

	config_init(&g_config);

	while ((opt = getopt_long(argc, argv, "c:d:fe:i:k:p:r:s:vh", longopts,
	    NULL)) != -1) {
		switch (opt) {
		case 'c':
			/* TODO: load config file */
			break;
		case 'd':
			strlcpy(g_config.db_path, optarg,
			    sizeof (g_config.db_path));
			break;
		case 'e':
			if (g_config.export_schema_path != NULL) {
				fprintf(stderr, "--export-schema given "
				    "multiple times\n");
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			g_config.export_schema_path = optarg;
			break;
		case 'k':
			if (g_config.check_schema_path != NULL) {
				fprintf(stderr, "--check-schema given "
				    "multiple times\n");
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			g_config.check_schema_path = optarg;
			break;
		case 'p':
			if (g_config.purge_dataset != NULL) {
				fprintf(stderr, "--purge given multiple "
				    "times\n");
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			g_config.purge_dataset = optarg;
			break;
		case 0x100:
			g_config.force = B_TRUE;
			break;
		case 'f':
			g_config.foreground = B_TRUE;
			break;
		case 'i':
		case 'r': {
			char *end = NULL;
			long val;

			errno = 0;
			val = strtol(optarg, &end, 10);
			if (errno != 0 || end == optarg || *end != '\0') {
				fprintf(stderr, "invalid numeric "
				    "argument '%s' for -%c\n", optarg,
				    opt);
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			/*
			 * -i: a poll interval below 1 makes the loop
			 * spin; -r: only a non-negative count is
			 * meaningful (0 disables retention), so a
			 * negative value is a typo, not "disabled" --
			 * reject it instead of silently clamping.
			 */
			if (val < (opt == 'i' ? 1 : 0) ||
			    val > INT_MAX) {
				fprintf(stderr, "argument for -%c must "
				    "be %s\n", opt,
				    opt == 'i' ? ">= 1" : ">= 0");
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			if (opt == 'i')
				g_config.poll_interval = (int)val;
			else
				g_config.retention_days = (int)val;
			break;
		}
		case 's':
			strlcpy(g_config.schema_path, optarg,
			    sizeof (g_config.schema_path));
			break;
		case 'v':
			g_config.verbose++;
			break;
		case 'h':
		default:
			usage(argv[0]);
			return (opt == 'h' ? EXIT_SUCCESS : EXIT_FAILURE);
		}
	}

	/*
	 * getopt_long eats the next word as the argument of a
	 * required_argument option even when that word is another
	 * option, so "zmetad --export-schema --force /path.json"
	 * stores the path as "--force" and leaves "/path.json" as a
	 * stray positional.  Recover: if a stored path is literally
	 * "--force", the flag was meant for us and the real path is
	 * the leftover positional.
	 */
	if ((g_config.export_schema_path != NULL &&
	    strcmp(g_config.export_schema_path, "--force") == 0) ||
	    (g_config.check_schema_path != NULL &&
	    strcmp(g_config.check_schema_path, "--force") == 0)) {
		char **pathp = (g_config.export_schema_path != NULL) ?
		    &g_config.export_schema_path : &g_config.check_schema_path;

		g_config.force = B_TRUE;
		*pathp = NULL;
		if (optind >= argc) {
			fprintf(stderr, "--%s requires a file path\n",
			    (pathp == &g_config.export_schema_path) ?
			    "export-schema" : "check-schema");
			return (EXIT_FAILURE);
		}
		*pathp = argv[optind];
		optind++;
	}

	/* One-shot schema modes: run and exit before any daemon setup */
	if (g_config.export_schema_path != NULL) {
		return (run_export_schema(&g_config));
	}
	if (g_config.check_schema_path != NULL) {
		return (run_check_schema(&g_config));
	}

	/* Initialize libzfs */
	g_zfs = libzfs_init();
	if (g_zfs == NULL) {
		fprintf(stderr, "Failed to initialize libzfs\n");
		return (EXIT_FAILURE);
	}

	/* Load the event schema before opening the database */
	{
		char errbuf[256];
		const char *path = (g_config.schema_path[0] != '\0') ?
		    g_config.schema_path : NULL;

		g_schema = zmetad_schema_load(path, errbuf);
		if (g_schema == NULL) {
			fprintf(stderr, "Failed to load event schema: %s\n",
			    errbuf);
			libzfs_fini(g_zfs);
			return (EXIT_FAILURE);
		}
	}

	/* Open/create database */
	err = zmetad_db_open(&db, g_config.db_path, g_schema);
	if (err != 0) {
		fprintf(stderr, "Failed to open database: %s\n",
		    strerror(err));
		libzfs_fini(g_zfs);
		return (EXIT_FAILURE);
	}

	/* One-shot purge mode: no signals, no daemonize, no loop. */
	if (g_config.purge_dataset != NULL) {
		g_db = db;
		int prc = run_purge(&g_config);
		zmetad_db_close(db);
		zmetad_schema_free(g_schema);
		libzfs_fini(g_zfs);
		return (prc == 0 ? EXIT_SUCCESS :
		    (prc == 1 ? EXIT_FAILURE : prc));
	}

	/* Setup signal handlers */
	setup_signals();

	/* Daemonize unless foreground mode */
	if (!g_config.foreground) {
		daemonize();
	}

	if (g_config.verbose) {
		printf("zmetad starting (poll=%ds, retention=%dd, db=%s)\n",
		    g_config.poll_interval, g_config.retention_days,
		    g_config.db_path);
	}

	/* Main loop */
	daemon_loop(db);

	if (g_config.verbose) {
		printf("zmetad shutting down\n");
	}

	/* Cleanup */
	if (g_daemonized)
		closelog();
	zmetad_db_close(db);
	zmetad_schema_free(g_schema);
	libzfs_fini(g_zfs);

	return (EXIT_SUCCESS);
}
