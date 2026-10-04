// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * NDJSON spool writer for zmetad (leaf 02).
 *
 * One compact JSON object per line, O_APPEND-written line-atomically:
 * the full line is built in memory, then a single write(2) puts the
 * line plus newline on the wire, so a tailer never sees a partial
 * line and no line ever spans a rotation.  Rotation is size-based
 * with a single .1 generation (close, rename over any existing .1,
 * reopen).  The whole writer is best-effort by contract: errors are
 * reported to the caller; nothing here may block or fail the
 * database commit path.
 *
 * The event body ("rec") is serialized schema-driven, in schema field
 * order, with JSON string escaping matching libnvpair's
 * nvlist_print_json_string() (\", \\, \n, \r, \t, \b, \f, \uXXXX for
 * control characters); non-ASCII UTF-8 bytes pass through verbatim,
 * which any JSON parser accepts.  op decodes through the schema enum,
 * so the spool line's "op" value is the same name the event_type
 * column stores.  The writer owns no warning channel: the caller
 * reports failures, keeping this TU unit-testable.
 *
 * Envelope (frozen contract, master.md):
 *   {"zmetad":1,"type":"event","dataset":"t/h","ts":1696312345,
 *    "rec":{...}}
 *   {"zmetad":1,"type":"epoch","dataset":"t/h","ts":...,
 *    "old_guid":111,"new_guid":222}
 *   {"zmetad":1,"type":"gap","dataset":"t/h","ts":...,"lost":37}
 *   {"zmetad":1,"type":"gap","dataset":"t/h","ts":...,"swap":true}
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdarg.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <libnvpair.h>

#include "zmetad_spool.h"
#include "zmetad_schema.h"

/* The lost sentinel for ring replacement, as stored in gaps.lost. */
#define	SPOOL_LOST_SWAP	((uint64_t)-1)

typedef struct spool_buf {
	char	*data;
	size_t	len;
	size_t	cap;
} spool_buf_t;

struct zmetad_spool {
	char		path[PATH_MAX];
	size_t		max_bytes;
	boolean_t	fsync;
	int		fd;
	boolean_t	poison;
	/* Schema used to serialize event bodies (not owned). */
	const zmetad_schema_t *schema;
};

/* Growable line buffer. */

static int
spool_buf_reserve(spool_buf_t *b, size_t extra)
{
	size_t need = b->len + extra;
	size_t cap;
	char *nd;

	if (need <= b->cap)
		return (0);

	/*
	 * Geometric growth with a generous but finite line ceiling: a
	 * line is at most the envelope plus one record (<= 18 schema
	 * fields), so 1 MiB is far above any real line; refusing
	 * beyond it keeps a pathological record from allocating
	 * without bound.
	 */
#define	SPOOL_MAX_LINE	(1u << 20)
	if (need > SPOOL_MAX_LINE)
		return (-1);

	cap = (b->cap == 0) ? 256 : b->cap;
	while (cap < need)
		cap *= 2;
	nd = realloc(b->data, cap);
	if (nd == NULL)
		return (-1);
	b->data = nd;
	b->cap = cap;
	return (0);
#undef	SPOOL_MAX_LINE
}

static void
spool_buf_append(spool_buf_t *b, const char *s, size_t n)
{
	(void) memcpy(b->data + b->len, s, n);
	b->len += n;
}

static int
spool_buf_puts(spool_buf_t *b, const char *s)
{
	size_t n = strlen(s);

	if (spool_buf_reserve(b, n) != 0)
		return (-1);
	spool_buf_append(b, s, n);
	return (0);
}

static int
spool_buf_u64(spool_buf_t *b, uint64_t v)
{
	char tmp[24];
	int n = snprintf(tmp, sizeof (tmp), "%llu",
	    (unsigned long long)v);

	if (n < 0 || (size_t)n >= sizeof (tmp) ||
	    spool_buf_reserve(b, (size_t)n) != 0)
		return (-1);
	spool_buf_append(b, tmp, (size_t)n);
	return (0);
}

