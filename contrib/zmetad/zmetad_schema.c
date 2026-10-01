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
 * Event schema loader for zmetad.
 *
 * Parses the canonical event schema document (events-schema.json, or
 * an identical embedded copy) with a minimal hand-rolled structural
 * scanner.  Only schema_version, fields[].name/type and the op enum
 * are interpreted; every other key is skipped so that newer documents
 * remain readable by older daemons (forward compatibility).
 *
 * The scanner deliberately understands just enough JSON to do this:
 * no external parser is linked in.
 */

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libnvpair.h>

#include "zmetad_schema.h"
#include "schema_blob.h"

#define	ZMETAD_SCHEMA_MAX_FIELDS	64
#define	ZMETAD_SCHEMA_MAX_ENUM		64
#define	ZMETAD_MAX_STRING		256
#define	ZMETAD_MAX_FILE			(1 << 20)

/*
 * Bound on container nesting for the structural skipper.  Documents
 * deeper than this are rejected rather than risking stack exhaustion
 * on a hostile --check-schema input.
 */
#define	ZMETAD_JPARSE_MAX_DEPTH		32

typedef enum {
	ZST_UINT16 = 0,
	ZST_UINT64,
	ZST_STRING
} zmetad_stype_t;

typedef struct zmetad_field {
	char		name[ZMETAD_MAX_STRING];
	zmetad_stype_t	type;
} zmetad_field_t;

struct zmetad_schema {
	uint64_t	version;
	uint_t		nfields;
	zmetad_field_t	fields[ZMETAD_SCHEMA_MAX_FIELDS];
	uint_t		nenum;
	char		enum_names[ZMETAD_SCHEMA_MAX_ENUM][ZMETAD_MAX_STRING];
};

/*
 * Minimal recursive-descent JSON scanner state.  On error "err" is set
 * to a static message and every parse routine returns -1.  "depth"
 * bounds the jskip_value/jskip_container mutual recursion so hostile
 * input cannot exhaust the stack.
 */
typedef struct zmetad_jparse {
	const char	*p;
	const char	*err;
	int		depth;
} zmetad_jparse_t;

static void
jskip_ws(zmetad_jparse_t *jp)
{
	while (*jp->p == ' ' || *jp->p == '\t' || *jp->p == '\n' ||
	    *jp->p == '\r')
		jp->p++;
}

/*
 * Parse a JSON string into buf.  Escapes are decoded; \uXXXX is not
 * needed by the schema and decodes to '?'.  The result is always
 * NUL-terminated.
 */
static int
jstring(zmetad_jparse_t *jp, char *buf, size_t buflen)
{
	size_t n = 0;

	jskip_ws(jp);
	if (*jp->p != '"') {
		jp->err = "expected string";
		return (-1);
	}
	jp->p++;
	while (*jp->p != '"') {
		char c = *jp->p;

		if (c == '\0') {
			jp->err = "unterminated string";
			return (-1);
		}
		if (c == '\\') {
			jp->p++;
			switch (*jp->p) {
			case '"':
			case '\\':
			case '/':
				c = *jp->p;
				break;
			case 'n':
				c = '\n';
				break;
			case 't':
				c = '\t';
				break;
			case 'r':
				c = '\r';
				break;
			case 'b':
				c = '\b';
				break;
			case 'f':
				c = '\f';
				break;
			case 'u': {
				/*
				 * \uXXXX: the schema never needs it, so
				 * it decodes to '?', but the four hex
				 * digits must still be present and valid
				 * - advancing blindly would read past
				 * the end of a truncated buffer.
				 */
				int i;
				for (i = 0; i < 4; i++) {
					jp->p++;
					if (*jp->p == '\0') {
						jp->err =
						    "unterminated string";
						return (-1);
					}
					if (!isxdigit((unsigned char)*jp->p)) {
						jp->err =
						    "bad escape sequence";
						return (-1);
					}
				}
				c = '?';
				break;
			}
			case '\0':
				jp->err = "unterminated string";
				return (-1);
			default:
				jp->err = "bad escape sequence";
				return (-1);
			}
		}
		if (n + 1 >= buflen) {
			jp->err = "string too long";
			return (-1);
		}
		buf[n++] = c;
		jp->p++;
	}
	jp->p++;
	buf[n] = '\0';
	return (0);
}

