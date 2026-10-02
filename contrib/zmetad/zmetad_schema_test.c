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
 * Standalone test for the zmetad event schema parser.  Loads the
 * embedded schema blob and exercises the public API.  Exits 0 on
 * success; on failure prints a message and exits 1.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libnvpair.h>

#include "zmetad_schema.h"
#include "schema_blob.h"

static int g_fail;

#define	REQUIRE(cond)							\
	do {								\
		if (!(cond)) {						\
			fprintf(stderr, "FAIL %s:%d: %s\n",		\
			    __FILE__, __LINE__, #cond);			\
			g_fail = 1;					\
		}							\
	} while (0)

/*
 * Write "doc" to a fresh mkstemp() file, load it through
 * zmetad_schema_load(), assert the load FAILED with a non-empty
 * errbuf, and remove the file.  Secure temp handling: no fixed
 * /tmp paths (symlink attacks in shared CI).
 */
static void
expect_load_fail(const char *doc, const char *what)
{
	char path[] = "/tmp/zmetad_schema_test_XXXXXX";
	char errbuf[256];
	int fd;
	FILE *fp;

	fd = mkstemp(path);
	if (fd < 0) {
		fprintf(stderr, "FAIL: mkstemp for %s: %s\n", what,
		    strerror(errno));
		g_fail = 1;
		return;
	}
	fp = fdopen(fd, "w");
	if (fp == NULL) {
		fprintf(stderr, "FAIL: fdopen for %s: %s\n", what,
		    strerror(errno));
		g_fail = 1;
		(void) close(fd);
		(void) unlink(path);
		return;
	}
	if (fwrite(doc, 1, strlen(doc), fp) != strlen(doc)) {
		fprintf(stderr, "FAIL: short write for %s\n", what);
		g_fail = 1;
	}
	(void) fclose(fp);

	errbuf[0] = '\0';
	zmetad_schema_t *bad = zmetad_schema_load(path, errbuf);
	if (bad != NULL) {
		fprintf(stderr, "FAIL: %s accepted\n", what);
		g_fail = 1;
		zmetad_schema_free(bad);
	} else if (errbuf[0] == '\0') {
		fprintf(stderr, "FAIL: %s: empty errbuf\n", what);
		g_fail = 1;
	}
	(void) unlink(path);
}

/* Same, but the load must SUCCEED; returns the schema (caller frees). */
static zmetad_schema_t *
expect_load_ok(const char *doc, const char *what)
{
	char path[] = "/tmp/zmetad_schema_test_XXXXXX";
	char errbuf[256];
	zmetad_schema_t *zs;
	int fd;
	FILE *fp;

	fd = mkstemp(path);
	if (fd < 0) {
		fprintf(stderr, "FAIL: mkstemp for %s: %s\n", what,
		    strerror(errno));
		g_fail = 1;
		return (NULL);
	}
	fp = fdopen(fd, "w");
	if (fp == NULL) {
		fprintf(stderr, "FAIL: fdopen for %s: %s\n", what,
		    strerror(errno));
		g_fail = 1;
		(void) close(fd);
		(void) unlink(path);
		return (NULL);
	}
	if (fwrite(doc, 1, strlen(doc), fp) != strlen(doc)) {
		fprintf(stderr, "FAIL: short write for %s\n", what);
		g_fail = 1;
	}
	(void) fclose(fp);

	errbuf[0] = '\0';
	zs = zmetad_schema_load(path, errbuf);
	if (zs == NULL) {
		fprintf(stderr, "FAIL: %s rejected: %s\n", what, errbuf);
		g_fail = 1;
	}
	(void) unlink(path);
	return (zs);
}

int
main(void)
{
	zmetad_schema_t *zs;
	char errbuf[256];
	char badbuf[256];
	nvlist_t *rec;
	uint64_t u64;
	uint_t nelem;
	data_type_t dtype;
	const char *str;
	int rc;

	/*
	 * Every field the embedded schema must define, in order, with
	 * its declared type.  Pinning the whole table guards against
	 * silent parser regressions (a field quietly dropped or
	 * retyped would otherwise only surface in consumers).
	 */
	static const struct {
		const char *name;
		const char *type;	/* record-side nvpair type */
	} expected_fields[] = {
		{ "txg",	"uint64" },
		{ "time",	"uint64" },
		{ "object",	"uint64" },
		{ "op",		"uint16" },
		{ "name",	"string" },
		{ "parent",	"uint64" },
		{ "old_name",	"string" },
		{ "old_parent",	"uint64" },
		{ "target",	"string" },
		{ "mode",	"uint64" },
		{ "old_size",	"uint64" },
		{ "new_size",	"uint64" },
		{ "attrs",	"uint64" },
		{ "uid",	"uint64" },
		{ "gid",	"uint64" },
		{ "principal",	"uint64" },
		{ "io_offset",	"uint64" },
		{ "io_bytes",	"uint64" },
	};
	static const char *const expected_enum[] = {
		"NONE", "CREATE", "REMOVE", "RENAME", "LINK", "SYMLINK",
		"TRUNCATE", "SETATTR", "WRITE", "READ",
	};

	zs = zmetad_schema_load(NULL, errbuf);
	if (zs == NULL) {
		fprintf(stderr, "FAIL: embedded schema load: %s\n", errbuf);
		return (1);
	}

	REQUIRE(zmetad_schema_version(zs) == 3);
	REQUIRE(zmetad_schema_nfields(zs) == 18);

	/* Pin all 18 field names, in order. */
	REQUIRE(zmetad_schema_nfields(zs) ==
	    sizeof (expected_fields) / sizeof (expected_fields[0]));
	for (size_t i = 0; i < sizeof (expected_fields) /
	    sizeof (expected_fields[0]); i++) {
		const char *got = zmetad_schema_field_name(zs, (uint_t)i);

		if (got == NULL) {
			fprintf(stderr, "FAIL: field %zu missing\n", i);
			g_fail = 1;
			continue;
		}
		if (strcmp(got, expected_fields[i].name) != 0) {
			fprintf(stderr, "FAIL: field %zu: got %s want %s\n",
			    i, got, expected_fields[i].name);
			g_fail = 1;
		}
	}

	/*
	 * Pin field TYPES via decode behavior: build a record whose
	 * every field carries its schema type, then require rc == 0
	 * and the matching dtype for each.  A type regression (e.g. op
	 * silently retyped to uint64) fails here.
	 */
	rec = fnvlist_alloc();
	REQUIRE(rec != NULL);
	for (size_t i = 0; i < sizeof (expected_fields) /
	    sizeof (expected_fields[0]); i++) {
		if (strcmp(expected_fields[i].type, "uint16") == 0)
			fnvlist_add_uint16(rec, expected_fields[i].name, 1);
		else if (strcmp(expected_fields[i].type, "uint64") == 0)
			fnvlist_add_uint64(rec, expected_fields[i].name, 1);
		else
			fnvlist_add_string(rec, expected_fields[i].name, "s");
	}
	for (size_t i = 0; i < sizeof (expected_fields) /
	    sizeof (expected_fields[0]); i++) {
		const char *s = NULL;
		void *out;
		data_type_t want;

		if (strcmp(expected_fields[i].type, "uint16") == 0)
			want = DATA_TYPE_UINT16;
		else if (strcmp(expected_fields[i].type, "uint64") == 0)
			want = DATA_TYPE_UINT64;
		else
			want = DATA_TYPE_STRING;

		out = (want == DATA_TYPE_STRING) ? (void *)&s : (void *)&u64;
		rc = zmetad_schema_field(zs, expected_fields[i].name, rec,
		    out, &nelem, &dtype);
		if (rc != 0 || dtype != want) {
			fprintf(stderr,
			    "FAIL: field %s: rc=%d dtype=%d want=%d\n",
			    expected_fields[i].name, rc, (int)dtype,
			    (int)want);
			g_fail = 1;
		}
	}
	fnvlist_free(rec);

	/* Pin the op enum: count == 10, names in order, 10 -> UNKNOWN. */
	for (size_t i = 0; i < sizeof (expected_enum) /
	    sizeof (expected_enum[0]); i++) {
		const char *got = zmetad_schema_op_name(zs, (uint64_t)i);

		if (strcmp(got, expected_enum[i]) != 0) {
			fprintf(stderr, "FAIL: op %zu: got %s want %s\n",
			    i, got, expected_enum[i]);
			g_fail = 1;
		}
	}
	REQUIRE(strcmp(zmetad_schema_op_name(
	    zs, sizeof (expected_enum) / sizeof (expected_enum[0])),
	    "UNKNOWN") == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 999), "UNKNOWN") == 0);

	/*
	 * Wire negotiation: the embedded schema is v3, so wire 3 is
	 * accepted and wire 4 refused.  Wires 1 and 2 are accepted:
	 * schema versions only add fields and op values, so a v3
	 * daemon decodes a v1/v2 wire record (datasets whose event
	 * log predates a version bump report their create-time
	 * version forever).  Wire 0 always means "kernel did not
	 * report a version".
	 */
	REQUIRE(zmetad_schema_check_version(zs, 3) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 0) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 1) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 2) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 4) == EINVAL);

	/*
	 * The --check-schema drift check in zmetad.c does NOT reuse
	 * check_version: it requires file_version == embedded version
	 * exactly, so an older schema FILE is refused there (unlike an
	 * older wire version, which stays acceptable above).  That
	 * comparison is local to run_check_schema() and is not
	 * exercised by this binary; these assertions pin the embedded
	 * version it compares against.
	 */
	REQUIRE(zmetad_schema_version(zs) == 3);

	/*
	 * op must be declared uint16; the field lookup with a NULL
	 * record must fail with EINVAL (bad argument).
	 */
	rc = zmetad_schema_field(zs, "op", NULL, &u64, &nelem, &dtype);
	REQUIRE(rc == EINVAL);
	REQUIRE(zmetad_schema_field_name(zs, 3) != NULL);

	rec = fnvlist_alloc();
	REQUIRE(rec != NULL);
	fnvlist_add_uint64(rec, "uid", 1000);
	fnvlist_add_uint16(rec, "op", 2);
	fnvlist_add_string(rec, "name", "foo.txt");

	/* uid is uint64 in the schema; record value matches. */
	rc = zmetad_schema_field(zs, "uid", rec, &u64, &nelem, &dtype);
	REQUIRE(rc == 0);
	REQUIRE(u64 == 1000);
	REQUIRE(dtype == DATA_TYPE_UINT64);

	/*
	 * A uint16 schema field decodes zero-extended into the
	 * caller's uint64_t storage.  Dirty the output buffer first:
	 * the decoder must overwrite ALL 8 bytes, not just the low
	 * two (the old implementation wrote through a uint16_t alias
	 * and left the upper bytes untouched - correct on LE only by
	 * accident of callers pre-zeroing).
	 */
	u64 = 0xFFFFFFFFFFFFFFFFULL;
	rc = zmetad_schema_field(zs, "op", rec, &u64, &nelem, &dtype);
	REQUIRE(rc == 0);
	REQUIRE(u64 == 2);
	REQUIRE(dtype == DATA_TYPE_UINT16);

	/* String field. */
	rc = zmetad_schema_field(zs, "name", rec, &str, &nelem, &dtype);
	REQUIRE(rc == 0);
	REQUIRE(strcmp(str, "foo.txt") == 0);
	REQUIRE(dtype == DATA_TYPE_STRING);
	REQUIRE(nelem == 7);

	/*
	 * String-typed schema field fed a numeric nvpair must fail
	 * with EINVAL (type mismatch), not silently decode.
	 */
	{
		nvlist_t *srec = fnvlist_alloc();

		fnvlist_add_uint64(srec, "name", 42);
		rc = zmetad_schema_field(zs, "name", srec, &str, &nelem,
		    &dtype);
		REQUIRE(rc == EINVAL);
		fnvlist_free(srec);
	}

	/* Absent-but-known field is ENOENT (normal). */
	rc = zmetad_schema_field(zs, "gid", rec, &u64, &nelem, &dtype);
	if (rc != ENOENT)
		(void) fprintf(stderr, "absent-field rc=%d (%s)\n", rc,
		    strerror(rc));
	REQUIRE(rc == ENOENT);

	/*
	 * Type mismatch: uid is uint64 in the schema, so feeding the
	 * uint64 lookup a record that stores a string must yield
	 * EINVAL. Simulate by removing and re-adding with the wrong
	 * type.
	 */
	fnvlist_remove(rec, "uid");
	fnvlist_add_string(rec, "uid", "not-a-number");
	rc = zmetad_schema_field(zs, "uid", rec, &u64, &nelem, &dtype);
	if (rc != EINVAL) {
		(void) fprintf(stderr, "wrong-type lookup rc=%d (%s)\n",
		    rc, strerror(rc));
		REQUIRE(rc == EINVAL);
	}

	/* Unknown (bogus) field name is ENOENT. */
	rc = zmetad_schema_field(zs, "no_such_field", rec, &u64, &nelem,
	    &dtype);
	REQUIRE(rc == ENOENT);

	fnvlist_free(rec);

	/* NULL-argument accessor variants. */
	REQUIRE(zmetad_schema_nfields(NULL) == 0);
	REQUIRE(zmetad_schema_field_name(NULL, 0) == NULL);
	REQUIRE(zmetad_schema_field_name(zs, 18) == NULL);
	REQUIRE(zmetad_schema_version(NULL) == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(NULL, 0), "UNKNOWN") == 0);
	REQUIRE(zmetad_schema_check_version(NULL, 1) == EINVAL);
	REQUIRE(zmetad_schema_field(NULL, "op", NULL, &u64, &nelem,
	    &dtype) == EINVAL);
	REQUIRE(zmetad_schema_field(zs, NULL, NULL, &u64, &nelem,
	    &dtype) == EINVAL);
	REQUIRE(zmetad_schema_field(zs, "op", NULL, NULL, &nelem,
	    &dtype) == EINVAL);

	/* zmetad_schema_compare: self-compare, and NULL rejection. */
	REQUIRE(zmetad_schema_compare(zs, zs, errbuf, sizeof (errbuf)) == 0);
	REQUIRE(zmetad_schema_compare(NULL, zs, errbuf,
	    sizeof (errbuf)) == EINVAL);
	REQUIRE(zmetad_schema_compare(zs, NULL, errbuf,
	    sizeof (errbuf)) == EINVAL);

	/*
	 * v2 IO fields: io_offset and io_bytes decode as uint64 on a
	 * synthetic WRITE record.
	 */
	rec = fnvlist_alloc();
	REQUIRE(rec != NULL);
	fnvlist_add_uint16(rec, "op", 8);
	fnvlist_add_uint64(rec, "io_offset", 1ULL << 40);
	fnvlist_add_uint64(rec, "io_bytes", 65536);

	rc = zmetad_schema_field(zs, "io_offset", rec, &u64, &nelem,
	    &dtype);
	REQUIRE(rc == 0);
	REQUIRE(u64 == (1ULL << 40));
	REQUIRE(dtype == DATA_TYPE_UINT64);

	rc = zmetad_schema_field(zs, "io_bytes", rec, &u64, &nelem,
	    &dtype);
	REQUIRE(rc == 0);
	REQUIRE(u64 == 65536);
	REQUIRE(dtype == DATA_TYPE_UINT64);

	/*
	 * op now carries WRITE (8), again into a dirtied uint64:
	 * the zero-extension contract must hold for every decode.
	 */
	u64 = 0xFFFFFFFFFFFFFFFFULL;
	rc = zmetad_schema_field(zs, "op", rec, &u64, &nelem, &dtype);
	REQUIRE(rc == 0);
	REQUIRE(u64 == 8);
	REQUIRE(dtype == DATA_TYPE_UINT16);

	fnvlist_free(rec);

	/*
	 * File-based round trip of the EMBEDDED blob: write it to a
	 * mkstemp file, load it, and require structural equality with
	 * the in-process embedded schema via zmetad_schema_compare.
	 */
	{
		char path[] = "/tmp/zmetad_schema_test_XXXXXX";
		zmetad_schema_t *file_zs;
		int fd = mkstemp(path);

		if (fd < 0) {
			fprintf(stderr, "FAIL: mkstemp: %s\n",
			    strerror(errno));
			g_fail = 1;
		} else {
			FILE *fp = fdopen(fd, "w");
			size_t len = strlen(ZMETAD_EMBEDDED_SCHEMA_JSON);

			if (fp == NULL) {
				fprintf(stderr, "FAIL: fdopen: %s\n",
				    strerror(errno));
				g_fail = 1;
				(void) close(fd);
			} else {
				if (fwrite(ZMETAD_EMBEDDED_SCHEMA_JSON, 1,
				    len, fp) != len) {
					fprintf(stderr,
					    "FAIL: blob write short\n");
					g_fail = 1;
				}
				if (fclose(fp) != 0) {
					fprintf(stderr, "FAIL: fclose: %s\n",
					    strerror(errno));
					g_fail = 1;
				}
				errbuf[0] = '\0';
				file_zs = zmetad_schema_load(path, errbuf);
				if (file_zs == NULL) {
					fprintf(stderr,
					    "FAIL: blob file load: %s\n",
					    errbuf);
					g_fail = 1;
				} else {
					rc = zmetad_schema_compare(zs,
					    file_zs, errbuf, sizeof (errbuf));
					if (rc != 0) {
						fprintf(stderr,
						    "FAIL: blob file != "
						    "embedded schema: %s\n",
						    errbuf);
						g_fail = 1;
					}
					REQUIRE(zmetad_schema_version(
					    file_zs) == 3);
					zmetad_schema_free(file_zs);
				}
			}
			(void) unlink(path);
		}
	}

	/* Malformed documents must be rejected with a message. */
	static const char *const bad_docs[] = {
		"",
		"not json at all",
		"{\"schema_version\": }",
		"{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"txg\": {\"type\": \"blob\"}}}}",
		"{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"txg\": {}}}}",
		"{\"record_format\": {\"fields\": {\"txg\": "
		    "{\"type\": \"uint64\"}}}}",
		"{\"schema_version\": 1,",
	};
	for (size_t i = 0; i < sizeof (bad_docs) / sizeof (bad_docs[0]);
	    i++) {
		char label[64];

		(void) snprintf(label, sizeof (label), "bad doc %zu", i);
		expect_load_fail(bad_docs[i], label);
	}

	/*
	 * K15: \u escape handling.  Truncated \u at EOF and mid-string
	 * must not read past the buffer; malformed hex is a bad escape;
	 * a valid \uXXXX decodes to '?'.
	 */
	expect_load_fail("{\"schema_version\": 1, \"record_format\": "
	    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}}}, \"x\": \"a\\u12",
	    "truncated \\u at EOF");
	expect_load_fail("{\"schema_version\": 1, \"record_format\": "
	    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}}}, \"x\": "
	    "\"a\\u12\" garbage",
	    "truncated \\u mid-string");
	expect_load_fail("{\"schema_version\": 1, \"record_format\": "
	    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}}}, \"x\": "
	    "\"a\\uZZZZ\"}",
	    "non-hex \\u escape");

	/* Valid \uXXXX inside a desc string is accepted as '?'. */
	{
		zmetad_schema_t *ok = expect_load_ok(
		    "{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"txg\": {\"type\": \"uint64\", "
		    "\"desc\": \"caf\\u00e9\"}}}}",
		    "valid \\uXXXX escape");

		if (ok != NULL) {
			REQUIRE(zmetad_schema_version(ok) == 1);
			REQUIRE(zmetad_schema_nfields(ok) == 1);
			zmetad_schema_free(ok);
		}
	}

	/*
	 * M19a: recursion depth bound.  Depth-31 nesting of unknown
	 * containers must parse (they are skipped structurally);
	 * depth-33 must be rejected as "document nested too deep".
	 * Nesting lives in an ignored key so only the skipper runs.
	 */
	{
		char deep[4096];
		int n;

		n = snprintf(deep, sizeof (deep),
		    "{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}}}, "
		    "\"ignored\": ");
		for (int i = 0; i < 31; i++) {
			n += snprintf(deep + n, sizeof (deep) - (size_t)n,
			    "[");
		}
		n += snprintf(deep + n, sizeof (deep) - (size_t)n, "1");
		for (int i = 0; i < 31; i++) {
			n += snprintf(deep + n, sizeof (deep) - (size_t)n,
			    "]");
		}
		(void) snprintf(deep + n, sizeof (deep) - (size_t)n, "}");
		{
			zmetad_schema_t *ok = expect_load_ok(deep,
			    "depth-31 nesting");

			if (ok != NULL)
				zmetad_schema_free(ok);
		}

		n = snprintf(deep, sizeof (deep),
		    "{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}}}, "
		    "\"ignored\": ");
		for (int i = 0; i < 33; i++) {
			n += snprintf(deep + n, sizeof (deep) - (size_t)n,
			    "[");
		}
		n += snprintf(deep + n, sizeof (deep) - (size_t)n, "1");
		for (int i = 0; i < 33; i++) {
			n += snprintf(deep + n, sizeof (deep) - (size_t)n,
			    "]");
		}
		(void) snprintf(deep + n, sizeof (deep) - (size_t)n, "}");
		expect_load_fail(deep, "depth-33 nesting");
	}

	/* M19b: schema_version that overflows uint64 is rejected. */
	expect_load_fail("{\"schema_version\": 18446744073709551616, "
	    "\"record_format\": {\"fields\": {\"txg\": "
	    "{\"type\": \"uint64\"}}}}",
	    "schema_version overflow");

	/* L17a: trailing garbage after the top-level document. */
	expect_load_fail("{\"schema_version\": 1, \"record_format\": "
	    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}}}} garbage",
	    "trailing garbage");

	/* L17c: duplicate field name in the fields object. */
	expect_load_fail("{\"schema_version\": 1, \"record_format\": "
	    "{\"fields\": {\"txg\": {\"type\": \"uint64\"}, "
	    "\"txg\": {\"type\": \"uint16\"}}}}",
	    "duplicate field name");

	/* L17c: duplicate schema_version key. */
	expect_load_fail("{\"schema_version\": 1, \"schema_version\": 2, "
	    "\"record_format\": {\"fields\": {\"txg\": "
	    "{\"type\": \"uint64\"}}}}",
	    "duplicate schema_version");

	/* L17b: trailing comma in the op enum array. */
	expect_load_fail("{\"schema_version\": 1, \"record_format\": "
	    "{\"fields\": {\"op\": {\"type\": \"uint16\", "
	    "\"enum\": [\"NONE\", \"CREATE\", ]}}}}",
	    "enum trailing comma");

	/* String bounds: 255 chars accepted, 256 rejected. */
	{
		char long_name[320];
		char doc[1024];

		memset(long_name, 'a', 255);
		long_name[255] = '\0';
		(void) snprintf(doc, sizeof (doc),
		    "{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"%s\": {\"type\": \"uint64\"}}}}",
		    long_name);
		{
			zmetad_schema_t *ok = expect_load_ok(doc,
			    "255-char field name");

			if (ok != NULL)
				zmetad_schema_free(ok);
		}

		memset(long_name, 'a', 256);
		long_name[256] = '\0';
		(void) snprintf(doc, sizeof (doc),
		    "{\"schema_version\": 1, \"record_format\": "
		    "{\"fields\": {\"%s\": {\"type\": \"uint64\"}}}}",
		    long_name);
		expect_load_fail(doc, "256-char field name");
	}

	/* L17d: file larger than the 1MB read cap is rejected as such. */
	{
		char path[] = "/tmp/zmetad_schema_test_XXXXXX";
		int fd = mkstemp(path);

		if (fd < 0) {
			fprintf(stderr, "FAIL: mkstemp oversize: %s\n",
			    strerror(errno));
			g_fail = 1;
		} else {
			FILE *fp = fdopen(fd, "w");
			static char filler[64 * 1024];

			if (fp == NULL) {
				fprintf(stderr, "FAIL: fdopen oversize\n");
				g_fail = 1;
				(void) close(fd);
			} else {
				memset(filler, ' ', sizeof (filler));
				(void) fwrite("{ \"pad\": \"", 1, 10, fp);
				for (int i = 0; i < 17; i++)
					(void) fwrite(filler, 1,
					    sizeof (filler), fp);
				(void) fwrite("\" }", 1, 3, fp);
				(void) fclose(fp);
				badbuf[0] = '\0';
				if (zmetad_schema_load(path, badbuf) !=
				    NULL) {
					fprintf(stderr,
					    "FAIL: oversize file accepted\n");
					g_fail = 1;
				} else if (strstr(badbuf, "file too large") ==
				    NULL) {
					fprintf(stderr,
					    "FAIL: oversize error is '%s', "
					    "want 'file too large'\n",
					    badbuf);
					g_fail = 1;
				}
			}
			(void) unlink(path);
		}
	}

	/* Missing file. */
	if (zmetad_schema_load("/nonexistent/schema.json", badbuf) != NULL) {
		fprintf(stderr, "FAIL: missing file load succeeded\n");
		g_fail = 1;
	}

	zmetad_schema_free(zs);

	if (g_fail) {
		fprintf(stderr, "zmetad_schema_test: FAILED\n");
		return (1);
	}

	printf("zmetad_schema_test: all checks passed\n");
	return (0);
}
