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
#include <libnvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

/* Default configuration values */
#define	ZMETAD_DEFAULT_DB_PATH		"/var/lib/zfs/zmetad.db"
#define	ZMETAD_DEFAULT_POLL_INTERVAL	30	/* seconds */
#define	ZMETAD_DEFAULT_RETENTION_DAYS	90
#define	ZMETAD_DEFAULT_MAX_SIZE_MB	1000

/* Configuration structure */
typedef struct zmetad_config {
	char		db_path[PATH_MAX];
	char		schema_path[PATH_MAX];
	int		poll_interval;
	int		retention_days;
	int		max_size_mb;
	boolean_t	foreground;
	int		verbose;
	char		*export_schema_path;
	char		*check_schema_path;
	boolean_t	force;
} zmetad_config_t;

/* Opaque database handle */
typedef struct zmetad_db zmetad_db_t;

/*
 * Database operations (zmetad_db.c)
 */

/* Open or create the SQLite database for the given event schema */
typedef struct zmetad_schema zmetad_schema_t;
int zmetad_db_open(zmetad_db_t **dbp, const char *path,
    const zmetad_schema_t *zs);

/* Close the database */
void zmetad_db_close(zmetad_db_t *db);

/* Insert an event record */
int zmetad_db_insert_event(zmetad_db_t *db, const char *dataset,
    nvlist_t *event);

/* Get the last synced offset for a dataset */
uint64_t zmetad_db_get_last_offset(zmetad_db_t *db, const char *dataset);

/* Set the last synced offset for a dataset */
int zmetad_db_set_last_offset(zmetad_db_t *db, const char *dataset,
    uint64_t offset);

/* Cleanup events older than retention_days */
int zmetad_db_cleanup(zmetad_db_t *db, int retention_days);

/* Get database statistics */
int zmetad_db_stats(zmetad_db_t *db, uint64_t *event_count,
    uint64_t *db_size);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMETAD_H */