/* Parse a non-negative integer.  Floats and negatives are rejected. */
static int
jnumber(zmetad_jparse_t *jp, uint64_t *out)
{
	uint64_t v = 0;

	jskip_ws(jp);
	if (*jp->p == '-') {
		jp->err = "negative number";
		return (-1);
	}
	if (!isdigit((unsigned char)*jp->p)) {
		jp->err = "expected number";
		return (-1);
	}
	while (isdigit((unsigned char)*jp->p)) {
		uint64_t d = (uint64_t)(*jp->p - '0');

		/*
		 * Reject values that would wrap UINT64_MAX (a schema
		 * version of 2^64+1 must not silently decode as 1).
		 */
		if (v > (UINT64_MAX - d) / 10) {
			jp->err = "number overflow";
			return (-1);
		}
		v = v * 10 + d;
		jp->p++;
	}
	if (*jp->p == '.' || *jp->p == 'e' || *jp->p == 'E') {
		jp->err = "non-integer number";
		return (-1);
	}
	*out = v;
	return (0);
}

static int
jskip_value(zmetad_jparse_t *jp);

/* Skip an object or array whose opening character has been consumed. */
static int
jskip_container(zmetad_jparse_t *jp, char close)
{
	char buf[ZMETAD_MAX_STRING];
	int rc = -1;

	/*
	 * Bound the jskip_value/jskip_container mutual recursion: a
	 * megabyte of "[[[[" must not be able to exhaust the stack.
	 */
	if (jp->depth >= ZMETAD_JPARSE_MAX_DEPTH) {
		jp->err = "document nested too deep";
		return (-1);
	}
	jp->depth++;

	jskip_ws(jp);
	if (*jp->p == close) {
		jp->p++;
		rc = 0;
		goto out;
	}
	for (;;) {
		if (close == '}') {
			if (jstring(jp, buf, sizeof (buf)) != 0)
				goto out;
			jskip_ws(jp);
			if (*jp->p != ':') {
				jp->err = "expected ':'";
				goto out;
			}
			jp->p++;
		}
		if (jskip_value(jp) != 0)
			goto out;
		jskip_ws(jp);
		if (*jp->p == ',') {
			jp->p++;
			jskip_ws(jp);
			continue;
		}
		if (*jp->p == close) {
			jp->p++;
			rc = 0;
			goto out;
		}
		jp->err = "malformed container";
		goto out;
	}

out:
	jp->depth--;
	return (rc);
}

/* Skip over any JSON value without interpreting it. */
static int
jskip_value(zmetad_jparse_t *jp)
{
	char buf[ZMETAD_MAX_STRING];
	uint64_t num;

	jskip_ws(jp);
	switch (*jp->p) {
	case '"':
		return (jstring(jp, buf, sizeof (buf)));
	case '{':
		jp->p++;
		return (jskip_container(jp, '}'));
	case '[':
		jp->p++;
		return (jskip_container(jp, ']'));
	case 't':
		if (strncmp(jp->p, "true", 4) == 0) {
			jp->p += 4;
			return (0);
		}
		break;
	case 'f':
		if (strncmp(jp->p, "false", 5) == 0) {
			jp->p += 5;
			return (0);
		}
		break;
	case 'n':
		if (strncmp(jp->p, "null", 4) == 0) {
			jp->p += 4;
			return (0);
		}
		break;
	default:
		return (jnumber(jp, &num));
	}
	jp->err = "bad literal";
	return (-1);
}

/* Convert a schema type name to its internal code. */
static int
stype_from_name(const char *name, zmetad_stype_t *out)
{
	if (strcmp(name, "uint16") == 0)
		*out = ZST_UINT16;
	else if (strcmp(name, "uint64") == 0)
		*out = ZST_UINT64;
	else if (strcmp(name, "string") == 0)
		*out = ZST_STRING;
	else
		return (-1);
	return (0);
}

/*
 * Parse one field definition object.  Only "type" is required; "enum"
 * is captured when this is the op field; all other keys are skipped.
 */
