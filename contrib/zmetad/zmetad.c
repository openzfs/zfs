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
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <pthread.h>
#include <syslog.h>
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

/*
 * Daemon-path diagnostics: stderr is /dev/null once daemonized, so
 * mirror warnings to syslog when not running in the foreground.
 * One-shot CLI modes run before openlog() with a live stderr and
 * use fprintf directly.
 */
static void
daemon_warn(const char *fmt, ...)
{
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	(void) vsnprintf(buf, sizeof (buf), fmt, ap);
	va_end(ap);

	(void) fputs(buf, stderr);
	if (!g_config.foreground)
		syslog(LOG_WARNING, "%s", buf);
}

static void
db_warn_sink(const char *msg)
{
	/*
	 * daemon_warn takes a printf-style format; msg is data, so go
	 * through "%s" to keep any embedded '%' literal.
	 */
	daemon_warn("%s", msg);
}

/*
 * Monotonic wall-clock-free seconds for interval scheduling: an NTP
 * step or DST change must not stretch or shrink the poll interval
 * (which would widen the ring-wrap window).  time(NULL) remains the
 * source for captured_at / last_seen wall-clock values.
 */
static uint64_t
mono_secs(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
		return (0);
	return ((uint64_t)ts.tv_sec);
}

static void
config_init(zmetad_config_t *cfg)
{
	memset(cfg, 0, sizeof (*cfg));
	cfg->poll_interval = ZMETAD_DEFAULT_POLL_INTERVAL;
	cfg->retention_days = ZMETAD_DEFAULT_RETENTION_DAYS;
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
 *
 * Existence is decided by open(O_CREAT|O_EXCL) itself, not by a
 * stat()-then-fopen() pair: the TOCTOU window between the two would
 * let a symlink or a racing create redirect the write (this runs as
 * root in some deployments).
 */
static int
run_export_schema(const zmetad_config_t *cfg)
{
	FILE *fp;
	int fd;

	fd = open(cfg->export_schema_path, O_WRONLY | O_CREAT | O_EXCL,
	    0644);
	if (fd < 0 && errno == EEXIST && cfg->force) {
		fd = open(cfg->export_schema_path, O_WRONLY | O_TRUNC);
	}
	if (fd < 0) {
		if (errno == EEXIST) {
			fprintf(stderr, "refusing to overwrite existing "
			    "file %s (use --force)\n",
			    cfg->export_schema_path);
		} else {
			fprintf(stderr, "cannot open %s for writing: %s\n",
			    cfg->export_schema_path, strerror(errno));
		}
		return (1);
	}

	fp = fdopen(fd, "w");
	if (fp == NULL) {
		fprintf(stderr, "cannot open %s for writing: %s\n",
		    cfg->export_schema_path, strerror(errno));
		(void) close(fd);
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
 * schema.  Reports version drift and field-level drift by name.  Never
 * enters the polling loop.
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
	 * A matching version alone is not enough: the file's fields
	 * are compared too, so a hand-edited file that bumped the
	 * version but changed field names/types/order is rejected.
	 */
	if (file_version != zmetad_schema_version(emb)) {
		fprintf(stderr, "schema version mismatch: file %s has "
		    "version %llu, embedded schema is version %llu\n",
		    cfg->check_schema_path, (u_longlong_t)file_version,
		    (u_longlong_t)zmetad_schema_version(emb));
		rc = 1;
	} else if (zmetad_schema_compare(zs, emb, errbuf,
	    sizeof (errbuf)) != 0) {
		fprintf(stderr, "schema field mismatch: file %s differs "
		    "from the embedded schema: %s\n",
		    cfg->check_schema_path, errbuf);
		rc = 1;
	} else {
		printf("schema file OK (version %llu)\n",
		    (u_longlong_t)file_version);
		rc = 0;
	}

	zmetad_schema_free(emb);
	zmetad_schema_free(zs);
	return (rc);
}

/*
 * One-shot mode: record the dataset's purge epoch (durable marker)
 * FIRST, then clear the dataset's kernel event ring, then -- only on
 * success -- delete its events/gaps/sync_state rows (in one
 * transaction, see zmetad_db_purge_dataset).
 *
 * The epoch must be durable before the ring is cleared: a daemon
 * polling this dataset reads the ring, not the epoch, so a poll
 * landing after the clear but before the epoch bump would see a reset
 * ring with an unchanged epoch, keep its stale high-water mark, and
 * write a permanent lost=0 regression row into gaps.  Persisting the
 * marker first also means a crash after the clear cannot leave the
 * ring reset without it.
 *
 * The ring must be cleared before the rows: if the rows went first and
 * the ring clear then failed (or a live daemon polled in between), the
 * next poll would re-ingest the whole ring.
 * Exits after: 0 success, 2 unknown dataset, 1 lzc or database error.
 * Never enters the polling loop.
 *
 * No libzfs_core_init() here: libzfs_init() (called in main before
 * run_purge) already took a refcount on the /dev/zfs lzc handle, and
 * libzfs_fini() releases it.  A second unpaired init would leak the
 * refcount (libzfs_core.c g_refcount never reaches 0).
 */
static int
run_purge(const zmetad_config_t *cfg)
{
	const char *ds = cfg->purge_dataset;
	nvlist_t *outnvl = NULL;
	long long counts[4];
	int err;

	if (!zfs_dataset_exists(g_zfs, ds, ZFS_TYPE_FILESYSTEM)) {
		fprintf(stderr, "cannot purge %s: dataset not found\n", ds);
		return (2);
	}

	/*
	 * Durable epoch first.  Fail closed: a clear without a recorded
	 * epoch would reintroduce the phantom-regression window this
	 * ordering exists to close, so refuse rather than proceed.
	 */
	if (zmetad_db_bump_purge_epoch(g_db, ds) != 0) {
		fprintf(stderr, "cannot purge %s: could not record purge "
		    "epoch; refusing to clear the kernel ring\n", ds);
		return (1);
	}

	err = lzc_clear_events(ds, &outnvl);
	if (outnvl != NULL)
		nvlist_free(outnvl);
	if (err == ENOENT) {
		fprintf(stderr, "cannot purge %s: dataset not found\n", ds);
		return (2);
	}
	if (err != 0) {
		fprintf(stderr, "cannot purge %s: kernel ring clear "
		    "failed: %s; database left untouched\n",
		    ds, strerror(err));
		return (1);
	}

	err = zmetad_db_purge_dataset(g_db, ds, counts);
	if (err != 0) {
		fprintf(stderr, "cannot purge %s: database error "
		    "(kernel ring already cleared)\n", ds);
		return (1);
	}

	printf("purged %s: %lld events, %lld gaps removed; "
	    "kernel ring cleared\n", ds, counts[0], counts[1]);
	return (0);
}

/*
 * Per-dataset loss-detection state: the previous poll's records_lost
 * counter and the highest watermark ever seen (so a regression can
 * be distinguished from the kernel's offset-0 "log exhausted"
 * sentinel).  Keyed by dataset name; entries live for the process
 * lifetime (datasets are few).
 */
struct loss_state {
	char		*dataset;
	uint64_t	last_lost;
	boolean_t	have_lost;
	uint64_t	high_water;
	boolean_t	have_high_water;
	uint64_t	stored_guid;
	uint64_t	purge_epoch;
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
 * excluded).  Records a gap row and warns on stderr.
 *
 * Returns 0 when detection ran; *lostp then holds the lost count
 * (0 = no loss).  Returns EIO when the persisted baseline could not
 * be read and detection was skipped: the caller must NOT re-baseline
 * in that case, or a transient read error would swallow the loss
 * accumulated since the stored baseline -- the exact failure the
 * baseline read's EIO/ENOENT split exists to survive.
 *
 * A records_lost DECREASE below the previous baseline means the ring
 * was cleared or purged externally (zfs events -c, zmetad --purge
 * against a running daemon): the counter restarted, so the loss
 * baseline AND the high-water mark are both re-armed -- like the
 * ring-swap path -- and detection is skipped for this cycle instead
 * of emitting a spurious lost=0 regression row on every poll.
 */
static int
detect_loss(const char *dataset, zmetad_db_t *db, struct loss_state *ls,
    uint64_t last_offset, uint64_t next_offset, uint64_t records_lost,
    uint64_t *lostp)
{
	uint64_t lost_delta = 0;
	uint64_t regression_from = 0;
	boolean_t regression = B_FALSE;

	*lostp = 0;

	/*
	 * A --purge under a live daemon resets both the kernel counters
	 * and the DB rows; the epoch moves first, so re-arm before the
	 * counters are read rather than depending on spotting a
	 * decrease afterwards.  The epoch is per dataset (DB layout 7):
	 * purging one dataset does not re-arm the others.
	 */
	{
		uint64_t epoch = 0;

		if (zmetad_db_get_purge_epoch(db, dataset, &epoch) == 0 &&
		    epoch != ls->purge_epoch) {
			ls->purge_epoch = epoch;
			ls->have_lost = B_FALSE;
			ls->have_high_water = B_FALSE;
			ls->last_lost = 0;
			ls->high_water = 0;
		}
	}

	if (!ls->have_lost) {
		/*
		 * First observation of this dataset this process
		 * lifetime: restore the persisted baseline (DB
		 * layout 6). NULL/absent = no baseline; the first
		 * poll after ring creation invents no gap for
		 * pre-existing history. With a baseline, a wrap that
		 * happened while the daemon was down becomes a delta
		 * rather than a silent re-arm.  A query ERROR (EIO)
		 * is NOT "no baseline": skip loss detection this
		 * poll -- re-arming here would swallow the loss
		 * accumulated since the last persisted baseline.
		 */
		boolean_t have = B_FALSE;
		uint64_t stored = 0;

		if (zmetad_db_get_last_lost(db, dataset, &have,
		    &stored) == EIO) {
			daemon_warn("cannot read loss baseline for %s; "
			    "skipping loss detection this poll\n", dataset);
			return (EIO);
		}
		if (have) {
			ls->last_lost = stored;
			ls->have_lost = B_TRUE;
		}
	}

	if (!ls->have_lost) {
		/*
		 * Still no baseline (fresh ring): seed from this
		 * poll's counters without reporting anything.
		 */
		ls->last_lost = records_lost;
		ls->have_lost = B_TRUE;
		ls->high_water = 0;
		ls->have_high_water = B_FALSE;
		if (next_offset != 0) {
			ls->high_water = next_offset;
			ls->have_high_water = B_TRUE;
		}
		return (0);
	}

	if (ls->have_lost && records_lost < ls->last_lost) {
		/*
		 * Baseline (restored from the DB, layout 6, or held
		 * in memory) and the counter went DOWN: ring cleared
		 * or replaced; re-arm.
		 */
		goto external_rearm;
	}

	if (records_lost >= ls->last_lost)
		lost_delta = records_lost - ls->last_lost;
	ls->last_lost = records_lost;
	ls->have_lost = B_TRUE;
	goto have_baseline;

external_rearm:
	if (ls->have_lost && records_lost < ls->last_lost) {
		/* External clear/purge: baseline and high-water are stale. */
		daemon_warn("records_lost on %s decreased from %llu to %llu: "
		    "event ring cleared or replaced externally; re-arming "
		    "loss detection baseline\n", dataset,
		    (unsigned long long)ls->last_lost,
		    (unsigned long long)records_lost);
		ls->last_lost = records_lost;
		ls->have_lost = B_TRUE;
		ls->high_water = 0;
		ls->have_high_water = B_FALSE;
		if (next_offset != 0) {
			ls->high_water = next_offset;
			ls->have_high_water = B_TRUE;
		}
		return (0);
	}

have_baseline:
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
		daemon_warn("event log watermark regression on %s: "
		    "next_offset=%llu below high-water %llu; records "
		    "since offset %llu were not captured\n",
		    dataset, (unsigned long long)next_offset,
		    (unsigned long long)regression_from,
		    (unsigned long long)next_offset);
	} else {
		daemon_warn("event log loss on %s: %llu records lost "
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
	if (zmetad_db_insert_gap(db, dataset,
	    regression ? next_offset : last_offset,
	    regression ? regression_from : next_offset,
	    lost_delta) != 0) {
		daemon_warn("failed to record event-log gap for %s "
		    "(lost=%llu)\n", dataset,
		    (unsigned long long)lost_delta);
	}

	*lostp = lost_delta;
	return (0);
}

static int
collect_dataset_events(const char *dataset, zmetad_db_t *db)
{
	nvlist_t *events = NULL;
	nvlist_t *records = NULL;
	nvlist_t *event;
	nvpair_t *elem = NULL;
	uint64_t last_offset = 0;
	uint64_t ring_guid = 0;
	uint64_t records_lost = 0;
	uint64_t records_undecodable = 0;
	uint64_t next_offset = 0;
	boolean_t have_guid = B_FALSE;
	boolean_t refetched = B_FALSE;
	boolean_t insert_failed = B_FALSE;
	struct loss_state *ls;
	uint_t count = 0;
	int err;

	ls = loss_state_get(dataset);
	if (ls == NULL)
		return (ENOMEM);

	/*
	 * Get last synced offset for this dataset.  ENOENT (never
	 * synced) starts at 0; a query ERROR must not be mistaken
	 * for offset 0 -- that would re-read the whole ring and
	 * emit a spurious regression row -- so skip this cycle.
	 */
	err = zmetad_db_get_last_offset(db, dataset, &last_offset);
	if (err != 0 && err != ENOENT) {
		daemon_warn("cannot read watermark for %s: %s; "
		    "skipping this cycle\n", dataset, strerror(err));
		return (err);
	}

fetch:
	events = NULL;
	err = lzc_get_events(dataset, 0, last_offset, &events);
	if (err != 0) {
		if (err == ENOENT) {
			/* Dataset doesn't exist or events not enabled */
			return (0);
		}
		daemon_warn("Failed to get events for %s: %s\n",
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
	 *
	 * The check runs BEFORE any record is consumed: this reply
	 * was fetched at the OLD ring's watermark, so if the ring was
	 * replaced, new-ring records in [0, old_watermark) are not in
	 * it and would be skipped forever.  On swap detection the
	 * reply is discarded, the watermark reset to 0, and the fetch
	 * repeated ONCE in the same cycle (single retry guard, no
	 * loop) so the new ring is read from its own beginning.
	 */
	ring_guid = 0;
	have_guid = (nvlist_lookup_uint64(events, "ring_guid",
	    &ring_guid) == 0 && ring_guid != 0);

	/*
	 * Dataset root object id (kernel-provided, GET_EVENTS reply).
	 * Zero on legacy kernels; the resolver then falls back to the
	 * empty-graph heuristic. Learned fresh every poll so it is
	 * current before any of this page's records resolve.
	 */
	{
		uint64_t root_id = 0;

		if (nvlist_lookup_uint64(events, "root_objid",
		    &root_id) == 0 && root_id != 0)
			zmetad_db_set_root_id(db, dataset, root_id);
	}

	if (have_guid) {
		uint64_t stored = ls->stored_guid;

		if (stored == 0) {
			/*
			 * First sighting this process: fall back to
			 * the stored value so a daemon restart does
			 * not mistake a long-lived ring for a new one.
			 *
			 * zmetad_db_get_ring_guid returns 0 for BOTH
			 * "no identity yet" and a failed read (it
			 * reports the error itself); cache the reply's
			 * guid only when the lookup returned a real
			 * identity.  Adopting this ring on an unknown
			 * answer would permanently mask a swap that
			 * happened while the daemon was down.  Left at
			 * 0, the next poll re-reads the persisted value
			 * (written later this cycle), so such a swap is
			 * caught one poll late instead of never.
			 */
			stored = zmetad_db_get_ring_guid(db, dataset);
			if (stored != 0)
				ls->stored_guid = stored;
		}
		if (stored != 0 && stored != ring_guid) {
			daemon_warn("ring replaced on %s: guid %llu "
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
			if (zmetad_db_insert_gap(db, dataset, 0,
			    last_offset, (uint64_t)-1) != 0) {
				daemon_warn("failed to record ring-"
				    "replacement gap for %s\n", dataset);
			}
			/*
			 * Discard the reply (fetched at the old
			 * watermark), restart the watermark at 0 and
			 * re-arm the loss baseline on the new ring,
			 * then re-fetch once.  The watermark write at
			 * the end of this cycle persists offset +
			 * new guid together.
			 */
			nvlist_free(events);
			last_offset = 0;
			ls->last_lost = 0;
			ls->have_lost = B_FALSE;
			ls->high_water = 0;
			ls->have_high_water = B_FALSE;
			ls->stored_guid = ring_guid;
			/*
			 * Clear the PERSISTED loss baseline too: it
			 * belongs to the old ring.  Left in place, a
			 * new-ring counter >= the old baseline would
			 * produce a meaningless cross-ring delta (or
			 * mask real new-ring loss) on the next poll.
			 */
			if (zmetad_db_set_last_lost(db, dataset,
			    B_FALSE, 0) != 0) {
				daemon_warn("failed to clear loss "
				    "baseline for %s after ring "
				    "replacement\n", dataset);
			}

			if (!refetched) {
				refetched = B_TRUE;
				goto fetch;
			}
			/*
			 * A second replacement within one poll cycle:
			 * do not chase it, but DO persist the reset
			 * (offset 0 + the newest guid) before
			 * returning.  The old watermark belongs to a
			 * ring that is already gone: left persisted,
			 * the next poll would fetch at that stale
			 * cursor against the newest ring and silently
			 * skip its records [0, old_watermark).
			 */
			if (zmetad_db_set_last_offset(db, dataset, 0,
			    ring_guid) != 0) {
				daemon_warn("failed to persist reset "
				    "watermark for %s after second "
				    "ring replacement\n", dataset);
			}
			daemon_warn("ring on %s replaced again within one "
			    "poll cycle; deferring to the next cycle\n",
			    dataset);
			return (0);
		}
		/*
		 * Keep the in-memory baseline in step with a confirmed
		 * identity.  When the lookup yielded 0 (unknown, or a
		 * read error) leave it 0 so the next poll re-reads the
		 * persisted value instead of adopting this ring (see
		 * above).
		 */
		if (stored != 0)
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
			daemon_warn("event log cleared on %s: cursor "
			    "%llu past eof %llu; resuming at 0\n", dataset,
			    (unsigned long long)last_offset,
			    (unsigned long long)log_eof);
			if (zmetad_db_insert_gap(db, dataset, log_eof,
			    last_offset, 0) != 0) {
				daemon_warn("failed to record clear "
				    "boundary gap for %s\n", dataset);
			}
			last_offset = 0;
			ls->last_lost = 0;
			ls->have_lost = B_FALSE;
			ls->high_water = 0;
			ls->have_high_water = B_FALSE;
			/*
			 * Clear the PERSISTED baseline too, exactly as
			 * the ring-replacement path does.  detect_loss()
			 * re-reads it on this very poll; left behind, the
			 * pre-clear counter would be restored and the new
			 * low counter would look like an external
			 * regression on every later poll.
			 */
			if (zmetad_db_set_last_lost(db, dataset, B_FALSE,
			    0) != 0) {
				daemon_warn("failed to clear loss baseline "
				    "for %s after log clear\n", dataset);
			}
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
		daemon_warn("schema version mismatch: daemon=%llu "
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
	if (nvlist_lookup_nvlist(events, "events", &records) != 0)
		records = NULL;

	/*
	 * The whole batch goes in ONE transaction: per-event
	 * autocommit costs an fsync each and lets the daemon fall
	 * behind the ring (causing the very gaps it reports).  If any
	 * insert fails the batch is rolled back and the watermark is
	 * NOT advanced, so the next poll re-reads this page from the
	 * old watermark; INSERT OR IGNORE makes the replay idempotent.
	 */
	if (zmetad_db_begin(db) != 0) {
		insert_failed = B_TRUE;
	} else {
		while (records != NULL &&
		    (elem = nvlist_next_nvpair(records, elem)) != NULL) {
			if (nvpair_type(elem) != DATA_TYPE_NVLIST)
				continue;

			if (nvpair_value_nvlist(elem, &event) != 0)
				continue;

			err = zmetad_db_insert_event(db, dataset, event);
			if (err != 0) {
				daemon_warn("Failed to insert event for "
				    "%s: %s\n", dataset, strerror(err));
				insert_failed = B_TRUE;
			} else {
				count++;
			}
		}

		if (insert_failed) {
			(void) zmetad_db_rollback(db);
		} else if (zmetad_db_commit(db) != 0) {
			insert_failed = B_TRUE;
			(void) zmetad_db_rollback(db);
		}
	}

	if (insert_failed) {
		count = 0;
		daemon_warn("event batch for %s rolled back: watermark not "
		    "advanced; the next poll re-reads from offset %llu "
		    "(replay is idempotent)\n", dataset,
		    (u_longlong_t)last_offset);
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
	(void) nvlist_lookup_uint64(events, "records_lost", &records_lost);
	(void) nvlist_lookup_uint64(events, "next_offset", &next_offset);
	/*
	 * New top-level reply key (absent on older kernels): records in
	 * THIS reply consumed by the cursor but not decodable by the
	 * kernel.  It is non-cumulative, per reply, by contract, so it
	 * must stay OUT of the cumulative records_lost delta that drives
	 * detect_loss() -- folding it in would re-count every undecodable
	 * record on each later poll and could trip the external-clear
	 * re-arm.  It is recorded as its own gap row below.
	 */
	(void) nvlist_lookup_uint64(events, "records_undecodable",
	    &records_undecodable);

	{
		uint64_t lost_now = 0;
		int lrc = detect_loss(dataset, db, ls, last_offset,
		    next_offset, records_lost, &lost_now);

		/*
		 * Only a successful detection may (re)baseline.  When the
		 * baseline read failed, detect_loss returned nonzero and
		 * left the persisted counter untouched, so the loss since
		 * it is still measured on a later poll.  Persisting
		 * records_lost unconditionally here (the old bug) would
		 * re-baseline after exactly the read error the EIO guard
		 * exists to survive.
		 */
		if (lrc == 0) {
			if (lost_now > 0 && g_config.verbose) {
				printf("Loss detected on %s; gap "
				    "recorded\n", dataset);
			}
			/*
			 * Persist the cumulative counter so the next
			 * process lifetime (DB layout 6) deltas against
			 * it instead of re-arming: a wrap during downtime
			 * is then reported, not swallowed.
			 */
			if (zmetad_db_set_last_lost(db, dataset, B_TRUE,
			    records_lost) != 0) {
				daemon_warn("failed to persist loss "
				    "baseline for %s\n", dataset);
			}
		}
	}

	/*
	 * Records the cursor consumed but could not decode.  Warn here;
	 * the gap row is written only once the page's watermark is
	 * durably advanced (below), because a rolled-back batch or a
	 * failed watermark write makes the next poll re-read this same
	 * reply, which would duplicate the row on every retry.
	 */
	if (records_undecodable > 0) {
		daemon_warn("event log on %s: %llu record(s) in this "
		    "page could not be decoded\n", dataset,
		    (unsigned long long)records_undecodable);
	}

	/*
	 * Advance the watermark from the returned next_offset,
	 * persisting the ring identity in the same write.  When the
	 * ring was replaced above, last_offset was reset to 0: the
	 * new ring's records start from its own offset space.  The
	 * upsert preserves a previously stored guid when the reply
	 * carries none (0 binds NULL upstream): a legacy reply never
	 * erases the stored identity, so a later kernel upgrade does
	 * not lose swap detection.  Skipped entirely when any insert
	 * in this batch failed: the batch was rolled back and the
	 * page must be re-read next poll.
	 */
	if (!insert_failed) {
		if (next_offset > 0) {
			err = zmetad_db_set_last_offset(db, dataset,
			    next_offset, have_guid ? ring_guid : 0);
		} else if (have_guid) {
			/*
			 * Even with nothing new to sync, remember the
			 * identity once seen so a later swap is
			 * detectable.
			 */
			err = zmetad_db_set_last_offset(db, dataset,
			    last_offset, ring_guid);
		} else {
			err = 0;
		}
		if (err != 0) {
			daemon_warn("failed to persist watermark for %s: "
			    "%s; the next poll re-reads this page\n",
			    dataset, strerror(err));
		} else if (next_offset > 0 && records_undecodable > 0) {
			/*
			 * The page's watermark is durable, so this
			 * reply will not be re-read: record the
			 * undecodable records as their own gap row.
			 * Not applied to records_lost -- see above.
			 */
			if (zmetad_db_insert_gap(db, dataset, last_offset,
			    next_offset, records_undecodable) != 0) {
				daemon_warn("failed to record undecodable-"
				    "record gap for %s\n", dataset);
			}
		}
	}

	nvlist_free(events);
	return (0);
}

/*
 * Sweep context for one collect_all_events() cycle.  complete is
 * cleared whenever any dataset could not be fully processed: a failed
 * pool/filesystem iteration, a mountpoint row that could not be
 * refreshed, or a per-dataset collect error.  A partial sweep leaves
 * datasets unvisited (or their rows stale), so the stale-dataset
 * prune that would delete rows for them must not run.
 */
struct collect_ctx {
	zmetad_db_t *db;
	boolean_t complete;
};

static int
collect_callback(zfs_handle_t *zhp, void *arg)
{
	struct collect_ctx *ctx = arg;
	zmetad_db_t *db = ctx->db;
	const char *name = zfs_get_name(zhp);
	uint64_t events_enabled;

	/* Check if events are enabled on this dataset */
	events_enabled = zfs_prop_get_int(zhp, ZFS_PROP_EVENTS);
	if (events_enabled) {
		char mountpoint[PATH_MAX];

		/*
		 * Record the mountpoint for path -> dataset
		 * resolution.  Non-filesystem datasets yield
		 * "-" or "legacy"; store as-is (consumers filter
		 * by '/' prefix).  Skip silently on error.
		 * A value filling the buffer is treated as
		 * truncated: storing it would poison
		 * longest-prefix attribution, so warn and skip.
		 */
		if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
		    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) == 0) {
			if (strlen(mountpoint) >= sizeof (mountpoint) - 1) {
				daemon_warn("mountpoint for %s is %lu bytes "
				    "or longer; not recorded (truncation "
				    "would break path attribution)\n", name,
				    (unsigned long)(sizeof (mountpoint) - 1));
			} else if (zmetad_db_upsert_mountpoint(db, name,
			    mountpoint) != 0) {
				/*
				 * The datasets row was not refreshed;
				 * a prune this cycle would delete this
				 * live dataset's mapping.  Flag the
				 * sweep partial.
				 */
				ctx->complete = B_FALSE;
				daemon_warn("failed to record "
				    "mountpoint for %s\n", name);
			}
		} else if (g_config.verbose) {
			daemon_warn("no mountpoint for %s\n", name);
		}

		if (collect_dataset_events(name, db) != 0)
			ctx->complete = B_FALSE;

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
	if (zfs_iter_filesystems_v2(zhp, 0, collect_callback, arg) != 0)
		ctx->complete = B_FALSE;

	zfs_close(zhp);
	return (0);
}

/*
 * One full poll cycle: walk every pool/dataset (each events-enabled
 * dataset refreshes its datasets row last_seen via the mountpoint
 * upsert), then prune rows no cycle member refreshed -- datasets
 * destroyed or with events disabled since the last poll.  Stale rows
 * left behind could otherwise win a longest-prefix match.  One
 * parameterized DELETE per cycle.
 *
 * The prune runs ONLY after a fully successful sweep.  A failed or
 * partial sweep clears ctx.complete and leaves datasets unvisited (or
 * their rows unrefreshed); pruning then would delete rows for
 * datasets that were never given the chance to refresh, wiping their
 * mountpoint mappings.  The sweep is synchronous, so the callback's
 * flag is sufficient to know.
 */
static void
collect_all_events(zmetad_db_t *db)
{
	int64_t cycle_start = (int64_t)time(NULL);
	struct collect_ctx ctx;

	ctx.db = db;
	ctx.complete = B_TRUE;

	/* Iterate all pools and datasets */
	if (zfs_iter_root(g_zfs, collect_callback, &ctx) != 0)
		ctx.complete = B_FALSE;

	if (!ctx.complete) {
		daemon_warn("dataset sweep incomplete; skipping stale "
		    "datasets prune this cycle\n");
		return;
	}

	if (zmetad_db_prune_stale_datasets(db, cycle_start) != 0) {
		daemon_warn("failed to prune stale datasets rows\n");
	}
}

static void
daemon_loop(zmetad_db_t *db)
{
	uint64_t last_collect = 0;
	uint64_t last_cleanup = 0;
	boolean_t collected_once = B_FALSE;
	boolean_t cleaned_once = B_FALSE;

	while (!g_shutdown) {
		uint64_t now = mono_secs();

		/* SIGHUP: reserved; configuration reload is not implemented */
		if (g_reload) {
			g_reload = 0;
			if (g_config.verbose) {
				printf("SIGHUP received: configuration "
				    "reload not implemented (reserved)\n");
			}
		}

		/*
		 * Forced collect: SIGUSR1 requests an out-of-band
		 * cycle, independent of the poll interval.  The
		 * test-and-clear runs with SIGUSR1 blocked: a signal
		 * arriving between the test and the clear would set
		 * the flag we just read and then be wiped, silently
		 * dropping the request.  Blocked signals stay pending
		 * and are delivered when the mask is restored.
		 */
		{
			sigset_t set, oset;
			boolean_t forced;

			sigemptyset(&set);
			sigaddset(&set, SIGUSR1);
			(void) pthread_sigmask(SIG_BLOCK, &set, &oset);
			forced = g_force_collect ? B_TRUE : B_FALSE;
			g_force_collect = 0;
			(void) pthread_sigmask(SIG_SETMASK, &oset, NULL);

			if (forced) {
				if (g_config.verbose) {
					printf("forced collect (SIGUSR1)\n");
				}
				collect_all_events(db);
				/*
				 * Anchor the next-due computation to the
				 * `now` sampled BEFORE the collect, not a
				 * fresh mono_secs(): a forced collect that
				 * crosses a one-second boundary would
				 * otherwise leave last_collect > now, and
				 * the unsigned `now - last_collect` below
				 * would wrap to a huge value and run a
				 * second full poll immediately.
				 */
				last_collect = now;
				collected_once = B_TRUE;
			}
		}

		/*
		 * Collect events at poll interval.  The first tick
		 * always collects: with a monotonic clock,
		 * last_collect = 0 means "never run", not "ran at
		 * uptime 0", so a freshly booted host (uptime below
		 * poll_interval) must not wait a whole interval before
		 * its first collection.  last_collect is anchored to
		 * `now` so the comparison can never underflow.
		 */
		if (!collected_once || now - last_collect >=
		    (uint64_t)g_config.poll_interval) {
			collect_all_events(db);
			last_collect = now;
			collected_once = B_TRUE;
		}

		/*
		 * Cleanup old events daily. The first tick is
		 * included: with a monotonic clock, last_cleanup = 0
		 * means "never run", not "ran at uptime 0", so a
		 * freshly booted host must still enforce retention
		 * immediately.
		 */
		if (!cleaned_once || now - last_cleanup >= 86400) {
			if (g_config.retention_days > 0) {
				int cerr = zmetad_db_cleanup(db,
				    g_config.retention_days);

				if (cerr != 0) {
					daemon_warn("retention cleanup "
					    "failed: %s\n", strerror(cerr));
				}
			}
			last_cleanup = mono_secs();
			cleaned_once = B_TRUE;
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

	/*
	 * Redirect stdio to /dev/null, verifying each open lands on
	 * the descriptor it is for.  An unchecked failure would leave
	 * that fd closed and the next open() anywhere in the process
	 * would alias it -- e.g. SQLite writes landing on what the
	 * program believes is stdout.  If any redirect cannot be
	 * established the daemon cannot run safely: exit.
	 */
	close(STDIN_FILENO);
	close(STDOUT_FILENO);
	close(STDERR_FILENO);

	for (int fd = STDIN_FILENO; fd <= STDERR_FILENO; fd++) {
		int nfd;

		nfd = open("/dev/null",
		    (fd == STDIN_FILENO) ? O_RDONLY : O_WRONLY);
		if (nfd != fd) {
			if (nfd >= 0)
				close(nfd);
			syslog(LOG_ERR, "daemonize: cannot redirect fd %d "
			    "to /dev/null: %s", fd, strerror(errno));
			exit(EXIT_FAILURE);
		}
	}
}

static void
usage(const char *progname)
{
	fprintf(stderr, "Usage: %s [options]\n", progname);
	fprintf(stderr, "\n");
	fprintf(stderr, "Options:\n");
	fprintf(stderr, "  -d, --database <path>  SQLite database path\n");
	fprintf(stderr, "  -f, --foreground       Run in foreground\n");
	fprintf(stderr, "  -i, --interval <sec>   Poll interval "
	    "(default: %d)\n", ZMETAD_DEFAULT_POLL_INTERVAL);
	fprintf(stderr, "  -r, --retention <days> Retention days, 1..%d "
	    "(default: %d)\n", ZMETAD_MAX_RETENTION_DAYS,
	    ZMETAD_DEFAULT_RETENTION_DAYS);
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

	while ((opt = getopt_long(argc, argv, "d:fe:i:k:p:r:s:vh", longopts,
	    NULL)) != -1) {
		switch (opt) {
		case 'd':
			/*
			 * A truncated path would open/create the wrong
			 * database file silently, so reject it here
			 * rather than at open time.
			 */
			if (strlcpy(g_config.db_path, optarg,
			    sizeof (g_config.db_path)) >=
			    sizeof (g_config.db_path)) {
				fprintf(stderr, "database path too long "
				    "(max %lu): %s\n",
				    (unsigned long)sizeof (g_config.db_path),
				    optarg);
				return (EXIT_FAILURE);
			}
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
		case 'i': {
			long sec;
			char *endptr = NULL;

			errno = 0;
			sec = strtol(optarg, &endptr, 10);
			if (endptr == optarg || *endptr != '\0' ||
			    errno == ERANGE || sec > INT_MAX) {
				fprintf(stderr, "invalid poll interval "
				    "'%s': expected a number of seconds\n",
				    optarg);
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			g_config.poll_interval = (sec < 1) ? 1 : (int)sec;
			break;
		}
		case 'r': {
			long days;
			char *endptr = NULL;

			errno = 0;
			days = strtol(optarg, &endptr, 10);
			if (endptr == optarg || *endptr != '\0' ||
			    errno == ERANGE || days <= 0 ||
			    days > ZMETAD_MAX_RETENTION_DAYS) {
				fprintf(stderr, "invalid retention '%s': "
				    "expected 1..%d days\n", optarg,
				    ZMETAD_MAX_RETENTION_DAYS);
				usage(argv[0]);
				return (EXIT_FAILURE);
			}
			g_config.retention_days = (int)days;
			break;
		}
		case 's':
			/*
			 * Same truncation hazard as -d: a clipped schema
			 * path would load the wrong file or fail
			 * confusingly; reject it before load.
			 */
			if (strlcpy(g_config.schema_path, optarg,
			    sizeof (g_config.schema_path)) >=
			    sizeof (g_config.schema_path)) {
				fprintf(stderr, "schema path too long "
				    "(max %lu): %s\n",
				    (unsigned long)sizeof (g_config.schema_path),
				    optarg);
				return (EXIT_FAILURE);
			}
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
	 * the leftover positional -- which must not itself look like
	 * an option.
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
		if (argv[optind][0] == '-') {
			fprintf(stderr, "--%s path must not start with "
			    "'-': %s\n",
			    (pathp == &g_config.export_schema_path) ?
			    "export-schema" : "check-schema", argv[optind]);
			return (EXIT_FAILURE);
		}
		*pathp = argv[optind];
		optind++;
	}

	/* Any remaining option-shaped schema path is a parse accident. */
	if ((g_config.export_schema_path != NULL &&
	    g_config.export_schema_path[0] == '-') ||
	    (g_config.check_schema_path != NULL &&
	    g_config.check_schema_path[0] == '-')) {
		fprintf(stderr, "schema path must not start with '-'\n");
		usage(argv[0]);
		return (EXIT_FAILURE);
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
		zmetad_schema_free(g_schema);
		libzfs_fini(g_zfs);
		return (EXIT_FAILURE);
	}

	/*
	 * DB-layer runtime warnings mirror daemon_warn (stderr plus
	 * syslog when daemonized); without this they would vanish
	 * into /dev/null once daemonized.  open-path errors keep
	 * plain fprintf -- they run with a live stderr or exit.
	 */
	zmetad_db_set_warn(db, db_warn_sink);

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

	/*
	 * Daemonize unless foreground mode.  openlog first so
	 * daemonize() failures and later daemon_warn() output have a
	 * syslog sink once stderr is /dev/null.
	 */
	if (!g_config.foreground) {
		openlog("zmetad", LOG_PID, LOG_DAEMON);
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
	zmetad_db_close(db);
	zmetad_schema_free(g_schema);
	libzfs_fini(g_zfs);
	if (!g_config.foreground)
		closelog();

	return (EXIT_SUCCESS);
}
