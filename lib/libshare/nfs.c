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
 */

#include <stdio.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"

static boolean_t nfs_available(void);

static sa_fstype_t *nfs_fstype;

/*
 * nfs_exportfs_temp_fd refers to a temporary copy of the output
 * from exportfs -v.
 */
static int nfs_exportfs_temp_fd = -1;

typedef int (*nfs_shareopt_callback_t)(const char *opt, const char *value,
    void *cookie);

typedef int (*nfs_host_callback_t)(const char *sharepath, const char *host,
    const char *security, const char *access, void *cookie);

/*
 * Invokes the specified callback function for each Solaris share option
 * listed in the specified string.
 */
static int
foreach_nfs_shareopt(const char *shareopts,
    nfs_shareopt_callback_t callback, void *cookie)
{
	char *shareopts_dup, *opt, *cur, *value;
	int was_nul, rc;

	if (shareopts == NULL)
		return (SA_OK);

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

			rc = callback(opt, value, cookie);

			if (rc != SA_OK) {
				free(shareopts_dup);
				return (rc);
			}
		}

		opt = cur + 1;

		if (was_nul)
			break;
	}

	free(shareopts_dup);

	return (0);
}

typedef struct nfs_host_cookie_s {
	nfs_host_callback_t callback;
	const char *sharepath;
	void *cookie;
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
	int rc;
	const char *access;
	char *host_dup, *host, *next;
	nfs_host_cookie_t *udata = (nfs_host_cookie_t *)pcookie;

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
			next = strchr(host, ':');
			if (next != NULL) {
				*next = '\0';
				next++;
			}

			rc = udata->callback(udata->sharepath, host,
			    udata->security, access, udata->cookie);

			if (rc != SA_OK) {
				free(host_dup);

				return (rc);
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
foreach_nfs_host(sa_share_impl_t impl_share, nfs_host_callback_t callback,
    void *cookie)
{
	nfs_host_cookie_t udata;
	char *shareopts;

	udata.callback = callback;
	udata.sharepath = impl_share->sharepath;
	udata.cookie = cookie;
	udata.security = "sys";

	shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;

	return foreach_nfs_shareopt(shareopts, foreach_nfs_host_cb,
	    &udata);
}

/*
 * Converts a Solaris NFS host specification to its Linux equivalent.
 */
static int
get_linux_hostspec(const char *solaris_hostspec, char **plinux_hostspec)
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
		*plinux_hostspec = strdup(solaris_hostspec + 1);
	} else {
		*plinux_hostspec = strdup(solaris_hostspec);
	}

	if (*plinux_hostspec == NULL) {
		return (SA_NO_MEMORY);
	}

	return (SA_OK);
}

/*
 * Used internally by nfs_enable_share to enable sharing for a single host.
 */
static int
nfs_enable_share_one(const char *sharepath, const char *host,
    const char *security, const char *access, void *pcookie)
{
	int rc;
	char *linuxhost, *hostpath, *opts;
	const char *linux_opts = (const char *)pcookie;
	char *argv[6];

	/* exportfs -i -o sec=XX,rX,<opts> <host>:<sharepath> */

	rc = get_linux_hostspec(host, &linuxhost);

	if (rc < 0)
		exit(1);

	hostpath = malloc(strlen(linuxhost) + 1 + strlen(sharepath) + 1);

	if (hostpath == NULL) {
		free(linuxhost);

		exit(1);
	}

	sprintf(hostpath, "%s:%s", linuxhost, sharepath);

	free(linuxhost);

	if (linux_opts == NULL)
		linux_opts = "";

	opts = malloc(4 + strlen(security) + 4 + strlen(linux_opts) + 1);

	if (opts == NULL)
		exit(1);

	sprintf(opts, "sec=%s,%s,%s", security, access, linux_opts);

#ifdef DEBUG
	fprintf(stderr, "sharing %s with opts %s\n", hostpath, opts);
#endif

	argv[0] = "/usr/sbin/exportfs";
	argv[1] = "-i";
	argv[2] = "-o";
	argv[3] = opts;
	argv[4] = hostpath;
	argv[5] = NULL;

	rc = libzfs_run_process(argv[0], argv, 0);

	free(hostpath);
	free(opts);

	if (rc < 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
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
	int rc;

	assert(plinux_opts != NULL);

	*plinux_opts = NULL;

	/* default options for Solaris shares */
	(void) add_linux_shareopt(plinux_opts, "no_subtree_check", NULL);
	(void) add_linux_shareopt(plinux_opts, "no_root_squash", NULL);
	(void) add_linux_shareopt(plinux_opts, "mountpoint", NULL);

	rc = foreach_nfs_shareopt(shareopts, get_linux_shareopts_cb,
	    plinux_opts);

	if (rc != SA_OK) {
		free(*plinux_opts);
		*plinux_opts = NULL;
	}

	return (rc);
}

/*
 * Enables NFS sharing for the specified share.
 */
static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	char *shareopts, *linux_opts;
	int rc;

	if (!nfs_available()) {
		return (SA_SYSTEM_ERR);
	}

	shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;

	if (shareopts == NULL)
		return (SA_OK);

	rc = get_linux_shareopts(shareopts, &linux_opts);

	if (rc != SA_OK)
		return (rc);

	rc = foreach_nfs_host(impl_share, nfs_enable_share_one, linux_opts);

	free(linux_opts);

	return (rc);
}