static int
jfield_def(zmetad_jparse_t *jp, zmetad_schema_t *zs, const char *fname)
{
	char key[ZMETAD_MAX_STRING];
	char type[ZMETAD_MAX_STRING];
	boolean_t have_type = B_FALSE;

	jskip_ws(jp);
	if (*jp->p != '{') {
		jp->err = "expected field definition object";
		return (-1);
	}
	jp->p++;
	jskip_ws(jp);
	if (*jp->p == '}') {
		jp->p++;
		jp->err = "field definition missing type";
		return (-1);
	}
	for (;;) {
		if (jstring(jp, key, sizeof (key)) != 0)
			return (-1);
		jskip_ws(jp);
		if (*jp->p != ':') {
			jp->err = "expected ':'";
			return (-1);
		}
		jp->p++;

		if (strcmp(key, "type") == 0) {
			if (jstring(jp, type, sizeof (type)) != 0)
				return (-1);
			have_type = B_TRUE;
		} else if (strcmp(key, "enum") == 0 &&
		    strcmp(fname, "op") == 0) {
			jskip_ws(jp);
			if (*jp->p != '[') {
				jp->err = "expected enum array";
				return (-1);
			}
			jp->p++;
			jskip_ws(jp);
			while (*jp->p != ']') {
				if (zs->nenum >= ZMETAD_SCHEMA_MAX_ENUM) {
					jp->err = "too many enum entries";
					return (-1);
				}
				if (jstring(jp, zs->enum_names[zs->nenum],
				    ZMETAD_MAX_STRING) != 0)
					return (-1);
				zs->nenum++;
				jskip_ws(jp);
				if (*jp->p == ',') {
					jp->p++;
					jskip_ws(jp);
					/*
					 * No trailing comma, consistent
					 * with every other container.
					 */
					if (*jp->p == ']') {
						jp->err = "trailing comma";
						return (-1);
					}
				} else if (*jp->p != ']') {
					jp->err = "malformed enum array";
					return (-1);
				}
			}
			jp->p++;
		} else {
			if (jskip_value(jp) != 0)
				return (-1);
		}

		jskip_ws(jp);
		if (*jp->p == ',') {
			jp->p++;
			jskip_ws(jp);
			continue;
		}
		if (*jp->p == '}') {
			jp->p++;
			break;
		}
		jp->err = "malformed field definition";
		return (-1);
	}

	if (!have_type) {
		jp->err = "field definition missing type";
		return (-1);
	}
	if (zs->nfields >= ZMETAD_SCHEMA_MAX_FIELDS) {
		jp->err = "too many fields";
		return (-1);
	}
	if (stype_from_name(type, &zs->fields[zs->nfields].type) != 0) {
		jp->err = "unknown field type";
		return (-1);
	}
	(void) strlcpy(zs->fields[zs->nfields].name, fname,
	    ZMETAD_MAX_STRING);
	zs->nfields++;
	return (0);
}

/* Walk the top-level document, extracting the pieces we understand. */
/*
 * Parse the "fields" object: a map of field name to field definition.
 */
static int
jfields_object(zmetad_jparse_t *jp, zmetad_schema_t *zs)
{
	jskip_ws(jp);
	if (*jp->p != '{') {
		jp->err = "expected fields object";
		return (-1);
	}
	jp->p++;
	jskip_ws(jp);
	if (*jp->p == '}') {
		jp->p++;
		return (0);
	}

	for (;;) {
		char fname[ZMETAD_MAX_STRING];

		if (jstring(jp, fname, sizeof (fname)) != 0)
			return (-1);
		for (uint_t i = 0; i < zs->nfields; i++) {
			if (strcmp(zs->fields[i].name, fname) == 0) {
				jp->err = "duplicate key";
				return (-1);
			}
		}
		jskip_ws(jp);
		if (*jp->p != ':') {
			jp->err = "expected ':'";
			return (-1);
		}
		jp->p++;
		if (jfield_def(jp, zs, fname) != 0)
			return (-1);
		jskip_ws(jp);
		if (*jp->p == ',') {
			jp->p++;
			jskip_ws(jp);
			continue;
		}
		if (*jp->p == '}') {
			jp->p++;
			break;
		}
		jp->err = "malformed fields object";
		return (-1);
	}

	return (0);
}

