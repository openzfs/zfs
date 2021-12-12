/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Gunnar Beutner
 * Copyright (c) 2012 Cyril Plisko. All rights reserved.
 * Copyright (c) 2019, 2020 by Delphix. All rights reserved.
 */

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "nfs.h"

#define	ZFS_EXPORTS_DIR		"/etc/exports.d"
#define	ZFS_EXPORTS_FILE	ZFS_EXPORTS_DIR"/zfs.exports"
#define	ZFS_EXPORTS_LOCK	ZFS_EXPORTS_FILE".lock"

static sa_fstype_t *nfs_fstype;

typedef int (*nfs_shareopt_callback_t)(const char *opt, const char *value,
    void *cookie);

typedef int (*nfs_host_callback_t)(FILE *tmpfile, const char *sharepath,
    const char *host, const char *security, const char *access, void *cookie);

/*
 * Invokes the specified callback function for each Solaris share option
 * listed in the specified string.
 */
static int
foreach_nfs_shareopt(const char *shareopts,
    nfs_shareopt_callback_t callback, void *cookie)
{
	char *shareopts_dup, *opt, *cur, *value;
	int was_nul, error;

	if (shareopts == NULL)
		return (SA_OK);

	if (strcmp(shareopts, "on") == 0)
		shareopts = "rw,crossmnt";

	shareopts_dup = strdup(shareopts);


	if (shareopts_dup == NULL)
		return (SA_NO_MEMORY);

	opt = shareopts_dup;
	was_nul = 0;

	while (1) {
		cur = opt;

		while (*cur != ',' && *cur != '\0')
			cur++;

		if (*cur == '\0')
			was_nul = 1;

		*cur = '\0';

		if (cur > opt) {
			value = strchr(opt, '=');

			if (value != NULL) {
				*value = '\0';
				value++;
			}

			error = callback(opt, value, cookie);

			if (error != SA_OK) {
				free(shareopts_dup);
				return (error);
			}
		}

		opt = cur + 1;

		if (was_nul)
			break;
	}

	free(shareopts_dup);

	return (SA_OK);
}

typedef struct nfs_host_cookie_s {
	nfs_host_callback_t callback;
	const char *sharepath;
	void *cookie;
	FILE *tmpfile;
	const char *security;
} nfs_host_cookie_t;

/*
 * Helper function for foreach_nfs_host. This function checks whether the
 * current share option is a host specification and invokes a callback
 * function with information about the host.
 */
static int
foreach_nfs_host_cb(const char *opt, const char *value, void *pcookie)
{
	int error;
	const char *access;
	char *host_dup, *host, *next, *v6Literal;
	nfs_host_cookie_t *udata = (nfs_host_cookie_t *)pcookie;
	int cidr_len;

#ifdef DEBUG
	fprintf(stderr, "foreach_nfs_host_cb: key=%s, value=%s\n", opt, value);
#endif

	if (strcmp(opt, "sec") == 0)
		udata->security = value;

	if (strcmp(opt, "rw") == 0 || strcmp(opt, "ro") == 0) {
		if (value == NULL)
			value = "*";

		access = opt;

		host_dup = strdup(value);

		if (host_dup == NULL)
			return (SA_NO_MEMORY);

		host = host_dup;

		do {
			if (*host == '[') {
				host++;
				v6Literal = strchr(host, ']');
				if (v6Literal == NULL) {
					free(host_dup);
					return (SA_SYNTAX_ERR);
				}
				if (v6Literal[1] == '\0') {
					*v6Literal = '\0';
					next = NULL;
				} else if (v6Literal[1] == '/') {
					next = strchr(v6Literal + 2, ':');
					if (next == NULL) {
						cidr_len =
						    strlen(v6Literal + 1);
						memmove(v6Literal,
						    v6Literal + 1,
						    cidr_len);
						v6Literal[cidr_len] = '\0';
					} else {
						cidr_len = next - v6Literal - 1;
						memmove(v6Literal,
						    v6Literal + 1,
						    cidr_len);
						v6Literal[cidr_len] = '\0';
						next++;
					}
				} else if (v6Literal[1] == ':') {
					*v6Literal = '\0';
					next = v6Literal + 2;
				} else {
					free(host_dup);
					return (SA_SYNTAX_ERR);
				}
			} else {
				next = strchr(host, ':');
				if (next != NULL) {
					*next = '\0';
					next++;
				}
			}

			error = udata->callback(udata->tmpfile,
			    udata->sharepath, host, udata->security,
			    access, udata->cookie);

			if (error != SA_OK) {
				free(host_dup);

				return (error);
			}

			host = next;
		} while (host != NULL);

		free(host_dup);
	}

	return (SA_OK);
}

