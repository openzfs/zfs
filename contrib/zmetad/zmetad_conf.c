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
 * /etc/zmetad.conf parser: "key = value" lines, '#' comments, no
 * escape grammar (a backslash outside quotes is rejected rather than
 * silently inventing one).  Values may be double-quoted.  Unknown or
 * duplicate keys and out-of-range numbers are hard errors, so a typo
 * in operator configuration can never silently half-apply.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <limits.h>

#include <libzfs.h>

#include "zmetad.h"
#include "zmetad_conf.h"

/* A line longer than this is rejected, not truncated. */
#define	ZMETAD_CONF_MAX_LINE	4096

/*
 * Spool bounds (min/max/default) come from zmetad.h
 * (ZMETAD_SPOOL_MIN_BYTES / ZMETAD_SPOOL_MAX_BYTES /
 * ZMETAD_DEFAULT_SPOOL_BYTES style names) so the parser and the
 * daemon cannot drift apart.
 */

static void
conf_err(char *err, size_t errlen, const char *fmt, ...)
{
	va_list ap;

	if (err == NULL || errlen == 0)
		return;
	va_start(ap, fmt);
	(void) vsnprintf(err, errlen, fmt, ap);
	va_end(ap);
}

/*
 * Trim leading and trailing whitespace in place; returns a pointer
 * into "s" and shortens it with NUL.
 */
static char *
trim(char *s)
{
	char *end;

	while (*s == ' ' || *s == '\t')
		s++;
	end = s + strlen(s);
	while (end > s && (end[-1] == ' ' || end[-1] == '\t' ||
	    end[-1] == '\n' || end[-1] == '\r'))
		end--;
	*end = '\0';
	return (s);
}

/*
 * Parse a double-quoted value.  "p" points at the opening quote.
 * No escape grammar: a backslash inside quotes is a hard error too
 * (the leaf spec rejects backslashes outside quotes; inside quotes
 * an escape would need a grammar to be unambiguous, so it is
 * rejected for the same reason).
 */
static int
parse_quoted(const char *p, char *out, size_t outlen, char *err,
    size_t errlen, int lineno)
{
	const char *q;

	p++;				/* skip opening quote */
	q = strchr(p, '"');
	if (q == NULL) {
		conf_err(err, errlen,
		    "line %d: unterminated quoted value", lineno);
		return (-1);
	}
	if ((size_t)(q - p) >= outlen) {
		conf_err(err, errlen, "line %d: value too long", lineno);
		return (-1);
	}
	(void) memcpy(out, p, q - p);
	out[q - p] = '\0';
	return (0);
}

static int
parse_bool(const char *val, boolean_t *out, char *err, size_t errlen,
    int lineno)
{
	if (strcmp(val, "on") == 0 || strcmp(val, "true") == 0)
		*out = B_TRUE;
	else if (strcmp(val, "off") == 0 || strcmp(val, "false") == 0)
		*out = B_FALSE;
	else {
		conf_err(err, errlen,
		    "line %d: spool_fsync: expected on|off|true|false, "
		    "got '%s'", lineno, val);
		return (-1);
	}
	return (0);
}

/*
 * strtol with full validation: whole string must be a decimal
 * integer within [lo, hi].
 */
static int
parse_int(const char *val, long lo, long hi, long *out, const char *key,
    char *err, size_t errlen, int lineno)
{
	char *end;
	long n;

	errno = 0;
	n = strtol(val, &end, 10);
	if (end == val || *end != '\0') {
		conf_err(err, errlen, "line %d: %s: '%s' is not a number",
		    lineno, key, val);
		return (-1);
	}
	if (errno == ERANGE || n < lo || n > hi) {
		conf_err(err, errlen, "line %d: %s: value %ld out of "
		    "range %ld..%ld", lineno, key, n, lo, hi);
		return (-1);
	}
	*out = n;
	return (0);
}