static int
jdocument(zmetad_jparse_t *jp, zmetad_schema_t *zs)
{
	char key[ZMETAD_MAX_STRING];
	boolean_t have_version = B_FALSE;
	boolean_t have_format = B_FALSE;

	jskip_ws(jp);
	if (*jp->p != '{') {
		jp->err = "expected JSON object";
		return (-1);
	}
	jp->p++;
	jskip_ws(jp);
	if (*jp->p == '}') {
		jp->p++;
		jp->err = "empty schema document";
		return (-1);
	}
	for (;;) {
		if (jstring(jp, key, sizeof (key)) != 0)
			return (-1);
		jskip_ws(jp);
		if (*jp->p != ':') {
			jp->err = "expected ':'";
			return (-1);
		}
		jp->p++;

		if (strcmp(key, "schema_version") == 0) {
			if (have_version) {
				jp->err = "duplicate key";
				return (-1);
			}
			have_version = B_TRUE;
			if (jnumber(jp, &zs->version) != 0)
				return (-1);
		} else if (strcmp(key, "record_format") == 0) {
			if (have_format) {
				jp->err = "duplicate key";
				return (-1);
			}
			have_format = B_TRUE;
			jskip_ws(jp);
			if (*jp->p != '{') {
				jp->err = "expected record_format object";
				return (-1);
			}
			jp->p++;
			jskip_ws(jp);
			if (*jp->p == '}') {
				jp->p++;
			} else {
				for (;;) {
					char fkey[ZMETAD_MAX_STRING];

					if (jstring(jp, fkey,
					    sizeof (fkey)) != 0)
						return (-1);
					jskip_ws(jp);
					if (*jp->p != ':') {
						jp->err = "expected ':'";
						return (-1);
					}
					jp->p++;
					if (strcmp(fkey, "fields") == 0) {
						if (jfields_object(jp, zs) != 0)
							return (-1);
					} else {
						if (jskip_value(jp) != 0)
							return (-1);
					}
					jskip_ws(jp);
					if (*jp->p == ',') {
						jp->p++;
						jskip_ws(jp);
						continue;
					}
					if (*jp->p == '}') {
						jp->p++;
						break;
					}
					jp->err = "malformed record_format";
					return (-1);
				}
			}
		} else {
			if (jskip_value(jp) != 0)
				return (-1);
		}

		jskip_ws(jp);
		if (*jp->p == ',') {
			jp->p++;
			jskip_ws(jp);
			continue;
		}
		if (*jp->p == '}') {
			jp->p++;
			break;
		}
		jp->err = "malformed document";
		return (-1);
	}
	return (0);
}

static void
schema_seterr(char *errbuf, const char *fmt, ...)
{
	va_list ap;

	if (errbuf == NULL)
		return;
	va_start(ap, fmt);
	(void) vsnprintf(errbuf, 256, fmt, ap);
	va_end(ap);
}

static char *
schema_read_file(const char *path, char *errbuf)
{
	FILE *fp;
	char *buf;
	size_t n;

	fp = fopen(path, "r");
	if (fp == NULL) {
		schema_seterr(errbuf, "cannot open %s: %s", path,
		    strerror(errno));
		return (NULL);
	}
	buf = malloc(ZMETAD_MAX_FILE);
	if (buf == NULL) {
		(void) fclose(fp);
		schema_seterr(errbuf, "out of memory");
		return (NULL);
	}
	n = fread(buf, 1, ZMETAD_MAX_FILE - 1, fp);
	if (ferror(fp)) {
		schema_seterr(errbuf, "read error on %s", path);
		(void) fclose(fp);
		free(buf);
		return (NULL);
	}
	/*
	 * If the buffer filled and the file is not at EOF, the document
	 * is larger than ZMETAD_MAX_FILE.  Report it distinctly rather
	 * than parsing a silently truncated copy (which would surface as
	 * a bogus "unterminated" parse error).
	 */
	if (n == ZMETAD_MAX_FILE - 1 && fgetc(fp) != EOF) {
		schema_seterr(errbuf, "file too large: %s exceeds %d bytes",
		    path, ZMETAD_MAX_FILE);
		(void) fclose(fp);
		free(buf);
		return (NULL);
	}
	(void) fclose(fp);
	buf[n] = '\0';
	return (buf);
}