/*
 * Used internally by nfs_disable_share to disable sharing for a single host.
 */
static int
nfs_disable_share_one(const char *sharepath, const char *host,
    const char *security, const char *access, void *cookie)
{
	int rc;
	char *linuxhost, *hostpath;
	char *argv[4];

	rc = get_linux_hostspec(host, &linuxhost);

	if (rc < 0)
		exit(1);

	hostpath = malloc(strlen(linuxhost) + 1 + strlen(sharepath) + 1);

	if (hostpath == NULL) {
		free(linuxhost);
		exit(1);
	}

	sprintf(hostpath, "%s:%s", linuxhost, sharepath);

	free(linuxhost);

#ifdef DEBUG
	fprintf(stderr, "unsharing %s\n", hostpath);
#endif

	argv[0] = "/usr/sbin/exportfs";
	argv[1] = "-u";
	argv[2] = hostpath;
	argv[3] = NULL;

	rc = libzfs_run_process(argv[0], argv, 0);

	free(hostpath);

	if (rc < 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}

/*
 * Disables NFS sharing for the specified share.
 */
static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	if (!nfs_available()) {
		/*
		 * The share can't possibly be active, so nothing
		 * needs to be done to disable it.
		 */
		return (SA_OK);
	}

	return (foreach_nfs_host(impl_share, nfs_disable_share_one, NULL));
}

/*
 * Checks whether the specified NFS share options are syntactically correct.
 */
static int
nfs_validate_shareopts(const char *shareopts)
{
	char *linux_opts;
	int rc;

	rc = get_linux_shareopts(shareopts, &linux_opts);

	if (rc != SA_OK)
		return (rc);

	free(linux_opts);

	return (SA_OK);
}

/*
 * Checks whether a share is currently active.
 */
static boolean_t
nfs_is_share_active(sa_share_impl_t impl_share)
{
	char line[512];
	char *tab, *cur;
	FILE *nfs_exportfs_temp_fp;

	if (!nfs_available())
		return (B_FALSE);

	nfs_exportfs_temp_fp = fdopen(dup(nfs_exportfs_temp_fd), "r");

	if (nfs_exportfs_temp_fp == NULL ||
	    fseek(nfs_exportfs_temp_fp, 0, SEEK_SET) < 0) {
		fclose(nfs_exportfs_temp_fp);
		return (B_FALSE);
	}

	while (fgets(line, sizeof (line), nfs_exportfs_temp_fp) != NULL) {
		/*
		 * exportfs uses separate lines for the share path
		 * and the export options when the share path is longer
		 * than a certain amount of characters; this ignores
		 * the option lines
		 */
		if (line[0] == '\t')
			continue;

		tab = strchr(line, '\t');

		if (tab != NULL) {
			*tab = '\0';
			cur = tab - 1;
		} else {
			/*
			 * there's no tab character, which means the
			 * NFS options are on a separate line; we just
			 * need to remove the new-line character
			 * at the end of the line
			 */
			cur = line + strlen(line) - 1;
		}

		/* remove trailing spaces and new-line characters */
		while (cur >= line && (*cur == ' ' || *cur == '\n'))
			*cur-- = '\0';

		if (strcmp(line, impl_share->sharepath) == 0) {
			fclose(nfs_exportfs_temp_fp);
			return (B_TRUE);
		}
	}

	fclose(nfs_exportfs_temp_fp);

	return (B_FALSE);
}

/*
 * Called to update a share's options. A share's options might be out of
 * date if the share was loaded from disk (i.e. /etc/dfs/sharetab) and the
 * "sharenfs" dataset property has changed in the meantime. This function
 * also takes care of re-enabling the share if necessary.
 */
