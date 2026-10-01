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

#ifndef	_ZMETAD_SCHEMA_H
#define	_ZMETAD_SCHEMA_H

#include <sys/types.h>
#include <libnvpair.h>

#ifdef	__cplusplus
extern "C" {
#endif

/*
 * Event schema description, parsed from events-schema.json (or an
 * identical embedded copy).  Only schema_version, fields[].name/type
 * and the op enum are interpreted; every other key is ignored for
 * forward compatibility.
 */
typedef struct zmetad_schema zmetad_schema_t;

/*
 * Load the event schema from "path".  If "path" is NULL the embedded
 * default (ZMETAD_EMBEDDED_SCHEMA_JSON) is used.  "errbuf" is a
 * caller-provided char[256]; on failure NULL is returned and errbuf
 * holds a human-readable reason.
 */
zmetad_schema_t *zmetad_schema_load(const char *path, char *errbuf);

/*
 * Compare a wire schema_version against the loaded schema.  Returns 0
 * when the versions are compatible: wire == 0 (pre-exposure kernels
 * that do not report a version) or wire == schema version.  Returns
 * EINVAL when "wire" is non-zero and differs.
 */
int zmetad_schema_check_version(const zmetad_schema_t *zs, uint64_t wire);

/*
 * Compare two loaded schemas for structural equality: identical field
 * count, and identical field name and type at every index (order is
 * significant).  Returns 0 when equal, non-zero otherwise; on
 * mismatch the first difference is described in "errbuf" (up to
 * "errlen" bytes).  Returns EINVAL if either schema is NULL.
 */
int zmetad_schema_compare(const zmetad_schema_t *a,
    const zmetad_schema_t *b, char *errbuf, size_t errlen);

/*
 * Look up field "name" (which must be a known schema field) in the
 * event record "rec".  On success stores the value in *out: for
 * numeric fields a zero-extended uint64_t, for string fields a
 * NUL-terminated const char pointer.  *dtype reports the record-side
 * type (DATA_TYPE_UINT16, DATA_TYPE_UINT64 or DATA_TYPE_STRING) and
 * *nelem the string length (0 for numeric fields).  Returns ENOENT
 * when the field is absent from the record (normal) and EINVAL on a
 * type mismatch against the schema (or bad arguments).
 */
int zmetad_schema_field(const zmetad_schema_t *zs, const char *name,
    nvlist_t *rec, void *out, uint_t *nelem, data_type_t *dtype);

/*
 * Number of fields known to the schema, and the name of field "idx"
 * (< zmetad_schema_nfields()).
 */
uint_t zmetad_schema_nfields(const zmetad_schema_t *zs);
const char *zmetad_schema_field_name(const zmetad_schema_t *zs, uint_t idx);

/*
 * Name of operation "op" per the schema enum, by index.  Values
 * outside the enum decode as "UNKNOWN".
 */
const char *zmetad_schema_op_name(const zmetad_schema_t *zs, uint64_t op);

/* Schema version declared by the parsed document. */
uint64_t zmetad_schema_version(const zmetad_schema_t *zs);

void zmetad_schema_free(zmetad_schema_t *zs);

#ifdef	__cplusplus
}
#endif

#endif	/* _ZMETAD_SCHEMA_H */