static int
apply_key(zmetad_config_t *cfg, const char *key, char *val,
    boolean_t *seen_spool_path, char *err, size_t errlen, int lineno)
{
	long n;

	if (strcmp(key, "db_path") == 0) {
		if (strlcpy(cfg->db_path, val, sizeof (cfg->db_path)) >=
		    sizeof (cfg->db_path)) {
			conf_err(err, errlen, "line %d: db_path: value "
			    "too long (max %lu bytes)",
			    lineno,
			    (unsigned long)(sizeof (cfg->db_path) - 1));
			return (-1);
		}
		return (0);
	}
	if (strcmp(key, "schema_path") == 0) {
		if (strlcpy(cfg->schema_path, val,
		    sizeof (cfg->schema_path)) >=
		    sizeof (cfg->schema_path)) {
			conf_err(err, errlen, "line %d: schema_path: "
			    "value too long (max %lu bytes)", lineno,
			    (unsigned long)(sizeof (cfg->schema_path) - 1));
			return (-1);
		}
		return (0);
	}
	if (strcmp(key, "poll_interval") == 0) {
		if (parse_int(val, 1, 86400, &n, key, err, errlen,
		    lineno) != 0)
			return (-1);
		cfg->poll_interval = (int)n;
		return (0);
	}
	if (strcmp(key, "retention_days") == 0) {
		if (parse_int(val, 0, ZMETAD_MAX_RETENTION_DAYS, &n, key,
		    err, errlen, lineno) != 0)
			return (-1);
		cfg->retention_days = (int)n;
		return (0);
	}
	if (strcmp(key, "spool_path") == 0) {
		if (strlcpy(cfg->spool_path, val,
		    sizeof (cfg->spool_path)) >=
		    sizeof (cfg->spool_path)) {
			conf_err(err, errlen, "line %d: spool_path: "
			    "value too long (max %lu bytes)", lineno,
			    (unsigned long)(sizeof (cfg->spool_path) - 1));
			return (-1);
		}
		/*
		 * A non-empty spool_path enables spooling; an explicit
		 * empty value ("spool_path = \"\"") leaves it off.
		 */
		cfg->spool_enabled = (cfg->spool_path[0] != '\0');
		*seen_spool_path = B_TRUE;
		return (0);
	}
	if (strcmp(key, "spool_max_bytes") == 0) {
		unsigned long long v;

		if (parse_int(val, 0, INT_MAX, &n, key, err, errlen,
		    lineno) != 0)
			return (-1);
		v = (unsigned long long)n;
		if (v < ZMETAD_SPOOL_MIN_BYTES ||
		    v > ZMETAD_SPOOL_MAX_BYTES) {
			conf_err(err, errlen, "line %d: spool_max_bytes: "
			    "value out of range %llu..%llu", lineno,
			    ZMETAD_SPOOL_MIN_BYTES, ZMETAD_SPOOL_MAX_BYTES);
			return (-1);
		}
		cfg->spool_max_bytes = (size_t)v;
		return (0);
	}
	if (strcmp(key, "spool_fsync") == 0)
		return (parse_bool(val, &cfg->spool_fsync, err, errlen,
		    lineno));

	conf_err(err, errlen, "line %d: unknown key '%s'", lineno, key);
	return (-1);
}

