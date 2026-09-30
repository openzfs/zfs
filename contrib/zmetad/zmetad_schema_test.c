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

	zs = zmetad_schema_load(NULL, errbuf);
	if (zs == NULL) {
		fprintf(stderr, "FAIL: embedded schema load: %s\n", errbuf);
		return (1);
	}

	REQUIRE(zmetad_schema_version(zs) == 2);
	REQUIRE(zmetad_schema_nfields(zs) == 17);

	/*
	 * Wire negotiation: the embedded schema is v2, so wire 2 is
	 * accepted and wire 3 refused.  Wire 1 is accepted: schema versions
	 * only add fields and op values, so a v2 daemon decodes a v1
	 * wire record (datasets whose event log predates the version
	 * bump report wire=1 forever).  Wire 0 always means "kernel
	 * did not report a version".
	 */
	REQUIRE(zmetad_schema_check_version(zs, 2) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 0) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 1) == 0);
	REQUIRE(zmetad_schema_check_version(zs, 3) == EINVAL);

	/*
	 * The --check-schema drift check in zmetad.c does NOT reuse
	 * check_version: it requires file_version == embedded version
	 * exactly, so an older schema FILE is refused there (unlike an
	 * older wire version, which stays acceptable above).  That
	 * comparison is local to run_check_schema() and is not
	 * exercised by this binary; these assertions pin the embedded
	 * version it compares against.
	 */
	REQUIRE(zmetad_schema_version(zs) == 2);

	/*
	 * op must be declared uint16; the field lookup on a uint64-
	 * valued record member must fail with EINVAL (type mismatch).
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

	/* A uint16 schema field must decode via uint16. */
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

	/* Op enum names. */
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 0), "NONE") == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 2), "REMOVE") == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 7), "SETATTR") == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 8), "WRITE") == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 9), "READ") == 0);
	REQUIRE(strcmp(zmetad_schema_op_name(zs, 999), "UNKNOWN") == 0);

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
	 * op now carries WRITE (8). The uint16 decoder writes only the
	 * low 16 bits of the caller's storage, so zero it first.
	 */
	u64 = 0;
	rc = zmetad_schema_field(zs, "op", rec, &u64, &nelem, &dtype);
	REQUIRE(rc == 0);
	REQUIRE(u64 == 8);
	REQUIRE(dtype == DATA_TYPE_UINT16);

	fnvlist_free(rec);

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
		char path[128];

		(void) snprintf(path, sizeof (path),
		    "/tmp/zmetad_schema_test_bad%zu.json", i);
		FILE *fp = fopen(path, "w");
		if (fp == NULL) {
			fprintf(stderr, "FAIL: cannot write %s\n", path);
			g_fail = 1;
			continue;
		}
		(void) fwrite(bad_docs[i], 1, strlen(bad_docs[i]), fp);
		(void) fclose(fp);

		zmetad_schema_t *bad = zmetad_schema_load(path, badbuf);
		if (bad != NULL) {
			fprintf(stderr, "FAIL: bad doc %zu accepted\n", i);
			g_fail = 1;
			zmetad_schema_free(bad);
		} else if (badbuf[0] == '\0') {
			fprintf(stderr, "FAIL: bad doc %zu: empty errbuf\n",
			    i);
			g_fail = 1;
		}
		(void) unlink(path);
	}

	/* Missing file. */
	if (zmetad_schema_load("/nonexistent/schema.json", badbuf) != NULL) {
		fprintf(stderr, "FAIL: missing file load succeeded\n");
		g_fail = 1;
	}

	if (g_fail) {
		fprintf(stderr, "zmetad_schema_test: FAILED\n");
		return (1);
	}

	printf("zmetad_schema_test: all checks passed\n");
	return (0);
}