/*
 * Append "s" as a quoted, JSON-escaped string.  Worst case is six
 * bytes per input byte (\u00XX).
 */
static int
spool_json_string(spool_buf_t *b, const char *s)
{
	size_t n = strlen(s);

	if (spool_buf_reserve(b, 6 * n + 2) != 0)
		return (-1);
	spool_buf_append(b, "\"", 1);
	for (size_t i = 0; i < n; i++) {
		unsigned char c = (unsigned char)s[i];

		switch (c) {
		case '"':
			spool_buf_append(b, "\\\"", 2);
			break;
		case '\\':
			spool_buf_append(b, "\\\\", 2);
			break;
		case '\n':
			spool_buf_append(b, "\\n", 2);
			break;
		case '\r':
			spool_buf_append(b, "\\r", 2);
			break;
		case '\t':
			spool_buf_append(b, "\\t", 2);
			break;
		case '\b':
			spool_buf_append(b, "\\b", 2);
			break;
		case '\f':
			spool_buf_append(b, "\\f", 2);
			break;
		default:
			if (c < 0x20) {
				char esc[8];

				(void) snprintf(esc, sizeof (esc),
				    "\\u%04x", (unsigned)c);
				spool_buf_append(b, esc, 6);
			} else {
				/*
				 * Printable ASCII and UTF-8
				 * continuation/lead bytes verbatim.
				 */
				spool_buf_append(b, (const char *)s + i, 1);
			}
			break;
		}
	}
	spool_buf_append(b, "\"", 1);
	return (0);
}

/*
 * Serialize the record's schema-known fields in schema order:
 * {"txg":15042,"object":128,"op":"TRUNCATE",...}.  Absent fields are
 * skipped; op decodes through the schema enum exactly as the
 * event_type column does.
 */
static int
spool_rec_serialize(spool_buf_t *b, const zmetad_schema_t *zs,
    const nvlist_t *rec)
{
	nvlist_t *mrec = (nvlist_t *)(uintptr_t)rec;
	boolean_t first = B_TRUE;

	if (spool_buf_reserve(b, 1) != 0)
		return (-1);
	spool_buf_append(b, "{", 1);
	if (zs == NULL || rec == NULL) {
		spool_buf_append(b, "}", 1);
		return (0);
	}

	for (uint_t i = 0; i < zmetad_schema_nfields(zs); i++) {
		const char *name = zmetad_schema_field_name(zs, i);
		uint64_t u = 0;
		uint_t nelem = 0;
		data_type_t dtype = DATA_TYPE_UNKNOWN;
		int rc = zmetad_schema_field(zs, name, mrec, &u, &nelem,
		    &dtype);

		if (rc == ENOENT)
			continue;
		if (rc != 0)
			return (-1);

		if (!first && (spool_buf_reserve(b, 1) != 0))
			return (-1);
		if (!first)
			spool_buf_append(b, ",", 1);
		first = B_FALSE;

		if (spool_json_string(b, name) != 0)
			return (-1);
		if (spool_buf_reserve(b, 1) != 0)
			return (-1);
		spool_buf_append(b, ":", 1);
		if (dtype == DATA_TYPE_STRING) {
			/*
			 * STRING decodes with the pointer stored in the
			 * caller's uint64_t slot (the decoder's union);
			 * recover it verbatim.
			 */
			const char *s = (const char *)(uintptr_t)u;

			if (spool_json_string(b, s) != 0)
				return (-1);
		} else if (strcmp(name, "op") == 0) {
			/*
			 * op decodes through the schema enum so the
			 * spool line carries the same op NAME the
			 * event_type column stores, not the wire
			 * number.
			 */
			if (spool_json_string(b,
			    zmetad_schema_op_name(zs, u)) != 0)
				return (-1);
		} else {
			if (spool_buf_u64(b, u) != 0)
				return (-1);
		}
	}

	if (spool_buf_reserve(b, 1) != 0)
		return (-1);
	spool_buf_append(b, "}", 1);
	return (0);
}

