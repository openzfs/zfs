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
#include <limits.h>
#include <sys/stat.h>

#include <libnvpair.h>

#include "zmetad.h"
#include "zmetad_conf.h"
#include "zmetad_schema.h"
#include "zmetad_spool.h"
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

/*
 * Write "doc" to "<dir>/<name>" (a plain name, no slashes) under a
 * caller-provided mkdtemp directory and return a malloc'd path (caller
 * frees) or NULL on failure.  Conf tests write several files, so a
 * per-suite directory beats another mkstemp name.
 */
static char *
conf_write_file(const char *dir, const char *name, const char *doc)
{
	char *path;
	FILE *fp;
	size_t len = strlen(doc);
	size_t dlen = strlen(dir);

	path = malloc(dlen + 1 + strlen(name) + 1);
	if (path == NULL)
		return (NULL);
	(void) snprintf(path, dlen + 1 + strlen(name) + 1, "%s/%s", dir,
	    name);
	fp = fopen(path, "w");
	if (fp == NULL) {
		fprintf(stderr, "FAIL: fopen %s: %s\n", path,
		    strerror(errno));
		g_fail = 1;
		free(path);
		return (NULL);
	}
	if (fwrite(doc, 1, len, fp) != len) {
		fprintf(stderr, "FAIL: short write %s\n", path);
		g_fail = 1;
	}
	(void) fclose(fp);
	return (path);
}

/*
 * Initialize a zmetad_config_t to the same built-in defaults the
 * daemon's config_init() applies (minus the daemon-only one-shot
 * pointers, which this test does not exercise).
 */
static void
config_defaults_for_test(zmetad_config_t *cfg)
{
	memset(cfg, 0, sizeof (*cfg));
	cfg->poll_interval = ZMETAD_DEFAULT_POLL_INTERVAL;
	cfg->retention_days = ZMETAD_DEFAULT_RETENTION_DAYS;
	(void) strlcpy(cfg->db_path, ZMETAD_DEFAULT_DB_PATH,
	    sizeof (cfg->db_path));
	cfg->spool_max_bytes = 64 * 1024 * 1024;
	cfg->spool_fsync = B_FALSE;
	cfg->spool_enabled = B_FALSE;
}

/*
 * Spool tests (leaf 02): read the whole spool file into a malloc'd
 * buffer (caller frees).  Returns NULL (and flags failure) when the
 * file cannot be read.
 */
static char *
spool_read_file(const char *path)
{
	FILE *fp;
	long sz;
	char *buf;

	fp = fopen(path, "r");
	if (fp == NULL) {
		fprintf(stderr, "FAIL: spool fopen %s: %s\n", path,
		    strerror(errno));
		g_fail = 1;
		return (NULL);
	}
	if (fseek(fp, 0, SEEK_END) != 0 || (sz = ftell(fp)) < 0) {
		fprintf(stderr, "FAIL: spool size %s: %s\n", path,
		    strerror(errno));
		g_fail = 1;
		(void) fclose(fp);
		return (NULL);
	}
	(void) rewind(fp);
	buf = malloc((size_t)sz + 1);
	if (buf == NULL) {
		(void) fclose(fp);
		return (NULL);
	}
	if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
		fprintf(stderr, "FAIL: spool short read %s\n", path);
		g_fail = 1;
		free(buf);
		(void) fclose(fp);
		return (NULL);
	}
	buf[sz] = '\0';
	(void) fclose(fp);
	return (buf);
}

/*
 * Verify every line in the spool content parses back as one complete
 * JSON object (the file ends with a newline; nothing may follow it).
 * A partial line -- the rotation hazard the writer exists to
 * prevent -- shows up as trailing data without its newline.
 * Parsing uses nvlist_unpack of an nvlist_print_json round trip is
 * overkill here: a hand-rolled scanner that checks balanced quoting
 * (no unescaped quote inside), one top-level {...}, and the fixed
 * envelope prefix is enough to prove line-atomicity and shape.
 */
