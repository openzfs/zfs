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
#include "zmetad_schema.h"

struct zmetad_db {
	sqlite3		*sqlite;
	sqlite3_stmt	*insert_event_stmt;
	sqlite3_stmt	*get_last_offset_stmt;
	sqlite3_stmt	*set_last_offset_stmt;
	const zmetad_schema_t *schema;
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
	");"
	"CREATE TABLE IF NOT EXISTS meta ("
	"    key TEXT PRIMARY KEY,"
	"    value TEXT NOT NULL"
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

static const char *get_meta_sql =
	"SELECT value FROM meta WHERE key = ?";

static const char *set_meta_sql =
	"INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)";

/*
 * Read a value from the meta table.  Returns 0 and sets *out (caller
 * frees) when the key exists; ENOENT when absent.
 */
static int
db_get_meta(zmetad_db_t *db, const char *key, char **out)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	*out = NULL;

	rc = sqlite3_prepare_v2(db->sqlite, get_meta_sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return (EIO);

	sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	if (rc == SQLITE_ROW) {
		const unsigned char *val = sqlite3_column_text(stmt, 0);

		*out = strdup(val != NULL ? (const char *)val : "");
		sqlite3_finalize(stmt);
		if (*out == NULL)
			return (ENOMEM);
		return (0);
	}
	sqlite3_finalize(stmt);
	return (rc == SQLITE_DONE ? ENOENT : EIO);
}

static int
db_set_meta(zmetad_db_t *db, const char *key, const char *value)
{
	sqlite3_stmt *stmt = NULL;
	int rc;

	rc = sqlite3_prepare_v2(db->sqlite, set_meta_sql, -1, &stmt, NULL);
	if (rc != SQLITE_OK)
		return (EIO);

	sqlite3_bind_text(stmt, 1, key, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 2, value, -1, SQLITE_STATIC);
	rc = sqlite3_step(stmt);
	sqlite3_finalize(stmt);
	return (rc == SQLITE_DONE ? 0 : EIO);
}

int
zmetad_db_open(zmetad_db_t **dbp, const char *path,
    const zmetad_schema_t *zs)
{
	zmetad_db_t *db;
	char *stored_version = NULL;
	char version_str[32];
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

	/*
	 * Record the event schema version this database was written with.
	 * Refuse to open when a previous run used a different version:
	 * silently mixing record layouts would corrupt the event table.
	 */
	db->schema = zs;
	if (zs != NULL) {
		(void) snprintf(version_str, sizeof (version_str), "%llu",
		    (unsigned long long)zmetad_schema_version(zs));

		rc = db_get_meta(db, "events_schema_version",
		    &stored_version);
		if (rc == 0 && strcmp(stored_version, version_str) != 0) {
			fprintf(stderr, "database schema version mismatch: "
			    "stored=%s loaded=%s\n",
			    stored_version, version_str);
			free(stored_version);
			sqlite3_close(db->sqlite);
			free(db);
			return (EINVAL);
		}
		free(stored_version);

		rc = db_set_meta(db, "events_schema_version", version_str);
		if (rc != 0) {
			fprintf(stderr, "Failed to record schema version\n");
			sqlite3_close(db->sqlite);
			free(db);
			return (rc);
		}
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
	const zmetad_schema_t *zs = db->schema;
	union {
		uint64_t	u64;
		const char	*str;
	} val;
	uint64_t op = 0;
	boolean_t have_op = B_FALSE;
	uint_t nelem;
	data_type_t dtype;
	int rc;

	if (zs == NULL)
		return (EINVAL);

	/*
	 * Decode the record schema-driven: every known field is looked
	 * up via zmetad_schema_field() (ENOENT = absent, normal) and
	 * bound to the matching SQL column by field name.  Absent
	 * fields stay NULL.  The mode column is kept for schema
	 * stability but has no record field.
	 */
	sqlite3_reset(stmt);
	sqlite3_clear_bindings(stmt);

	for (uint_t i = 0; i < zmetad_schema_nfields(zs); i++) {
		const char *name = zmetad_schema_field_name(zs, i);

		val.u64 = 0;
		nelem = 0;
		dtype = DATA_TYPE_UNKNOWN;

		rc = zmetad_schema_field(zs, name, event, &val.u64, &nelem,
		    &dtype);
		if (rc == ENOENT)
			continue;
		if (rc != 0) {
			fprintf(stderr, "Field %s: %s\n", name,
			    strerror(rc));
			return (rc);
		}

		if (strcmp(name, "txg") == 0) {
			sqlite3_bind_int64(stmt, 2, (sqlite3_int64)val.u64);
		} else if (strcmp(name, "time") == 0) {
			sqlite3_bind_int64(stmt, 3, (sqlite3_int64)val.u64);
		} else if (strcmp(name, "object") == 0) {
			sqlite3_bind_int64(stmt, 4, (sqlite3_int64)val.u64);
		} else if (strcmp(name, "op") == 0) {
			op = val.u64;
			have_op = B_TRUE;
		} else if (strcmp(name, "name") == 0) {
			sqlite3_bind_text(stmt, 6, val.str, nelem,
			    SQLITE_TRANSIENT);
		} else if (strcmp(name, "old_name") == 0) {
			sqlite3_bind_text(stmt, 7, val.str, nelem,
			    SQLITE_TRANSIENT);
		} else if (strcmp(name, "uid") == 0) {
			sqlite3_bind_int64(stmt, 8, (sqlite3_int64)val.u64);
		} else if (strcmp(name, "gid") == 0) {
			sqlite3_bind_int64(stmt, 9, (sqlite3_int64)val.u64);
		} else if (strcmp(name, "new_size") == 0) {
			sqlite3_bind_int64(stmt, 11, (sqlite3_int64)val.u64);
		}
		/*
		 * parent, old_parent, target, mode, old_size, attrs:
		 * decoded and validated but no dedicated column.
		 */
	}

	/*
	 * The event_type column stores the schema enum name; an op
	 * outside the enum decodes as UNKNOWN.
	 */
	sqlite3_bind_text(stmt, 1, dataset, -1, SQLITE_STATIC);
	sqlite3_bind_text(stmt, 5, zmetad_schema_op_name(zs,
	    have_op ? op : 0), -1, SQLITE_STATIC);

	/*
	 * Column 10 (mode) stays NULL: no record field maps to it;
	 * the column is kept for schema stability.
	 */

	rc = sqlite3_step(stmt);
	if (rc != SQLITE_DONE && rc != SQLITE_CONSTRAINT) {
		fprintf(stderr, "Insert error: %s\n",
		    sqlite3_errmsg(db->sqlite));
		sqlite3_reset(stmt);
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