static int
spool_buf_init(spool_buf_t *b)
{
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
	return (spool_buf_reserve(b, 64));
}

static void
spool_buf_free(spool_buf_t *b)
{
	free(b->data);
	b->data = NULL;
	b->len = 0;
	b->cap = 0;
}

/* Lifecycle. */

int
zmetad_spool_open(zmetad_spool_t **sp, const char *path,
    size_t max_bytes, boolean_t fsync)
{
	zmetad_spool_t *s;

	if (sp == NULL || path == NULL || path[0] == '\0')
		return (EINVAL);

	s = calloc(1, sizeof (*s));
	if (s == NULL)
		return (ENOMEM);
	if (strlcpy(s->path, path, sizeof (s->path)) >=
	    sizeof (s->path)) {
		free(s);
		return (ENAMETOOLONG);
	}
	s->max_bytes = max_bytes;
	s->fsync = fsync;
	s->fd = -1;
	s->poison = B_FALSE;
	s->schema = NULL;
	*sp = s;
	return (0);
}

void
zmetad_spool_set_schema(zmetad_spool_t *s, const zmetad_schema_t *zs)
{
	if (s != NULL)
		s->schema = zs;
}

static int
spool_open_fd(zmetad_spool_t *s)
{
	int fd = open(s->path, O_WRONLY | O_CREAT | O_APPEND, 0644);

	if (fd < 0)
		return (-1);
	s->fd = fd;
	return (0);
}

static void
spool_close_fd(zmetad_spool_t *s)
{
	if (s->fd >= 0) {
		(void) close(s->fd);
		s->fd = -1;
	}
}

/*
 * Write one complete line (newline appended here).  Rotation happens
 * BEFORE the append when the pending line would cross the threshold,
 * so no line is ever split across a rotation.
 */
static int
spool_write_line(zmetad_spool_t *s, spool_buf_t *b)
{
	uint64_t total = b->len + 1;
	ssize_t w;

	if (s->fd < 0 && spool_open_fd(s) != 0)
		return (-1);

	if (s->max_bytes > 0) {
		struct stat st;

		if (fstat(s->fd, &st) != 0)
			return (-1);
		if (st.st_size > 0 &&
		    (uint64_t)st.st_size + total > s->max_bytes) {
			char npath[sizeof (s->path) + 2];

			spool_close_fd(s);
			if (snprintf(npath, sizeof (npath), "%s.1",
			    s->path) >= (int)sizeof (npath))
				return (-1);
			/* Overwrites any existing .1 by design. */
			if (rename(s->path, npath) != 0 && errno != ENOENT)
				return (-1);
			if (spool_open_fd(s) != 0)
				return (-1);
		}
	}

	w = write(s->fd, b->data, b->len);
	if (w < 0 || (size_t)w != b->len) {
		/* A partial line is an error for that line, not a retry. */
		spool_close_fd(s);
		return (-1);
	}
	if (write(s->fd, "\n", 1) != 1) {
		spool_close_fd(s);
		return (-1);
	}
	return (0);
}

/*
 * Poisoning: after any write error the handle refuses further writes
 * until the caller clears the flag at the start of the next collect
 * cycle, so one ENOSPC produces one warning per cycle, not one per
 * record.  Public via the header as zmetad_spool_poison / _resume.
 */
boolean_t
zmetad_spool_poisoned(zmetad_spool_t *s)
{
	return (s != NULL ? s->poison : B_TRUE);
}

void
zmetad_spool_resume(zmetad_spool_t *s)
{
	if (s != NULL)
		s->poison = B_FALSE;
}

/* Envelope assembly. */