static void
spool_assert_ndjson(const char *content, const char *what)
{
	const char *p = content;

	while (*p != '\0') {
		const char *nl = strchr(p, '\n');
		boolean_t in_str = B_FALSE;
		const char *q;

		if (nl == NULL) {
			fprintf(stderr, "FAIL: %s: trailing data without "
			    "newline (partial line)\n", what);
			g_fail = 1;
			return;
		}
		/* Scanner: quotes toggle; backslash escapes one char. */
		for (q = p; q < nl; q++) {
			if (in_str && *q == '\\') {
				q++;
				continue;
			}
			if (*q == '"')
				in_str = !in_str;
		}
		if (in_str || *p != '{' || nl[-1] != '}') {
			fprintf(stderr, "FAIL: %s: line is not one "
			    "complete JSON object: %.*s\n", what,
			    (int)(nl - p), p);
			g_fail = 1;
			return;
		}
		p = nl + 1;
	}
}

static void
run_spool_tests(void)
{
	char dir[] = "/tmp/zmetad_spool_test_XXXXXX";
	char *path;
	zmetad_schema_t *zs;
	zmetad_spool_t *sp;
	nvlist_t *rec;
	char errbuf[256];

	if (mkdtemp(dir) == NULL) {
		fprintf(stderr, "FAIL: mkdtemp spool: %s\n", strerror(errno));
		g_fail = 1;
		return;
	}

	zs = zmetad_schema_load(NULL, errbuf);
	if (zs == NULL) {
		fprintf(stderr, "FAIL: spool: embedded schema load: %s\n",
		    errbuf);
		g_fail = 1;
		return;
	}

	/* Argument validation. */
	REQUIRE(zmetad_spool_open(&sp, NULL, 0, B_FALSE) == EINVAL);
	REQUIRE(zmetad_spool_open(NULL, "x", 0, B_FALSE) == EINVAL);
	REQUIRE(zmetad_spool_open(&sp, "", 0, B_FALSE) == EINVAL);

	/* Poisoned-by-default contract for an unknown handle. */
	REQUIRE(zmetad_spool_poisoned(NULL) == B_TRUE);
	zmetad_spool_resume(NULL);		/* no-op */
	zmetad_spool_close(NULL);		/* no-op */

	/*
	 * Event envelope: keys fixed and ordered, rec serialized
	 * schema-driven in schema order, JSON escaping round-trips.
	 * The dataset name carries a quote and a backslash; the
	 * escaped line is verified against an expected literal AND
	 * re-scanned by spool_assert_ndjson().
	 */
	{
		const char *dsname = "tank/we\"ird\\path";
		char expect[512];
		char *content;
		uint64_t ts = 1696312345;

		path = malloc(strlen(dir) + 32);
		REQUIRE(path != NULL);
		(void) snprintf(path, strlen(dir) + 32, "%s/ev.ndjson",
		    dir);

		REQUIRE(zmetad_spool_open(&sp, path, 0, B_FALSE) == 0);
		REQUIRE(zmetad_spool_poisoned(sp) == B_FALSE);
		zmetad_spool_set_schema(sp, zs);

		rec = fnvlist_alloc();
		REQUIRE(rec != NULL);
		fnvlist_add_uint64(rec, "txg", 15042);
		fnvlist_add_uint16(rec, "op", 6);	/* TRUNCATE */
		fnvlist_add_string(rec, "name", "a\\b\"c");

		REQUIRE(zmetad_spool_event(sp, dsname, rec, ts) == 0);
		fnvlist_free(rec);
		zmetad_spool_close(sp);

		content = spool_read_file(path);
		REQUIRE(content != NULL);
		(void) snprintf(expect, sizeof (expect),
		    "{\"zmetad\":1,\"type\":\"event\","
		    "\"dataset\":\"tank/we\\\"ird\\\\path\","
		    "\"ts\":%llu,"
		    "\"rec\":{\"txg\":15042,\"op\":\"TRUNCATE\","
		    "\"name\":\"a\\\\b\\\"c\"}}\n",
		    (unsigned long long)ts);
		if (content != NULL && strcmp(content, expect) != 0) {
			fprintf(stderr, "FAIL: spool envelope mismatch:\n"
			    " got: %s want: %s\n", content, expect);
			g_fail = 1;
		}
		if (content != NULL)
			spool_assert_ndjson(content, "envelope");
		free(content);

		/* Lazily created: the file exists only after a write. */
		{
			struct stat st;
			char *path2 = malloc(strlen(dir) + 32);

			REQUIRE(path2 != NULL);
			(void) snprintf(path2, strlen(dir) + 32,
			    "%s/lazy.ndjson", dir);
			REQUIRE(zmetad_spool_open(&sp, path2, 0,
			    B_FALSE) == 0);
			zmetad_spool_close(sp);
			REQUIRE(stat(path2, &st) == -1 &&
			    errno == ENOENT);
			free(path2);
		}

		/* Bad args on a live handle. */
		REQUIRE(zmetad_spool_open(&sp, path, 0, B_FALSE) == 0);
		zmetad_spool_set_schema(sp, zs);
		REQUIRE(zmetad_spool_event(sp, NULL, NULL, 1) == EINVAL);
		REQUIRE(zmetad_spool_marker(sp, NULL, "gap",
		    NULL) == EINVAL);
		zmetad_spool_close(sp);

		free(path);
	}

	/*
	 * Markers: epoch (old_guid/new_guid), gap lost=n, gap swap
	 * sentinel ((uint64_t)-1 renders as "swap":true), and the
	 * type validation.
	 */
	{
		char *content;

		path = malloc(strlen(dir) + 32);
		REQUIRE(path != NULL);
		(void) snprintf(path, strlen(dir) + 32, "%s/mk.ndjson",
		    dir);

		REQUIRE(zmetad_spool_open(&sp, path, 0, B_FALSE) == 0);
		REQUIRE(zmetad_spool_marker(sp, "tank/h", "epoch",
		    "old_guid", (uint64_t)111, "new_guid", (uint64_t)222,
		    NULL) == 0);
		REQUIRE(zmetad_spool_marker(sp, "tank/h", "gap",
		    "lost", (uint64_t)37, NULL) == 0);
		REQUIRE(zmetad_spool_marker(sp, "tank/h", "gap",
		    "lost", (uint64_t)-1, NULL) == 0);
		REQUIRE(zmetad_spool_marker(sp, "tank/h", "bogus",
		    NULL) == EINVAL);
		zmetad_spool_close(sp);

		content = spool_read_file(path);
		REQUIRE(content != NULL);
		if (content != NULL) {
			const char *l1 = strstr(content, "\"type\":\"epoch\"");
			const char *l2 = strstr(content, "\"lost\":37");
			const char *l3 = strstr(content, "\"swap\":true");

			REQUIRE(l1 != NULL);
			REQUIRE(strstr(l1 != NULL ? l1 : content,
			    "\"old_guid\":111") != NULL);
			REQUIRE(strstr(l1 != NULL ? l1 : content,
			    "\"new_guid\":222") != NULL);
			REQUIRE(l2 != NULL);
			REQUIRE(l3 != NULL);
			spool_assert_ndjson(content, "markers");
		}
		free(content);
		free(path);
	}

	/*
	 * Rotation at a tiny max_bytes: each line forces a roll, so
	 * the live file holds only the LAST line, the .1 generation
	 * holds every earlier line, and no line is ever split.
	 */
	{
		char dot1[strlen(dir) + 40];
		char *content;
		int i;

		path = malloc(strlen(dir) + 40);
		REQUIRE(path != NULL);
		(void) snprintf(path, strlen(dir) + 40, "%s/rot.ndjson",
		    dir);
		(void) snprintf(dot1, sizeof (dot1), "%s.1", path);

		/*
		 * Lines are ~55 bytes; a 60-byte threshold rolls on
		 * every line after the first.
		 */
		REQUIRE(zmetad_spool_open(&sp, path, 60, B_FALSE) == 0);
		zmetad_spool_set_schema(sp, zs);
		rec = fnvlist_alloc();
		REQUIRE(rec != NULL);
		fnvlist_add_uint64(rec, "txg", 1);
		for (i = 0; i < 6; i++) {
			REQUIRE(zmetad_spool_event(sp, "tank/r", rec,
			    (uint64_t)(1000 + i)) == 0);
		}
		fnvlist_free(rec);
		zmetad_spool_close(sp);

		{
			struct stat st;
			char *old;
			int nlines = 0;
			char *p;

			REQUIRE(stat(dot1, &st) == 0);
			REQUIRE(st.st_size > 0);

			/* Live file: exactly one (the last) line. */
			content = spool_read_file(path);
			REQUIRE(content != NULL);
			if (content != NULL) {
				for (p = content; (p = strchr(p, '\n'))
				    != NULL; p++)
					nlines++;
				REQUIRE(nlines == 1);
				REQUIRE(strstr(content,
				    "\"ts\":1005") != NULL);
				spool_assert_ndjson(content,
				    "rotation live");
			}
			free(content);

			/*
			 * .1: the previous generation holds the
			 * last line written before the final roll
			 * (each line was appended to a FRESH file,
			 * so one line per generation; the earlier
			 * lines were each renamed over by the
			 * next).  None may be partial.
			 */
			old = spool_read_file(dot1);
			REQUIRE(old != NULL);
			if (old != NULL) {
				nlines = 0;
				for (p = old; (p = strchr(p, '\n'))
				    != NULL; p++)
					nlines++;
				REQUIRE(nlines == 1);
				REQUIRE(strstr(old,
				    "\"ts\":1004") != NULL);
				spool_assert_ndjson(old,
				    "rotation .1");
			}
			free(old);
		}
		free(path);
	}

	/*
	 * Error path: a spool path in a read-only directory poisons
	 * the handle, events/marker refuse with EIO, resume clears
	 * it (recovery at the next collect cycle), and -- the
	 * poison contract -- the CALLER decides what happens to the
	 * collect; nothing here can fail it.
	 */
	{
		char rodir[strlen(dir) + 16];
		char ropath[strlen(dir) + 32];
		struct stat st;

		(void) snprintf(rodir, sizeof (rodir), "%s/ro", dir);
		REQUIRE(mkdir(rodir, 0555) == 0);
		(void) snprintf(ropath, sizeof (ropath), "%s/x.ndjson",
		    rodir);

		REQUIRE(zmetad_spool_open(&sp, ropath, 0,
		    B_FALSE) == 0);
		rec = fnvlist_alloc();
		REQUIRE(rec != NULL);
		fnvlist_add_uint64(rec, "txg", 1);
		/* First write fails (open in RO dir) and poisons. */
		REQUIRE(zmetad_spool_event(sp, "tank/e", rec, 1) == EIO);
		REQUIRE(zmetad_spool_poisoned(sp) == B_TRUE);
		/* Poisoned handles refuse everything with EIO. */
		REQUIRE(zmetad_spool_event(sp, "tank/e", rec, 2) == EIO);
		REQUIRE(zmetad_spool_marker(sp, "tank/e", "gap",
		    "lost", (uint64_t)1, NULL) == EIO);
		/* Nothing was created. */
		REQUIRE(stat(ropath, &st) == -1 && errno == ENOENT);
		fnvlist_free(rec);
		/* Resume: the next cycle's write is attempted again. */
		zmetad_spool_resume(sp);
		REQUIRE(zmetad_spool_poisoned(sp) == B_FALSE);
		REQUIRE(zmetad_spool_event(sp, "tank/e", NULL, 3) == EIO);
		REQUIRE(zmetad_spool_poisoned(sp) == B_TRUE);
		zmetad_spool_close(sp);
		(void) rmdir(rodir);
	}

	zmetad_schema_free(zs);
	if (g_fail) {
		fprintf(stderr, "spool test dir kept for inspection: %s\n",
		    dir);
		return;
	}
	(void) rmdir(dir);
}

