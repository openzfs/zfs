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
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libzutil.h>

/* Substring from after the last slash, or the string itself if none */
const char *
zfs_basename(const char *path)
{
	const char *bn = strrchr(path, '/');
	return (bn ? bn + 1 : path);
}

/* Return index of last slash or -1 if none */
ssize_t
zfs_dirnamelen(const char *path)
{
	const char *end = strrchr(path, '/');
	return (end ? end - path : -1);
}

/*
 * Given a shorthand device name check if a file by that name exists in any
 * of the 'zpool_default_import_path' or ZPOOL_IMPORT_PATH directories.  If
 * one is found, store its fully qualified path in the 'path' buffer passed
 * by the caller and return 0, otherwise return an error.
 */
int
zfs_resolve_shortname(const char *name, char *path, size_t len)
{
	const char *env = getenv("ZPOOL_IMPORT_PATH");

	if (env) {
		for (;;) {
			env += strspn(env, ":");
			size_t dirlen = strcspn(env, ":");
			if (dirlen) {
				(void) snprintf(path, len, "%.*s/%s",
				    (int)dirlen, env, name);
				if (access(path, F_OK) == 0)
					return (0);

				env += dirlen;
			} else
				break;
		}
	} else {
		size_t count;
		const char *const *zpool_default_import_path =
		    zpool_default_search_paths(&count);

		for (size_t i = 0; i < count; ++i) {
			(void) snprintf(path, len, "%s/%s",
			    zpool_default_import_path[i], name);
			if (access(path, F_OK) == 0)
				return (0);
		}
	}

	return (errno = ENOENT);
}

/*
 * Given a shorthand device name look for a match against 'cmp_name'.  This
 * is done by checking all prefix expansions using either the default
 * 'zpool_default_import_paths' or the ZPOOL_IMPORT_PATH environment
 * variable.  Proper partition suffixes will be appended if this is a
 * whole disk.  When a match is found 0 is returned otherwise ENOENT.
 */
static int
zfs_strcmp_shortname(const char *name, const char *cmp_name, int wholedisk)
{
	int path_len, cmp_len, i = 0, error = ENOENT;
	char *dir, *env, *envdup = NULL, *tmp = NULL;
	char path_name[MAXPATHLEN];
	const char *const *zpool_default_import_path = NULL;
	size_t count;

	cmp_len = strlen(cmp_name);
	env = getenv("ZPOOL_IMPORT_PATH");

	if (env) {
		envdup = strdup(env);
		dir = strtok_r(envdup, ":", &tmp);
	} else {
		zpool_default_import_path = zpool_default_search_paths(&count);
		dir = (char *)zpool_default_import_path[i];
	}

	while (dir) {
		/* Trim trailing directory slashes from ZPOOL_IMPORT_PATH */
		if (env) {
			while (dir[strlen(dir)-1] == '/')
				dir[strlen(dir)-1] = '\0';
		}

		path_len = snprintf(path_name, MAXPATHLEN, "%s/%s", dir, name);
		if (wholedisk)
			path_len = zfs_append_partition(path_name, MAXPATHLEN);

		if ((path_len == cmp_len) && strcmp(path_name, cmp_name) == 0) {
			error = 0;
			break;
		}

		if (env) {
			dir = strtok_r(NULL, ":", &tmp);
		} else if (++i < count) {
			dir = (char *)zpool_default_import_path[i];
		} else {
			dir = NULL;
		}
	}

	if (env)
		free(envdup);

	return (error);
}

/*
 * Given either a shorthand or fully qualified path name look for a match
 * against 'cmp'.  The passed name will be expanded as needed for comparison
 * purposes and redundant slashes stripped to ensure an accurate match.
 */
int
zfs_strcmp_pathname(const char *name, const char *cmp, int wholedisk)
{
	int path_len, cmp_len;
	char path_name[MAXPATHLEN];
	char cmp_name[MAXPATHLEN];
	char *dir, *tmp = NULL;

	/* Strip redundant slashes if they exist due to ZPOOL_IMPORT_PATH */
	cmp_name[0] = '\0';
	(void) strlcpy(path_name, cmp, sizeof (path_name));
	for (dir = strtok_r(path_name, "/", &tmp);
	    dir != NULL;
	    dir = strtok_r(NULL, "/", &tmp)) {
		strlcat(cmp_name, "/", sizeof (cmp_name));
		strlcat(cmp_name, dir, sizeof (cmp_name));
	}

	if (name[0] != '/')
		return (zfs_strcmp_shortname(name, cmp_name, wholedisk));

	(void) strlcpy(path_name, name, MAXPATHLEN);
	path_len = strlen(path_name);
	cmp_len = strlen(cmp_name);

	if (wholedisk) {
		path_len = zfs_append_partition(path_name, MAXPATHLEN);
		if (path_len == -1)
			return (ENOMEM);
	}

	if ((path_len != cmp_len) || strcmp(path_name, cmp_name))
		return (ENOENT);

	return (0);
}
