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
 * Copyright (c) 2016 Jorgen Lundman <lundman@lundman.net>
 */

#include <stdio.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"

static sa_fstype_t *nfs_fstype;

#define EXPORTFILE "/etc/exports"

typedef int (*nfs_shareopt_callback_t)(const char *opt, const char *value,
    void *cookie);

struct nfs_host_cookie_s;

typedef int (*nfs_host_callback_t)(struct nfs_host_cookie_s *, const char *host, const char *access);

typedef struct nfs_host_cookie_s {
	nfs_host_callback_t callback;
	const char *sharepath;
	void *cookie;
	const char *security;
	FILE *file;
	const char *exportname;
} nfs_host_cookie_t;

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

	if (strcmp(opt, "rw") == 0 || strcmp(opt, "ro") == 0 ||
		strcmp(opt, "root") == 0) {
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

			rc = udata->callback(udata, host,
			    access);

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
				 void *cookie, FILE *file, char *exportname)
{
	nfs_host_cookie_t udata;
	char *shareopts;

	udata.callback = callback;
	udata.sharepath = impl_share->sharepath;
	udata.cookie = cookie;
	udata.security = "sys";
	udata.file = file;
	udata.exportname = exportname;

	shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;

	return foreach_nfs_shareopt(shareopts, foreach_nfs_host_cb,
	    &udata);
}

/*
 * Converts a Solaris NFS host specification to its OSX equivalent.
 */
static int
get_osx_hostspec(const char *solaris_hostspec, char **posx_hostspec)
{
	/*
	 * For now we just support CIDR masks (e.g. @192.168.0.0/16) and host
	 * wildcards (e.g. *.example.org).
	 */
	if (solaris_hostspec[0] == '@') {
		/*
		 * Solaris host specifier, e.g. @192.168.0.0/16;
		 * We need to convert this to -network 192.168.0.0 -mask 255.255.0.0
		 */
		char tmpbuf[8+1+15+1+5+1+15+1]; // "-network" + IP + "-mask" + IP (+ spaces + nil)

		solaris_hostspec++;

		uint32_t mask = 0;
		char *slash;
		const char *dot;
		uint32_t bits = 0;

		// Check if they use "/" notation
		slash = strchr(solaris_hostspec, '/');
		if (slash) {
			bits = atoi(&slash[1]);
			mask = (bits >= 32) ? 0xFFFFFFFF : ~(0xFFFFFFFF >> bits);
			*slash = 0;
		} else {
			// No slash, check how many dots we got.
			bits = 8;
			dot = solaris_hostspec;
			while ((dot = strchr(dot, '.'))) {
				dot++;
				bits += 8;
			}
			mask = ~(0xFFFFFFFF >> bits);
		}

		snprintf(tmpbuf, sizeof(tmpbuf),
				 "-network %s -mask %u.%u.%u.%u",
				 solaris_hostspec,
				 mask>>24, mask>>16&0xff,mask>>8&0xff,mask&0xff);
		*posx_hostspec = strdup(tmpbuf);

	} else {
		*posx_hostspec = strdup(solaris_hostspec);
	}

	if (*posx_hostspec == NULL) {
		return (SA_NO_MEMORY);
	}

	return (SA_OK);
}


/*
 * Adds a OSX share option to an array of NFS options.
 */
static int
add_osx_shareopt(char **posx_opts, const char *key, const char *value)
{
	size_t len = 0;
	char *new_osx_opts;

	if (*posx_opts != NULL)
		len = strlen(*posx_opts);

	new_osx_opts = realloc(*posx_opts, len + 1 + strlen(key) +
	    (value ? 1 + strlen(value) : 0) + 1);

	if (new_osx_opts == NULL)
		return (SA_NO_MEMORY);

	new_osx_opts[len] = '\0';

	if (len > 0)
		strcat(new_osx_opts, ",");

	strcat(new_osx_opts, key);

	if (value != NULL) {
		strcat(new_osx_opts, "=");
		strcat(new_osx_opts, value);
	}

	*posx_opts = new_osx_opts;

	return (SA_OK);
}

/*
 * Validates and converts a single Solaris share option to its OS X
 * equivalent.
 * Multiple lines might be required, for example:
 * IllumOS: sharenfs= rw=192.168,root=@192.168.1,ro=host1:host2 DATASET
 * OS X: /DATASET -network 192.168.0.0 -mask 255.255.0.0
 *     : /DATASET -maproot=root -network 192.168.1.0 -mask 255.255.255.0
 *     : /DATASET -ro host1 host2
 */
