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

#ifndef	_ZMETAD_SPOOL_H
#define	_ZMETAD_SPOOL_H

#include <sys/types.h>
#include <libnvpair.h>

#include "zmetad.h"

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * NDJSON spool writer (leaf 02).  One JSON object per line, appended to
 * a single file, for log-pipeline tailers (syslog-ng, vector, ...).
 * Best-effort: no caller path may be blocked or failed by a spool
 * error; the database commit is always the integrity record.
 */
typedef struct zmetad_spool zmetad_spool_t;

/*
 * Open (lazily created on first write) the spool at "path" with the
 * given rotation threshold and per-batch fsync setting.  Returns 0 on
 * success; EINVAL on bad arguments, ENAMETOOLONG on a truncated path.
 * The path is only validated here (length), not by touching the file:
 * open happens lazily so a transiently unavailable filesystem does not
 * disable spooling for the process lifetime.
 */
int zmetad_spool_open(zmetad_spool_t **sp, const char *path,
    size_t max_bytes, boolean_t fsync);

/*
 * Bind the schema used to serialize event record bodies ("rec") in
 * schema field order.  May be called before or after events are
 * spooled; serialization without a bound schema renders "rec" as {}.
 */
void zmetad_spool_set_schema(zmetad_spool_t *sp,
    const zmetad_schema_t *zs);

/*
 * Append one event record as a complete envelope line:
 *   {"zmetad":1,"type":"event","dataset":...,"ts":...,"rec":{...}}
 * "rec" is the wire record serialized schema-driven ("rec" carries the
 * same fields the events table binds).  captured_at is the ingest time
 * stored in the DB, not a fresh wall clock.  Returns 0 on success,
 * EIO/ENOSPC-style errors on a write failure (the caller skips the
 * rest of the batch; the DB is untouched).
 */
int zmetad_spool_event(zmetad_spool_t *sp, const char *dataset,
    const nvlist_t *rec, uint64_t captured_at);

/*
 * Append one marker line.  "type" is "epoch" (payload: old_guid,
 * new_guid uint64 pairs) or "gap" (payload: "lost" uint64, or the
 * (uint64_t)-1 sentinel rendered as {"swap":true}).  Markers must be
 * emitted by the caller BEFORE the records that follow the boundary.
 * Payload pairs are passed as (name, uint64) lists terminated by NULL:
 *   zmetad_spool_marker(sp, ds, "epoch", "old_guid", o, "new_guid", n,
 *       NULL);
 */
int zmetad_spool_marker(zmetad_spool_t *sp, const char *dataset,
    const char *type, ...);

/*
 * Close the spool, flushing and freeing the handle.  NULL is a no-op.
 */
void zmetad_spool_close(zmetad_spool_t *sp);

/*
 * Poison state: after a write error the handle refuses further writes
 * (poisoned) until the caller resumes it at the start of the next
 * collect cycle -- one ENOSPC yields one warning per cycle, not one
 * per record.  zmetad_spool_poisoned() returns B_TRUE for NULL (never
 * spool on an unknown handle state).
 */
boolean_t zmetad_spool_poisoned(zmetad_spool_t *sp);
void zmetad_spool_resume(zmetad_spool_t *sp);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMETAD_SPOOL_H */