zmetad_schema_t *
zmetad_schema_load(const char *path, char *errbuf)
{
	zmetad_schema_t *zs;
	zmetad_jparse_t jp;
	char *buf = NULL;
	const char *text;
	size_t offset;

	if (errbuf != NULL)
		errbuf[0] = '\0';

	zs = calloc(1, sizeof (*zs));
	if (zs == NULL) {
		schema_seterr(errbuf, "out of memory");
		return (NULL);
	}

	if (path == NULL) {
		text = ZMETAD_EMBEDDED_SCHEMA_JSON;
	} else {
		buf = schema_read_file(path, errbuf);
		if (buf == NULL) {
			free(zs);
			return (NULL);
		}
		text = buf;
	}

	jp.p = text;
	jp.err = NULL;
	jp.depth = 0;
	if (jdocument(&jp, zs) != 0) {
		offset = (size_t)(jp.p - text);
		schema_seterr(errbuf, "malformed schema at offset %lu: %s",
		    (unsigned long)offset, jp.err != NULL ? jp.err : "error");
		free(buf);
		free(zs);
		return (NULL);
	}

	/* Nothing but whitespace may follow the top-level document. */
	jskip_ws(&jp);
	if (*jp.p != '\0') {
		offset = (size_t)(jp.p - text);
		schema_seterr(errbuf,
		    "malformed schema at offset %lu: trailing garbage",
		    (unsigned long)offset);
		free(buf);
		free(zs);
		return (NULL);
	}
	free(buf);

	if (zs->nfields == 0) {
		schema_seterr(errbuf, "schema defines no fields");
		free(zs);
		return (NULL);
	}
	if (zs->version == 0) {
		schema_seterr(errbuf, "missing or zero schema_version");
		free(zs);
		return (NULL);
	}

	return (zs);
}

int
zmetad_schema_check_version(const zmetad_schema_t *zs, uint64_t wire)
{
	if (zs == NULL)
		return (EINVAL);

	/*
	 * wire == 0 means the kernel did not report a schema version at
	 * all (pre-exposure kernels); that is always acceptable.
	 *
	 * A wire version OLDER than the daemon's is accepted: schema
	 * versions only add fields and op values, so a v2 daemon decodes
	 * a v1 wire record correctly (unknown ops surface as "UNKNOWN"
	 * and absent optional fields as ENOENT). A wire version NEWER
	 * than the daemon may carry records this daemon cannot decode
	 * faithfully and is refused.
	 */
	if (wire == 0)
		return (0);

	if (wire > zs->version)
		return (EINVAL);

	return (0);
}

/* Human-readable name of an internal schema type code. */
static const char *
stype_name(zmetad_stype_t t)
{
	switch (t) {
	case ZST_UINT16:
		return ("uint16");
	case ZST_UINT64:
		return ("uint64");
	case ZST_STRING:
		return ("string");
	}
	return ("unknown");
}

/*
 * Compare two loaded schemas for structural equality: same field
 * count, and identical name and type at every index (order matters -
 * the field index is the wire ordering).  Returns 0 when equal;
 * otherwise non-zero with the first difference described in errbuf.
 */
int
zmetad_schema_compare(const zmetad_schema_t *a, const zmetad_schema_t *b,
    char *errbuf, size_t errlen)
{
	if (a == NULL || b == NULL) {
		if (errbuf != NULL && errlen > 0)
			(void) snprintf(errbuf, errlen,
			    "NULL schema argument");
		return (EINVAL);
	}

	if (a->nfields != b->nfields) {
		if (errbuf != NULL && errlen > 0)
			(void) snprintf(errbuf, errlen,
			    "field count differs: %u vs %u",
			    (unsigned)a->nfields, (unsigned)b->nfields);
		return (1);
	}

	for (uint_t i = 0; i < a->nfields; i++) {
		if (strcmp(a->fields[i].name, b->fields[i].name) != 0) {
			if (errbuf != NULL && errlen > 0)
				(void) snprintf(errbuf, errlen,
				    "field %u name differs: %s vs %s",
				    (unsigned)i, a->fields[i].name,
				    b->fields[i].name);
			return (1);
		}
		if (a->fields[i].type != b->fields[i].type) {
			if (errbuf != NULL && errlen > 0)
				(void) snprintf(errbuf, errlen,
				    "field %u (%s) type differs: %s vs %s",
				    (unsigned)i, a->fields[i].name,
				    stype_name(a->fields[i].type),
				    stype_name(b->fields[i].type));
			return (1);
		}
	}

	if (errbuf != NULL && errlen > 0)
		errbuf[0] = '\0';
	return (0);
}