int
zmetad_conf_load(zmetad_config_t *cfg, const char *path, char *err,
    size_t errlen)
{
	FILE *fp;
	char line[ZMETAD_CONF_MAX_LINE];
	int lineno = 0;
	/*
	 * Duplicate detection needs a bit set per known key; there
	 * are only seven, so a small array of flags indexed by key
	 * id keeps this dependency-free.
	 */
	enum {
		K_DB_PATH, K_SCHEMA_PATH, K_POLL_INTERVAL, K_RETENTION_DAYS,
		K_SPOOL_PATH, K_SPOOL_MAX_BYTES, K_SPOOL_FSYNC, K_COUNT
	};
	boolean_t seen[K_COUNT] = { B_FALSE, B_FALSE, B_FALSE, B_FALSE,
	    B_FALSE, B_FALSE, B_FALSE };
	boolean_t seen_spool_path = B_FALSE;

	if (err != NULL && errlen > 0)
		err[0] = '\0';

	fp = fopen(path, "r");
	if (fp == NULL) {
		if (errno == ENOENT)
			return (1);
		conf_err(err, errlen, "cannot open %s: %s", path,
		    strerror(errno));
		return (-1);
	}

	while (fgets(line, sizeof (line), fp) != NULL) {
		char *p = line;
		char *eq;
		char *key;
		char *val;
		size_t len = strlen(line);
		int kid;

		lineno++;

		/* No NUL within the buffer means the line ran over. */
		if (len > 0 && line[len - 1] != '\n' && !feof(fp)) {
			conf_err(err, errlen, "line %d: line too long "
			    "(max %d bytes)", lineno, ZMETAD_CONF_MAX_LINE);
			(void) fclose(fp);
			return (-1);
		}

		/* '#' starts a comment; support trailing comments. */
		for (char *c = line; *c != '\0'; c++) {
			if (*c == '#') {
				*c = '\0';
				break;
			}
			/*
			 * A backslash may start a line continuation in
			 * some grammars; we do not have one, so it is
			 * rejected with a clear error rather than
			 * silently eaten or copied verbatim.
			 */
			if (*c == '\\' && c[1] != '\0') {
				conf_err(err, errlen, "line %d: "
				    "backslash escapes are not "
				    "supported in zmetad.conf", lineno);
				(void) fclose(fp);
				return (-1);
			}
		}

		p = trim(line);
		if (*p == '\0')
			continue;

		eq = strchr(p, '=');
		if (eq == NULL) {
			conf_err(err, errlen, "line %d: expected "
			    "'key = value', got '%s'", lineno, p);
			(void) fclose(fp);
			return (-1);
		}
		*eq = '\0';
		key = trim(p);
		val = trim(eq + 1);
		if (*key == '\0') {
			conf_err(err, errlen, "line %d: empty key", lineno);
			(void) fclose(fp);
			return (-1);
		}
		if (*val == '\0') {
			conf_err(err, errlen, "line %d: key '%s' has an "
			    "empty value", lineno, key);
			(void) fclose(fp);
			return (-1);
		}

		/*
		 * Double-quoted value: strip the quotes.  Anything
		 * after the closing quote (besides whitespace,
		 * already trimmed... which cannot be, since trim ran
		 * before quote detection) is rejected by
		 * parse_quoted leaving the rest unchecked; be strict:
		 * require the closing quote to be the last character.
		 */
		if (*val == '"') {
			size_t vlen = strlen(val);
			char quoted[ZMETAD_CONF_MAX_LINE];
			const char *close = strchr(val + 1, '"');

			if (close == NULL ||
			    close != val + vlen - 1) {
				conf_err(err, errlen, "line %d: "
				    "malformed quoted value", lineno);
				(void) fclose(fp);
				return (-1);
			}
			if (parse_quoted(val, quoted, sizeof (quoted),
			    err, errlen, lineno) != 0) {
				(void) fclose(fp);
				return (-1);
			}
			(void) strlcpy(val, quoted, ZMETAD_CONF_MAX_LINE);
		}

		/* Map the key name to its duplicate-detection slot. */
		if (strcmp(key, "db_path") == 0)
			kid = K_DB_PATH;
		else if (strcmp(key, "schema_path") == 0)
			kid = K_SCHEMA_PATH;
		else if (strcmp(key, "poll_interval") == 0)
			kid = K_POLL_INTERVAL;
		else if (strcmp(key, "retention_days") == 0)
			kid = K_RETENTION_DAYS;
		else if (strcmp(key, "spool_path") == 0)
			kid = K_SPOOL_PATH;
		else if (strcmp(key, "spool_max_bytes") == 0)
			kid = K_SPOOL_MAX_BYTES;
		else if (strcmp(key, "spool_fsync") == 0)
			kid = K_SPOOL_FSYNC;
		else
			kid = -1;

		/*
		 * apply_key reports unknown keys itself (so the error
		 * names the key); here only duplicates are rejected.
		 */
		if (kid >= 0 && seen[kid]) {
			conf_err(err, errlen, "line %d: duplicate key "
			    "'%s'", lineno, key);
			(void) fclose(fp);
			return (-1);
		}
		if (kid >= 0)
			seen[kid] = B_TRUE;

		if (apply_key(cfg, key, val, &seen_spool_path, err,
		    errlen, lineno) != 0) {
			(void) fclose(fp);
			return (-1);
		}
	}

	if (ferror(fp)) {
		conf_err(err, errlen, "error reading %s: %s", path,
		    strerror(errno));
		(void) fclose(fp);
		return (-1);
	}
	(void) fclose(fp);

	/*
	 * spool_max_bytes / spool_fsync may appear without
	 * spool_path: they are remembered and take effect when
	 * spooling is enabled (by spool_path here or by a future
	 * CLI flag).  spool_enabled is only ever set by spool_path
	 * in this file.
	 */
	(void) seen_spool_path;

	if (cfg->spool_max_bytes == 0)
		cfg->spool_max_bytes = ZMETAD_SPOOL_DEFAULT_BYTES;

	return (0);
}