static int
nfs_update_shareopts(sa_share_impl_t impl_share, const char *resource,
    const char *shareopts)
{
	char *shareopts_dup;
	boolean_t needs_reshare = B_FALSE;
	char *old_shareopts;

	FSINFO(impl_share, nfs_fstype)->active =
	    nfs_is_share_active(impl_share);

	old_shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;

	if (strcmp(shareopts, "on") == 0)
		shareopts = "rw";

	if (FSINFO(impl_share, nfs_fstype)->active && old_shareopts != NULL &&
	    strcmp(old_shareopts, shareopts) != 0) {
		needs_reshare = B_TRUE;
		nfs_disable_share(impl_share);
	}

	shareopts_dup = strdup(shareopts);

	if (shareopts_dup == NULL)
		return (SA_NO_MEMORY);

	if (old_shareopts != NULL)
		free(old_shareopts);

	FSINFO(impl_share, nfs_fstype)->shareopts = shareopts_dup;

	if (needs_reshare)
		nfs_enable_share(impl_share);

	return (SA_OK);
}

/*
 * Clears a share's NFS options. Used by libshare to
 * clean up shares that are about to be free()'d.
 */
static void
nfs_clear_shareopts(sa_share_impl_t impl_share)
{
	free(FSINFO(impl_share, nfs_fstype)->shareopts);
	FSINFO(impl_share, nfs_fstype)->shareopts = NULL;
}

static const sa_share_ops_t nfs_shareops = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,

	.validate_shareopts = nfs_validate_shareopts,
	.update_shareopts = nfs_update_shareopts,
	.clear_shareopts = nfs_clear_shareopts,
};

/*
 * nfs_check_exportfs() checks that the exportfs command runs
 * and also maintains a temporary copy of the output from
 * exportfs -v.
 * To update this temporary copy simply call this function again.
 *
 * TODO : Use /var/lib/nfs/etab instead of our private copy.
 *        But must implement locking to prevent concurrent access.
 *
 * TODO : The temporary file descriptor is never closed since
 *        there is no libshare_nfs_fini() function.
 */
static int
nfs_check_exportfs(void)
{
	pid_t pid;
	int rc, status;
	static char nfs_exportfs_tempfile[] = "/tmp/exportfs.XXXXXX";

	/*
	 * Close any existing temporary copies of output from exportfs.
	 * We have already called unlink() so file will be deleted.
	 */
	if (nfs_exportfs_temp_fd >= 0)
		close(nfs_exportfs_temp_fd);

	nfs_exportfs_temp_fd = mkstemp(nfs_exportfs_tempfile);

	if (nfs_exportfs_temp_fd < 0)
		return (SA_SYSTEM_ERR);

	unlink(nfs_exportfs_tempfile);

	fcntl(nfs_exportfs_temp_fd, F_SETFD, FD_CLOEXEC);

	pid = fork();

	if (pid < 0) {
		(void) close(nfs_exportfs_temp_fd);
		nfs_exportfs_temp_fd = -1;
		return (SA_SYSTEM_ERR);
	}

	if (pid > 0) {
		while ((rc = waitpid(pid, &status, 0)) <= 0 && errno == EINTR);

		if (rc <= 0) {
			(void) close(nfs_exportfs_temp_fd);
			nfs_exportfs_temp_fd = -1;
			return (SA_SYSTEM_ERR);
		}

		if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
			(void) close(nfs_exportfs_temp_fd);
			nfs_exportfs_temp_fd = -1;
			return (SA_CONFIG_ERR);
		}

		return (SA_OK);
	}

	/* child */

	/* exportfs -v */

	if (dup2(nfs_exportfs_temp_fd, STDOUT_FILENO) < 0)
		exit(1);

	rc = execlp("/usr/sbin/exportfs", "exportfs", "-v", NULL);

	if (rc < 0) {
		exit(1);
	}

	exit(0);
}

/*
 * Provides a convenient wrapper for determing nfs availability
 */
static boolean_t
nfs_available(void)
{
	if (nfs_exportfs_temp_fd == -1)
		(void) nfs_check_exportfs();

	return ((nfs_exportfs_temp_fd != -1) ? B_TRUE : B_FALSE);
}

/*
 * Initializes the NFS functionality of libshare.
 */
void
libshare_nfs_init(void)
{
	nfs_fstype = register_fstype("nfs", &nfs_shareops);
}