static const zmetad_field_t *
schema_find_field(const zmetad_schema_t *zs, const char *name)
{
	for (uint_t i = 0; i < zs->nfields; i++) {
		if (strcmp(zs->fields[i].name, name) == 0)
			return (&zs->fields[i]);
	}
	return (NULL);
}


int
zmetad_schema_field(const zmetad_schema_t *zs, const char *name,
    nvlist_t *rec, void *out, uint_t *nelem, data_type_t *dtype)
{
	const zmetad_field_t *zf;
	int err;

	if (zs == NULL || name == NULL || rec == NULL || out == NULL)
		return (EINVAL);

	zf = schema_find_field(zs, name);
	if (zf == NULL)
		return (ENOENT);

	if (nelem != NULL)
		*nelem = 0;
	if (dtype != NULL)
		*dtype = DATA_TYPE_UNKNOWN;

	/*
	 * Presence and type are separate questions: nvlist_lookup_uint64()
	 * and friends report a type mismatch as ENOENT (the typed lookup
	 * only matches pairs of the requested type), which would make an
	 * ETL bug look like an absent field. Look the pair up by name,
	 * then decode by the schema type and report EINVAL when the
	 * record's actual type disagrees.
	 */
	nvpair_t *pair = NULL;
	err = nvlist_lookup_nvpair(rec, name, &pair);
	if (err != 0)
		return (ENOENT);

	switch (zf->type) {
	case ZST_UINT16: {
		/*
		 * Decode into a true uint16_t and assign the widened
		 * value: *out is a uint64_t per the header contract,
		 * and writing through a (uint16_t *) alias would leave
		 * the high bytes untouched (byte-order dependent).
		 */
		uint16_t v16;

		if (nvpair_type(pair) != DATA_TYPE_UINT16)
			return (EINVAL);
		err = nvpair_value_uint16(pair, &v16);
		if (err != 0)
			return (err);
		*(uint64_t *)out = v16;
		if (dtype != NULL)
			*dtype = DATA_TYPE_UINT16;
		return (0);
	}
	case ZST_UINT64: {
		if (nvpair_type(pair) != DATA_TYPE_UINT64)
			return (EINVAL);
		err = nvpair_value_uint64(pair, (uint64_t *)out);
		if (err != 0)
			return (err);
		if (dtype != NULL)
			*dtype = DATA_TYPE_UINT64;
		return (0);
	}
	case ZST_STRING: {
		const char *s = NULL;

		if (nvpair_type(pair) != DATA_TYPE_STRING)
			return (EINVAL);
		err = nvpair_value_string(pair, &s);
		if (err != 0)
			return (err);
		*(const char **)out = s;
		if (nelem != NULL)
			*nelem = (uint_t)strlen(s);
		if (dtype != NULL)
			*dtype = DATA_TYPE_STRING;
		return (0);
	}
	}

	return (EINVAL);
}

uint_t
zmetad_schema_nfields(const zmetad_schema_t *zs)
{
	if (zs == NULL)
		return (0);
	return (zs->nfields);
}

const char *
zmetad_schema_field_name(const zmetad_schema_t *zs, uint_t idx)
{
	if (zs == NULL || idx >= zs->nfields)
		return (NULL);
	return (zs->fields[idx].name);
}

const char *
zmetad_schema_op_name(const zmetad_schema_t *zs, uint64_t op)
{
	if (zs == NULL || op >= zs->nenum)
		return ("UNKNOWN");
	return (zs->enum_names[op]);
}

uint64_t
zmetad_schema_version(const zmetad_schema_t *zs)
{
	if (zs == NULL)
		return (0);
	return (zs->version);
}

void
zmetad_schema_free(zmetad_schema_t *zs)
{
	free(zs);
}


/*
 * Embedded copy of the canonical schema document, byte-identical to
 * contrib/zmetad/events-schema.json.  schema-check.sh enforces the
 * identity; do not edit this literal without updating the JSON file
 * (or vice versa) in the same change.
 */