static int
spool_envelope_head(spool_buf_t *b, const char *type, const char *dataset,
    uint64_t ts)
{
	if (spool_buf_puts(b, "{\"zmetad\":1,\"type\":") != 0 ||
	    spool_json_string(b, type) != 0 ||
	    spool_buf_puts(b, ",\"dataset\":") != 0 ||
	    spool_json_string(b, dataset) != 0 ||
	    spool_buf_puts(b, ",\"ts\":") != 0 ||
	    spool_buf_u64(b, ts) != 0)
		return (-1);
	return (0);
}

int
zmetad_spool_event(zmetad_spool_t *s, const char *dataset,
    const nvlist_t *rec, uint64_t captured_at)
{
	spool_buf_t b;
	int rc;

	if (s == NULL || dataset == NULL)
		return (EINVAL);
	if (s->poison)
		return (EIO);
	if (spool_buf_init(&b) != 0)
		return (ENOMEM);

	rc = spool_envelope_head(&b, "event", dataset, captured_at);
	if (rc == 0) {
		rc = (spool_buf_puts(&b, ",\"rec\":") != 0 ||
		    spool_rec_serialize(&b, s->schema, rec) != 0 ||
		    spool_buf_puts(&b, "}") != 0) ? -1 : 0;
	}
	if (rc == 0 && spool_write_line(s, &b) != 0)
		s->poison = B_TRUE;
	if (rc == 0 && s->fsync && fsync(s->fd) != 0)
		s->poison = B_TRUE;

	spool_buf_free(&b);
	return (s->poison ? EIO : 0);
}

int
zmetad_spool_marker(zmetad_spool_t *s, const char *dataset,
    const char *type, ...)
{
	spool_buf_t b;
	va_list ap;
	int rc;

	if (s == NULL || dataset == NULL || type == NULL)
		return (EINVAL);
	if (strcmp(type, "epoch") != 0 && strcmp(type, "gap") != 0)
		return (EINVAL);
	if (s->poison)
		return (EIO);
	if (spool_buf_init(&b) != 0)
		return (ENOMEM);

	rc = spool_envelope_head(&b, type, dataset, (uint64_t)time(NULL));
	if (rc == 0) {
		/*
		 * Payload: NULL-terminated (name, uint64) pairs.  The
		 * gap swap sentinel ((uint64_t)-1, from gaps.lost)
		 * renders as "swap":true per the envelope contract.
		 */
		const char *name;

		va_start(ap, type);
		while ((name = va_arg(ap, const char *)) != NULL) {
			uint64_t val = va_arg(ap, uint64_t);
			boolean_t swap = (strcmp(type, "gap") == 0 &&
			    strcmp(name, "lost") == 0 &&
			    val == SPOOL_LOST_SWAP);

			/*
			 * The gap swap sentinel renders as
			 * "swap":true: the KEY becomes "swap"
			 * (the envelope contract has no
			 * "lost":true form), the value the
			 * JSON true literal.
			 */
			if (spool_buf_puts(&b, ",") != 0 ||
			    spool_json_string(&b, swap ? "swap" : name)
			    != 0 ||
			    spool_buf_puts(&b, swap ? ":true" : ":")
			    != 0) {
				rc = -1;
				break;
			}
			if (!swap && spool_buf_u64(&b, val) != 0) {
				rc = -1;
				break;
			}
		}
		va_end(ap);
	}
	if (rc == 0)
		rc = spool_buf_puts(&b, "}") != 0 ? -1 : 0;
	if (rc == 0 && spool_write_line(s, &b) != 0)
		s->poison = B_TRUE;
	if (rc == 0 && s->fsync && fsync(s->fd) != 0)
		s->poison = B_TRUE;

	spool_buf_free(&b);
	return (s->poison ? EIO : 0);
}

void
zmetad_spool_close(zmetad_spool_t *s)
{
	if (s == NULL)
		return;
	spool_close_fd(s);
	free(s);
}