static int
get_osx_shareopts_cb(const char *key, const char *value, void *cookie)
{
	char **posx_opts = (char **)cookie;

#ifdef DEBUG
	fprintf(stderr, "ZFS: share key '%s' value '%s'\r\n", key, value);
#endif

	/* host-specific options, these are taken care of elsewhere */
	if (strcmp(key, "ro") == 0 || strcmp(key, "rw") == 0 ||
	    strcmp(key, "root") == 0 || strcmp(key, "sec") == 0)
		return (SA_OK);

	if (strcmp(key, "maproot") != 0 && strcmp(key, "mapall") != 0 &&
	    strcmp(key, "alldirs") != 0 &&
	    strcmp(key, "32bitclients") != 0 &&
	    strcmp(key, "manglednames") != 0 &&
	    strcmp(key, "network") != 0 && strcmp(key, "mask") != 0 &&
	    strcmp(key, "offline") != 0 &&
	    strcmp(key, "fspath") != 0 && strcmp(key, "fsuuid") != 0) {
		return (SA_SYNTAX_ERR);
	}

	(void) add_osx_shareopt(posx_opts, key, value);

	return (SA_OK);
}

/*
 * Takes a string containing Solaris share options (e.g. "sync,no_acl") and
 * converts them to a NULL-terminated array of OSX NFS options.
 */
static int
get_osx_shareopts(const char *shareopts, char **posx_opts)
{
	int rc;

	assert(posx_opts != NULL);

	*posx_opts = NULL;

	/* default options for Solaris shares */
	rc = foreach_nfs_shareopt(shareopts, get_osx_shareopts_cb,
	    posx_opts);

	if (rc != SA_OK) {
		free(*posx_opts);
		*posx_opts = NULL;
	}

	return (rc);
}


/*
 * Helper function to ask nfsd to refresh from the /etc/exports file
 */
static int nfs_refresh_mountd(void)
{
	char *argv[8] = {
		"/sbin/nfsd",
		"update"
	};
	int rc;

#ifdef DEBUG
	fprintf(stderr, "ZFS: refreshing mountd\r\n");
#endif
	// Run "nfsd update" to re-read /etc/exports, if returncode is 1
	// nfsd might not be running, try starting it with "nfsd start".
	// Check that /etc/export is non-zero?
	rc = libzfs_run_process(argv[0], argv, 0);
	if (rc == 1) {
		char *argv[8] = {
			"/sbin/nfsd",
			"start"
		};
#ifdef DEBUG
		fprintf(stderr, "ZFS: starting mountd\r\n");
#endif
		rc = libzfs_run_process(argv[0], argv, 0);
		if (rc != 0) return SA_SYSTEM_ERR;
	}
	return SA_OK;
}



static int
nfs_enable_share_one(nfs_host_cookie_t *udata, const char *host,
					 const char *access)
{
	int rc;
	char *osxhost;

	rc = get_osx_hostspec(host, &osxhost);

#ifdef DEBUG
	fprintf(stderr, "share_one path '%s' host '%s'->'%s' sec '%s' acc '%s'\r\n",
			udata->sharepath, host, osxhost, udata->security, access);
#endif

	if (!strcmp(access, "rw")) {

		fprintf(udata->file, "%s\t%s\n",
				udata->exportname, osxhost);

	} else if (!strcmp(access, "root")) {

		fprintf(udata->file, "%s\t-maproot=root %s\n",
				udata->exportname, osxhost);

	} else if (!strcmp(access, "ro")) {

		fprintf(udata->file, "%s\t-ro %s\n",
				udata->exportname, osxhost);

	}

	free(osxhost);

	if (rc < 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}



/*
 * Enables NFS sharing for the specified share.
 * Create the exportname, which is "/mountpoint/dataset" - where the quotationmarks
 * are included (to handle spaces).
 * Open TMPFILE, and copy over the /etc/export file, skipping any line starting with
 * the exportname.
 * Then call foreach_nfs_host, to output new entries for this exportname
 * Close TMPFILE, and rename back to /etc/exports.
 */
static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	char *shareopts, *osx_opts, *mountpoint;
	char *exportname = NULL;
	int rc;
	int len;
	FILE *file = NULL;
	FILE *exportfile = NULL;
	char line[256];
	static char tempfile[] = EXPORTFILE".XXXXXX";
	int fd = -1;
	int exportfd;

	shareopts = FSINFO(impl_share, nfs_fstype)->shareopts;
	mountpoint = impl_share->sharepath;

	if (shareopts == NULL)
		return (SA_OK);

	rc = get_osx_shareopts(shareopts, &osx_opts);

	if (rc != SA_OK)
		return (rc);

	// Create the exportname.
	len = strlen(mountpoint) + 2 /* "" */ + 1 /* nul */;
	exportname = malloc(len);
	if (!len) return (SA_SYSTEM_ERR);

	snprintf(exportname, len, "\"%s\"", mountpoint);
#ifdef DEBUG
	fprintf(stderr, "ZFS: enable_share '%s' ops '%s'\r\n",
			exportname, osx_opts);
#endif

	// Create temporary file
	fd = mkstemp(tempfile);

	if (fd < 0)
		goto failed;

	file = fdopen(fd, "r+");

	if (!file) goto failed;

	// Open the export file, exclusive?
	exportfd = open(EXPORTFILE, O_RDONLY|O_EXLOCK);
	if (exportfd >= 0)
		exportfile = fdopen(exportfd, "r");

	// If exist, copy contents over
	if (exportfile) {
		while(fgets(line, sizeof(line), exportfile)) {
			// Skip lines that are "exportname".
			if (!strncmp(line, exportname, strlen(exportname)))
				continue;
			fputs(line, file);
		}
	}

	// Output fresh lines for this share
	rc = foreach_nfs_host(impl_share, nfs_enable_share_one, osx_opts, file, exportname);

	// Rename or Unlink
	if (rc == SA_OK) {
		rename(tempfile, EXPORTFILE);
	} else {
		unlink(tempfile);
	}

	// Free, close, return
	free(osx_opts);
	if (exportfile)
		fclose(exportfile);
	if (exportfd >=0)
		close(exportfd);
	fclose(file);
	close(fd);
	free(exportname);

	rc = nfs_refresh_mountd();

	return (rc);

  failed:

	if (fd >= 0) close(fd);
	if (exportname) free(exportname);
	return (SA_SYSTEM_ERR);

}