const char *ZMETAD_EMBEDDED_SCHEMA_JSON =
	"{\n"
	"  \"schema_version\": 2,\n"
	"  \"record_format\": {\n"
	"    \"encoding\": \"nvlist-packed-native\",\n"
	"    \"record_header\": \"uint64 little-endian payload length\",\n"
	"    \"fields\": {\n"
	"      \"txg\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": true,\n"
	"        \"desc\": \"transaction group of the change\"\n"
	"      },\n"
	"      \"time\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": true,\n"
	"        \"desc\": \"hrtime nanoseconds\"\n"
	"      },\n"
	"      \"object\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": true,\n"
	"        \"desc\": \"object ID affected\"\n"
	"      },\n"
	"      \"op\": {\n"
	"        \"type\": \"uint16\",\n"
	"        \"since\": 1,\n"
	"        \"always\": true,\n"
	"        \"enum\": [\n"
	"          \"NONE\",\n"
	"          \"CREATE\",\n"
	"          \"REMOVE\",\n"
	"          \"RENAME\",\n"
	"          \"LINK\",\n"
	"          \"SYMLINK\",\n"
	"          \"TRUNCATE\",\n"
	"          \"SETATTR\",\n"
	"          \"WRITE\",\n"
	"          \"READ\"\n"
	"        ],\n"
	"        \"desc\": \"operation type; WRITE and READ are opt-in"
	" (events_io) and coalesced by the events_io_window fence: io_offset"
	" is the window's first offset, io_bytes the summed total\"\n"
	"      },\n"
	"      \"name\": {\n"
	"        \"type\": \"string\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"desc\": \"file/dir name\"\n"
	"      },\n"
	"      \"parent\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"desc\": \"parent object ID\"\n"
	"      },\n"
	"      \"old_name\": {\n"
	"        \"type\": \"string\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"RENAME\"\n"
	"        ],\n"
	"        \"desc\": \"old name (rename)\"\n"
	"      },\n"
	"      \"old_parent\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"RENAME\"\n"
	"        ],\n"
	"        \"desc\": \"old parent (rename)\"\n"
	"      },\n"
	"      \"target\": {\n"
	"        \"type\": \"string\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"SYMLINK\"\n"
	"        ],\n"
	"        \"desc\": \"symlink target\"\n"
	"      },\n"
	"      \"mode\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"CREATE\"\n"
	"        ],\n"
	"        \"desc\": \"file mode (create)\"\n"
	"      },\n"
	"      \"old_size\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"TRUNCATE\"\n"
	"        ],\n"
	"        \"desc\": \"size before truncate\"\n"
	"      },\n"
	"      \"new_size\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"TRUNCATE\"\n"
	"        ],\n"
	"        \"desc\": \"size after truncate\"\n"
	"      },\n"
	"      \"attrs\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"SETATTR\"\n"
	"        ],\n"
	"        \"desc\": \"changed attr mask\"\n"
	"      },\n"
	"      \"uid\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"desc\": \"user ID\"\n"
	"      },\n"
	"      \"gid\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 1,\n"
	"        \"always\": false,\n"
	"        \"desc\": \"group ID\"\n"
	"      },\n"
	"      \"io_offset\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 2,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"WRITE\",\n"
	"          \"READ\"\n"
	"        ],\n"
	"        \"desc\": \"IO start offset; when the fence window is open,"
	" the window's first offset\"\n"
	"      },\n"
	"      \"io_bytes\": {\n"
	"        \"type\": \"uint64\",\n"
	"        \"since\": 2,\n"
	"        \"always\": false,\n"
	"        \"ops\": [\n"
	"          \"WRITE\",\n"
	"          \"READ\"\n"
	"        ],\n"
	"        \"desc\": \"IO byte count; when the fence window is open,"
	" the summed total of coalesced IOs\"\n"
	"      }\n"
	"    },\n"
	"    \"invariants\": [\n"
	"      \"consumers MUST ignore fields they do not recognize (forward"
	" compat)\",\n"
	"      \"op values outside the enum MUST decode as UNKNOWN\",\n"
	"      \"names are dataset-relative at event time; resolve stability"
	" via object id\"\n"
	"    ]\n"
	"  }\n"
	"}\n";