/*
 * Invokes a callback function for all NFS hosts that are set for a share.
 */
static int
foreach_nfs_host(sa_share_impl_t impl_share, FILE *tmpfile,
    nfs_host_callback_t callback, void *cookie)
{
	nfs_host_cookie_t udata;
	char *shareopts;

	udata.callback = callback;
	udata.sharepath = impl_share->sa_mountpoint;
	udata.cookie = cookie;
	udata.tmpfile = tmpfile;
	udata.security = "sys";

	shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;

	return (foreach_nfs_shareopt(shareopts, foreach_nfs_host_cb,
	    &udata));
}

/*
 * Converts a Solaris NFS host specification to its Linux equivalent.
 */
static const char *
get_linux_hostspec(const char *solaris_hostspec)
{
	/*
	 * For now we just support CIDR masks (e.g. @192.168.0.0/16) and host
	 * wildcards (e.g. *.example.org).
	 */
	if (solaris_hostspec[0] == '@') {
		/*
		 * Solaris host specifier, e.g. @192.168.0.0/16; we just need
		 * to skip the @ in this case
		 */
		return (solaris_hostspec + 1);
	} else {
		return (solaris_hostspec);
	}
}

/*
 * Adds a Linux share option to an array of NFS options.
 */
static int
add_linux_shareopt(char **plinux_opts, const char *key, const char *value)
{
	size_t len = 0;
	char *new_linux_opts;

	if (*plinux_opts != NULL)
		len = strlen(*plinux_opts);

	new_linux_opts = realloc(*plinux_opts, len + 1 + strlen(key) +
	    (value ? 1 + strlen(value) : 0) + 1);

	if (new_linux_opts == NULL)
		return (SA_NO_MEMORY);

	new_linux_opts[len] = '\0';

	if (len > 0)
		strcat(new_linux_opts, ",");

	strcat(new_linux_opts, key);

	if (value != NULL) {
		strcat(new_linux_opts, "=");
		strcat(new_linux_opts, value);
	}

	*plinux_opts = new_linux_opts;

	return (SA_OK);
}

/*
 * Validates and converts a single Solaris share option to its Linux
 * equivalent.
 */
static int
get_linux_shareopts_cb(const char *key, const char *value, void *cookie)
{
	char **plinux_opts = (char **)cookie;

	/* host-specific options, these are taken care of elsewhere */
	if (strcmp(key, "ro") == 0 || strcmp(key, "rw") == 0 ||
	    strcmp(key, "sec") == 0)
		return (SA_OK);

	if (strcmp(key, "anon") == 0)
		key = "anonuid";

	if (strcmp(key, "root_mapping") == 0) {
		(void) add_linux_shareopt(plinux_opts, "root_squash", NULL);
		key = "anonuid";
	}

	if (strcmp(key, "nosub") == 0)
		key = "subtree_check";

	if (strcmp(key, "insecure") != 0 && strcmp(key, "secure") != 0 &&
	    strcmp(key, "async") != 0 && strcmp(key, "sync") != 0 &&
	    strcmp(key, "no_wdelay") != 0 && strcmp(key, "wdelay") != 0 &&
	    strcmp(key, "nohide") != 0 && strcmp(key, "hide") != 0 &&
	    strcmp(key, "crossmnt") != 0 &&
	    strcmp(key, "no_subtree_check") != 0 &&
	    strcmp(key, "subtree_check") != 0 &&
	    strcmp(key, "insecure_locks") != 0 &&
	    strcmp(key, "secure_locks") != 0 &&
	    strcmp(key, "no_auth_nlm") != 0 && strcmp(key, "auth_nlm") != 0 &&
	    strcmp(key, "no_acl") != 0 && strcmp(key, "mountpoint") != 0 &&
	    strcmp(key, "mp") != 0 && strcmp(key, "fsuid") != 0 &&
	    strcmp(key, "refer") != 0 && strcmp(key, "replicas") != 0 &&
	    strcmp(key, "root_squash") != 0 &&
	    strcmp(key, "no_root_squash") != 0 &&
	    strcmp(key, "all_squash") != 0 &&
	    strcmp(key, "no_all_squash") != 0 && strcmp(key, "fsid") != 0 &&
	    strcmp(key, "anonuid") != 0 && strcmp(key, "anongid") != 0) {
		return (SA_SYNTAX_ERR);
	}

	(void) add_linux_shareopt(plinux_opts, key, value);

	return (SA_OK);
}

