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
 * zmetad database operations - SQLite backend
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sqlite3.h>
#include <libnvpair.h>

#include "zmetad.h"

struct zmetad_db {
	sqlite3		*sqlite;
	sqlite3_stmt	*insert_event_stmt;
	sqlite3_stmt	*get_last_offset_stmt;
	sqlite3_stmt	*set_last_offset_stmt;
};

static const char *schema_sql =
	"CREATE TABLE IF NOT EXISTS events ("
	"    id INTEGER PRIMARY KEY AUTOINCREMENT,"
	"    dataset TEXT NOT NULL,"
	"    txg INTEGER NOT NULL,"
	"    timestamp INTEGER NOT NULL,"
	"    object_id INTEGER NOT NULL,"
	"    event_type TEXT NOT NULL,"
	"    path TEXT,"
	"    old_path TEXT,"
	"    uid INTEGER,"
	"    gid INTEGER,"
	"    mode INTEGER,"
	"    size INTEGER,"
	"    UNIQUE(dataset, txg, object_id, event_type, timestamp)"
	");"
	"CREATE INDEX IF NOT EXISTS idx_events_dataset_time "
	"    ON events(dataset, timestamp);"
	"CREATE INDEX IF NOT EXISTS idx_events_object "
	"    ON events(dataset, object_id);"
	"CREATE INDEX IF NOT EXISTS idx_events_path "
	"    ON events(dataset, path);"
	"CREATE TABLE IF NOT EXISTS sync_state ("
	"    dataset TEXT PRIMARY KEY,"
	"    last_offset INTEGER NOT NULL,"
	"    last_sync INTEGER NOT NULL"
	");";

static const char *insert_event_sql =
	"INSERT OR IGNORE INTO events "
	"(dataset, txg, timestamp, object_id, event_type, path, old_path, "
	"uid, gid, mode, size) "
	"VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)";

static const char *get_last_offset_sql =
	"SELECT last_offset FROM sync_state WHERE dataset = ?";

static const char *set_last_offset_sql =
	"INSERT OR REPLACE INTO sync_state (dataset, last_offset, last_sync) "
	"VALUES (?, ?, ?)";

int
zmetad_db_open(zmetad_db_t **dbp, const char *path)
{
	zmetad_db_t *db;
	int rc;

	db = calloc(1, sizeof (*db));
	if (db == NULL) {
		return (ENOMEM);
	}

	rc = sqlite3_open(path, &db->sqlite);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "SQLite open error: %s\n",
		    sqlite3_errmsg(db->sqlite));
		free(db);
		return (EIO);
	}

	/* Enable WAL mode for better concurrency */
	sqlite3_exec(db->sqlite, "PRAGMA journal_mode=WAL", NULL, NULL, NULL);

	/* Create schema */
	char *errmsg = NULL;
	rc = sqlite3_exec(db->sqlite, schema_sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Schema creation error: %s\n", errmsg);
		sqlite3_free(errmsg);
		sqlite3_close(db->sqlite);
		free(db);
		return (EIO);
	}

	/* Prepare statements */
	rc = sqlite3_prepare_v2(db->sqlite, insert_event_sql, -1,
	    &db->insert_event_stmt, NULL);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Prepare insert error: %s\n",
		    sqlite3_errmsg(db->sqlite));
		sqlite3_close(db->sqlite);
		free(db);
		return (EIO);
	}

	rc = sqlite3_prepare_v2(db->sqlite, get_last_offset_sql, -1,
	    &db->get_last_offset_stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_finalize(db->insert_event_stmt);
		sqlite3_close(db->sqlite);
		free(db);
		return (EIO);
	}

	rc = sqlite3_prepare_v2(db->sqlite, set_last_offset_sql, -1,
	    &db->set_last_offset_stmt, NULL);
	if (rc != SQLITE_OK) {
		sqlite3_finalize(db->insert_event_stmt);
		sqlite3_finalize(db->get_last_offset_stmt);
		sqlite3_close(db->sqlite);
		free(db);
		return (EIO);
	}

	*dbp = db;
	return (0);
}

void
zmetad_db_close(zmetad_db_t *db)
{
	if (db == NULL)
		return;

	if (db->insert_event_stmt)
		sqlite3_finalize(db->insert_event_stmt);
	if (db->get_last_offset_stmt)
		sqlite3_finalize(db->get_last_offset_stmt);
	if (db->set_last_offset_stmt)
		sqlite3_finalize(db->set_last_offset_stmt);
	if (db->sqlite)
		sqlite3_close(db->sqlite);

	free(db);
}