/*
 * Disables NFS sharing for the specified share.
 */
static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	// Create the exportname.
	int len;
	FILE *file = NULL;
	FILE *exportfile = NULL;
	char *exportname = NULL;
	char line[256];
	char *mountpoint;
	static char tempfile[] = EXPORTFILE"XXXXXX";
	int fd = -1;
	int exportfd;
	int rc = SA_OK;

	mountpoint = impl_share->sharepath;

	len = strlen(mountpoint) + 2 /* "" */ + 1 /* nul */;
	exportname = malloc(len);
	if (!len) return (SA_SYSTEM_ERR);

	snprintf(exportname, len, "\"%s\"", mountpoint);
#ifdef DEBUG
	fprintf(stderr, "ZFS: disable_share '%s'\r\n",
			exportname);
#endif

	// Create temporary file

	fd = mkstemp(tempfile);

	if (fd < 0)
		goto failed;

	file = fdopen(fd, "r+");

	if (!file) goto failed;

	// Open the export file, exclusive?
	exportfd = open(EXPORTFILE, O_RDONLY|O_EXLOCK);
	if (exportfd >= 0)
		exportfile = fdopen(exportfd, "r");

	// If exist, copy contents over
	if (exportfile) {
		while(fgets(line, sizeof(line), exportfile)) {
			// Skip lines that are "exportname".
			if (!strncmp(line, exportname, strlen(exportname)))
				continue;
			fputs(line, file);
		}
	}

	// Don't output lines when disabling.
	//rc = (foreach_nfs_host(impl_share, nfs_disable_share_one, NULL, NULL, NULL));

	// Rename or Unlink
	if (rc == SA_OK) {
		rename(tempfile, EXPORTFILE);
	} else {
		unlink(tempfile);
	}

	// Free, close, return
	if (exportfile)
		fclose(exportfile);
	if (exportfd >=0)
		close(exportfd);
	fclose(file);
	close(fd);
	free(exportname);

	rc = nfs_refresh_mountd();

	return (rc);

  failed:

	if (fd >= 0) close(fd);
	if (exportname) free(exportname);
	return (SA_SYSTEM_ERR);

}

/*
 * Checks whether the specified NFS share options are syntactically correct.
 */
static int
nfs_validate_shareopts(const char *shareopts)
{
	char *osx_opts;
	int rc;

	rc = get_osx_shareopts(shareopts, &osx_opts);

	if (rc != SA_OK)
		return (rc);

	free(osx_opts);

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
	FILE *fp;

	fp = fopen(EXPORTFILE, "r");

	if (fp == NULL) {
		return (B_FALSE);
	}

	while (fgets(line, sizeof (line), fp) != NULL) {
		/*
		 * exportfs uses separate lines for the share path
		 * and the export options when the share path is longer
		 * than a certain amount of characters; this ignores
		 * the option lines
		 */
		if (line[0] == '\t')
			continue;

		tab = strchr(line, '\t');

		// Skip quotes
		cur = line;
		if (*cur == '"') cur++;

		if (tab) {
			tab--;
			if (*tab == '"') *tab = 0;
			tab++;
		}

		if (strcmp(cur, impl_share->sharepath) == 0) {
			fclose(fp);
			return (B_TRUE);
		}
	}

	fclose(fp);

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
 * Initializes the NFS functionality of libshare.
 */
void
libshare_nfs_init(void)
{
	nfs_fstype = register_fstype("nfs", &nfs_shareops);
}