/*
 * Conf loader: rc 0 = loaded, 1 = file absent, -1 = hard error.
 */
static void
run_conf_tests(void)
{
	char dir[] = "/tmp/zmetad_conf_test_XXXXXX";
	zmetad_config_t cfg;
	char err[256];

	if (mkdtemp(dir) == NULL) {
		fprintf(stderr, "FAIL: mkdtemp conf: %s\n", strerror(errno));
		g_fail = 1;
		return;
	}

	/* File absent -> 1, config untouched. */
	{
		char *absent = conf_write_file(dir, "absent", "");

		(void) unlink(absent);
		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, absent, err,
		    sizeof (err)) == 1);
		free(absent);
	}

	/* Minimal file: one key, everything else default. */
	{
		char *p = conf_write_file(dir, "minimal",
		    "poll_interval = 7\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == 0);
		REQUIRE(cfg.poll_interval == 7);
		REQUIRE(cfg.retention_days == ZMETAD_DEFAULT_RETENTION_DAYS);
		free(p);
	}

	/* All keys. */
	{
		char *p = conf_write_file(dir, "all",
		    "db_path = /tmp/zmd-all.db\n"
		    "schema_path = /tmp/schema.json\n"
		    "poll_interval = 600\n"
		    "retention_days = 365\n"
		    "spool_path = /var/spool/zmetad/events.ndjson\n"
		    "spool_max_bytes = 1048576\n"
		    "spool_fsync = on\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == 0);
		REQUIRE(strcmp(cfg.db_path, "/tmp/zmd-all.db") == 0);
		REQUIRE(strcmp(cfg.schema_path, "/tmp/schema.json") == 0);
		REQUIRE(cfg.poll_interval == 600);
		REQUIRE(cfg.retention_days == 365);
		REQUIRE(cfg.spool_enabled == B_TRUE);
		REQUIRE(strcmp(cfg.spool_path,
		    "/var/spool/zmetad/events.ndjson") == 0);
		REQUIRE(cfg.spool_max_bytes == 1048576);
		REQUIRE(cfg.spool_fsync == B_TRUE);
		free(p);
	}

	/* Quoted values, blank lines, whole-line + trailing comments. */
	{
		char *p = conf_write_file(dir, "quoted",
		    "# whole-line comment\n"
		    "\n"
		    "db_path = \"/tmp/zmd q.db\" # trailing comment\n"
		    "spool_fsync = \"off\"\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == 0);
		REQUIRE(strcmp(cfg.db_path, "/tmp/zmd q.db") == 0);
		REQUIRE(cfg.spool_fsync == B_FALSE);
		free(p);
	}

	/* Unknown key -> hard error naming it. */
	{
		char *p = conf_write_file(dir, "unknown",
		    "db_path = /tmp/x.db\n"
		    "bogus_key = 1\n");

		config_defaults_for_test(&cfg);
		err[0] = '\0';
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		REQUIRE(strstr(err, "bogus_key") != NULL);
		free(p);
	}

	/* Duplicate key -> hard error. */
	{
		char *p = conf_write_file(dir, "dup",
		    "poll_interval = 5\n"
		    "poll_interval = 6\n");

		config_defaults_for_test(&cfg);
		err[0] = '\0';
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		REQUIRE(strstr(err, "duplicate") != NULL);
		free(p);
	}

	/* Out-of-range poll_interval -> hard error. */
	{
		char *p = conf_write_file(dir, "range",
		    "poll_interval = 86401\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* Non-numeric poll_interval -> hard error. */
	{
		char *p = conf_write_file(dir, "bogus",
		    "poll_interval = bogus\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* Backslash outside quotes -> hard error, no escape grammar. */
	{
		char *p = conf_write_file(dir, "backslash",
		    "db_path = /tmp/x\\y.db\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* Truncating db_path -> hard error (value longer than PATH_MAX). */
	{
		char *p = conf_write_file(dir, "toolong", "");
		FILE *fp = fopen(p, "w");

		REQUIRE(fp != NULL);
		(void) fprintf(fp, "db_path = /tmp/");
		for (size_t i = 0; i < PATH_MAX; i++)
			(void) fputc('x', fp);
		(void) fprintf(fp, "\n");
		(void) fclose(fp);

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* spool_max_bytes out of range (below 1MB) -> hard error. */
	{
		char *p = conf_write_file(dir, "smallspool",
		    "spool_max_bytes = 1024\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* spool_fsync invalid token -> hard error. */
	{
		char *p = conf_write_file(dir, "badfsync",
		    "spool_fsync = maybe\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* Line over the 4KB cap -> hard error, not truncation. */
	{
		char *p = conf_write_file(dir, "longline", "");
		FILE *fp = fopen(p, "w");

		REQUIRE(fp != NULL);
		(void) fprintf(fp, "schema_path = /tmp/");
		for (int i = 0; i < 5000; i++)
			(void) fputc('y', fp);
		(void) fprintf(fp, "\n");
		(void) fclose(fp);

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == -1);
		free(p);
	}

	/* Empty spool_path leaves spooling disabled. */
	{
		char *p = conf_write_file(dir, "emptyspool",
		    "spool_path = \"\"\n");

		config_defaults_for_test(&cfg);
		REQUIRE(zmetad_conf_load(&cfg, p, err, sizeof (err)) == 0);
		REQUIRE(cfg.spool_enabled == B_FALSE);
		free(p);
	}

	(void) rmdir(dir);
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
	zs = NULL;

	/*
	 * NDJSON spool writer (leaf 02): loads its own embedded
	 * schema since the parser tests freed theirs.
	 */
	run_spool_tests();

	/* zmetad.conf parser (leaf 01). */
	run_conf_tests();

	if (g_fail) {
		fprintf(stderr, "zmetad_schema_test: FAILED\n");
		return (1);
	}

	printf("zmetad_schema_test: all checks passed\n");
	return (0);
}