int
zmetad_db_insert_event(zmetad_db_t *db, const char *dataset, nvlist_t *event)
{
	sqlite3_stmt *stmt = db->insert_event_stmt;
	uint64_t txg = 0, timestamp = 0, object_id = 0;
	uint64_t uid = 0, gid = 0, mode = 0, size = 0;
	const char *event_type = NULL;
	const char *path = NULL;
	const char *old_path = NULL;
	int rc;

	/*
	 * Extract fields from nvlist. These key names must match the
	 * ZFS_EV_* definitions in <sys/zfs_events.h>: "op" is a uint16
	 * operation enum (ZFS_EV_CREATE, ...), names are strings, and
	 * the post-write size is "new_size". There is no mode field in
	 * event records; the column is kept for schema stability.
	 */
	uint64_t op = 0;
	uint16_t op16 = 0;
	(void) nvlist_lookup_uint64(event, "txg", &txg);
	(void) nvlist_lookup_uint64(event, "time", &timestamp);
	(void) nvlist_lookup_uint64(event, "object", &object_id);
	(void) nvlist_lookup_uint16(event, "op", &op16);
	op = op16;
	(void) nvlist_lookup_string(event, "name", &path);
	(void) nvlist_lookup_string(event, "old_name", &old_path);
	(void) nvlist_lookup_uint64(event, "uid", &uid);
	(void) nvlist_lookup_uint64(event, "gid", &gid);
	(void) nvlist_lookup_uint64(event, "new_size", &size);
	static const char *const op_names[] = {
		"NONE", "CREATE", "REMOVE", "RENAME", "LINK", "SYMLINK",
		"TRUNCATE", "SETATTR"
	};
	event_type = (op < sizeof (op_names) / sizeof (op_names[0])) ?
	    op_names[op] : "UNKNOWN";

	/* Bind parameters */
	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, dataset, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, txg);
	sqlite3_bind_int64(stmt, 3, timestamp);
	sqlite3_bind_int64(stmt, 4, object_id);
	sqlite3_bind_text(stmt, 5, event_type ? event_type : "UNKNOWN",
	    -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 6, path, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 7, old_path, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 8, uid);
	sqlite3_bind_int64(stmt, 9, gid);
	sqlite3_bind_int64(stmt, 10, mode);
	sqlite3_bind_int64(stmt, 11, size);

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
		fprintf(stderr, "Insert error: %s\n",
		    sqlite3_errmsg(db->sqlite));
		return (EIO);
	}

	return (0);
}

uint64_t
zmetad_db_get_last_offset(zmetad_db_t *db, const char *dataset)
{
	sqlite3_stmt *stmt = db->get_last_offset_stmt;
	uint64_t offset = 0;

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, dataset, -1, SQLITE_STATIC);

	if (sqlite3_step(stmt) == SQLITE_ROW) {
		offset = sqlite3_column_int64(stmt, 0);
	}

	return (offset);
}

int
zmetad_db_set_last_offset(zmetad_db_t *db, const char *dataset, uint64_t offset)
{
	sqlite3_stmt *stmt = db->set_last_offset_stmt;
	int rc;

	sqlite3_reset(stmt);
	sqlite3_bind_text(stmt, 1, dataset, -1, SQLITE_STATIC);
	sqlite3_bind_int64(stmt, 2, offset);
	sqlite3_bind_int64(stmt, 3, time(NULL));

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE) {
		fprintf(stderr, "Set last_offset error: %s\n",
		    sqlite3_errmsg(db->sqlite));
		return (EIO);
	}

	return (0);
}

int
zmetad_db_cleanup(zmetad_db_t *db, int retention_days)
{
	char sql[256];
	char *errmsg = NULL;
	int rc;
	time_t cutoff;

	/* Calculate cutoff timestamp */
	cutoff = time(NULL) - (retention_days * 86400);

	snprintf(sql, sizeof (sql),
	    "DELETE FROM events WHERE timestamp < %ld", (long)cutoff);

	rc = sqlite3_exec(db->sqlite, sql, NULL, NULL, &errmsg);
	if (rc != SQLITE_OK) {
		fprintf(stderr, "Cleanup error: %s\n", errmsg);
		sqlite3_free(errmsg);
		return (EIO);
	}

	/* Vacuum to reclaim space */
	sqlite3_exec(db->sqlite, "VACUUM", NULL, NULL, NULL);

	return (0);
}

int
zmetad_db_stats(zmetad_db_t *db, uint64_t *event_count, uint64_t *db_size)
{
	sqlite3_stmt *stmt;
	int rc;

	/* Get event count */
	rc = sqlite3_prepare_v2(db->sqlite, "SELECT COUNT(*) FROM events",
	    -1, &stmt, NULL);
	if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
		*event_count = sqlite3_column_int64(stmt, 0);
	}
	sqlite3_finalize(stmt);

	/* Get database page count * page size */
	rc = sqlite3_prepare_v2(db->sqlite,
	    "SELECT page_count * page_size "
	    "FROM pragma_page_count, pragma_page_size",
	    -1, &stmt, NULL);
	if (rc == SQLITE_OK && sqlite3_step(stmt) == SQLITE_ROW) {
		*db_size = sqlite3_column_int64(stmt, 0);
	}
	sqlite3_finalize(stmt);

	return (0);
}