/*
 * Takes a string containing Solaris share options (e.g. "sync,no_acl") and
 * converts them to a NULL-terminated array of Linux NFS options.
 */
static int
get_linux_shareopts(const char *shareopts, char **plinux_opts)
{
	int error;

	assert(plinux_opts != NULL);

	*plinux_opts = NULL;

	/* no_subtree_check - Default as of nfs-utils v1.1.0 */
	(void) add_linux_shareopt(plinux_opts, "no_subtree_check", NULL);

	/* mountpoint - Restrict exports to ZFS mountpoints */
	(void) add_linux_shareopt(plinux_opts, "mountpoint", NULL);

	error = foreach_nfs_shareopt(shareopts, get_linux_shareopts_cb,
	    plinux_opts);

	if (error != SA_OK) {
		free(*plinux_opts);
		*plinux_opts = NULL;
	}

	return (error);
}

/*
 * This function populates an entry into /etc/exports.d/zfs.exports.
 * This file is consumed by the linux nfs server so that zfs shares are
 * automatically exported upon boot or whenever the nfs server restarts.
 */
static int
nfs_add_entry(FILE *tmpfile, const char *sharepath,
    const char *host, const char *security, const char *access_opts,
    void *pcookie)
{
	const char *linux_opts = (const char *)pcookie;

	if (linux_opts == NULL)
		linux_opts = "";

	if (fprintf(tmpfile, "%s %s(sec=%s,%s,%s)\n", sharepath,
	    get_linux_hostspec(host), security, access_opts,
	    linux_opts) < 0) {
		fprintf(stderr, "failed to write to temporary file\n");
		return (SA_SYSTEM_ERR);
	}

	return (SA_OK);
}

/*
 * Enables NFS sharing for the specified share.
 */
static int
nfs_enable_share_impl(sa_share_impl_t impl_share, FILE *tmpfile)
{
	char *shareopts, *linux_opts;
	int error;

	shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;
	error = get_linux_shareopts(shareopts, &linux_opts);
	if (error != SA_OK)
		return (error);

	error = foreach_nfs_host(impl_share, tmpfile, nfs_add_entry,
	    linux_opts);
	free(linux_opts);
	return (error);
}

static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, ZFS_EXPORTS_DIR, impl_share,
	    nfs_enable_share_impl));
}

/*
 * Disables NFS sharing for the specified share.
 */
static int
nfs_disable_share_impl(sa_share_impl_t impl_share, FILE *tmpfile)
{
	(void) impl_share, (void) tmpfile;
	return (SA_OK);
}

static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	return (nfs_toggle_share(
	    ZFS_EXPORTS_LOCK, ZFS_EXPORTS_FILE, ZFS_EXPORTS_DIR, impl_share,
	    nfs_disable_share_impl));
}

static boolean_t
nfs_is_shared(sa_share_impl_t impl_share)
{
	return (nfs_is_shared_impl(ZFS_EXPORTS_FILE, impl_share));
}

/*
 * Checks whether the specified NFS share options are syntactically correct.
 */
static int
nfs_validate_shareopts(const char *shareopts)
{
	char *linux_opts;
	int error;

	error = get_linux_shareopts(shareopts, &linux_opts);

	if (error != SA_OK)
		return (error);

	free(linux_opts);
	return (SA_OK);
}

static int
nfs_update_shareopts(sa_share_impl_t impl_share, const char *shareopts)
{
	FSINFO(impl_share, nfs_fstype)->shareopts = (char *)shareopts;
	return (SA_OK);
}

/*
 * Clears a share's NFS options. Used by libshare to
 * clean up shares that are about to be free()'d.
 */
static void
nfs_clear_shareopts(sa_share_impl_t impl_share)
{
	FSINFO(impl_share, nfs_fstype)->shareopts = NULL;
}

static int
nfs_commit_shares(void)
{
	char *argv[] = {
	    "/usr/sbin/exportfs",
	    "-ra",
	    NULL
	};

	return (libzfs_run_process(argv[0], argv, 0));
}

static const sa_share_ops_t nfs_shareops = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,
	.is_shared = nfs_is_shared,

	.validate_shareopts = nfs_validate_shareopts,
	.update_shareopts = nfs_update_shareopts,
	.clear_shareopts = nfs_clear_shareopts,
	.commit_shares = nfs_commit_shares,
};

/*
 * Initializes the NFS functionality of libshare.
 */
void
libshare_nfs_init(void)
{
	nfs_fstype = register_fstype("nfs", &nfs_shareops);
}
