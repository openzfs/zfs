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
 * Copyright (c) 2016 Jorgen Lundman <lundman@lundman.net>
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "smb.h"

#include <ctype.h>
#include <sys/socket.h>

static boolean_t smb_available(void);

#define	SMB_NAME_MAX 255
#define	SHARING_CMD_PATH "/usr/sbin/sharing"

smb_share_t *smb_shares;
static int smb_disable_share(sa_share_impl_t impl_share);
static boolean_t smb_is_share_active(sa_share_impl_t impl_share);

/*
 * Parse out a "value" part of a "line" of input. By skipping white space.
 * If line ends up being empty, read the next line, skipping white spare.
 * strdup() value before returning.
 */
static int
get_attribute(const char *attr, char *line, char **value, FILE *file)
{
	char *r = line;
	char line2[512];

	if (strncasecmp(attr, line, strlen(attr)))
		return (0);

	r += strlen(attr);

	while (isspace(*r)) r++; // Skip whitespace

	// Nothing left? Read next line
	if (!*r) {
		if (!fgets(line2, sizeof (line2), file))
			return (0);
		// Eat newlines
		if ((r = strchr(line2, '\r'))) *r = 0;
		if ((r = strchr(line2, '\n'))) *r = 0;
		// Parse new input
		r = line2;
		while (isspace(*r)) r++; // Skip whitespace
	}

	// Did we get something?
	if (*r) {
		*value = strdup(r);
		return (1);
	}
	return (0);
}

static int
spawn_with_pipe(const char *path, char *argv[], int flags)
{
	int fd[2];
	pid_t pid;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, fd) != 0)
		return (-1);

	pid = vfork();

	// Child
	if (pid == 0) {
		close(fd[0]);
		dup2(fd[1], STDIN_FILENO);
		dup2(fd[1], STDOUT_FILENO);
		if (flags) dup2(fd[1], STDERR_FILENO);
		(void) execvp(path, argv);
		_exit(-1);
	}
	// Parent and error
	close(fd[1]);
	if (pid == -1) {
		close(fd[0]);
		return (-1);
	}
	return (fd[0]);
}

/*
 * Retrieve the list of SMB shares. We execute "dscl . -readall /SharePoints"
 * which gets us shares in the format:
 * dsAttrTypeNative:directory_path: /Volumes/BOOM/zfstest
 * dsAttrTypeNative:smb_name: zfstest
 * dsAttrTypeNative:smb_shared: 1
 * dsAttrTypeNative:smb_guestaccess: 1
 *
 * Note that long lines can be continued on the next line, with a leading space:
 * dsAttrTypeNative:smb_name:
 *  lundman's Public Folder
 *
 * We don't use "sharing -l" as its output format is "peculiar".
 *
 * This is a temporary implementation that should be replaced with
 * direct DirectoryService API calls.
 *
 */
static int
smb_retrieve_shares(void)
{
	char line[512];
	char *path = NULL, *shared = NULL, *name = NULL;
	char *guest = NULL, *r;
	smb_share_t *shares, *new_shares = NULL;
	int fd;
	FILE *file = NULL;
	const char *argv[8] = {
		"/usr/bin/dscl",
		".",
		"-readall",
		"/SharePoints"
	};

	fd = spawn_with_pipe(argv[0], (char **)argv, 0);

	if (fd < 0)
		return (SA_SYSTEM_ERR);

	file = fdopen(fd, "r");
	if (!file) {
		close(fd);
		return (SA_SYSTEM_ERR);
	}

	while (fgets(line, sizeof (line), file)) {

		if ((r = strchr(line, '\r'))) *r = 0;
		if ((r = strchr(line, '\n'))) *r = 0;

		if (get_attribute("dsAttrTypeNative:smb_name:",
		    line, &name, file) ||
		    get_attribute("dsAttrTypeNative:directory_path:",
		    line, &path, file) ||
		    get_attribute("dsAttrTypeNative:smb_guestaccess:",
		    line, &guest, file) ||
		    get_attribute("dsAttrTypeNative:smb_shared:",
		    line, &shared, file)) {

			// If we have all desired attributes, create a new
			// share, & it is currently shared (not just listed)
			if (name && path && guest && shared &&
			    atoi(shared) != 0) {

				shares = (smb_share_t *)
				    malloc(sizeof (smb_share_t));

				if (shares) {
					strlcpy(shares->name, name,
					    sizeof (shares->name));
					strlcpy(shares->path, path,
					    sizeof (shares->path));
					shares->guest_ok = atoi(guest);

#ifdef DEBUG
					fprintf(stderr,
					    "ZFS: smbshare '%s' mount '%s'\r\n",
					    name, path);
#endif

					shares->next = new_shares;
					new_shares = shares;
				} // shares malloc

				// Make it free all variables
				strlcpy(line, "-", sizeof (line));

			} // if all

		} // if got_attribute

		if (strncmp("-", line, sizeof (line)) == 0) {
			if (name)   {	free(name); 	name  = NULL; }
			if (path)   {	free(path); 	path  = NULL; }
			if (guest)  {	free(guest);	guest = NULL; }
			if (shared) {	free(shared);	shared = NULL; }
		} // if "-"
	} // while fgets

	fclose(file);
	close(fd);

	if (name)   {	free(name); 	name  = NULL; }
	if (path)   {	free(path); 	path  = NULL; }
	if (guest)  {	free(guest);	guest = NULL; }
	if (shared) {	free(shared);	shared = NULL; }

	/*
	 * The ZOL implementation here just leaks the previous list in
	 * "smb_shares" each time this is called, and it is called a lot.
	 * We really should iterate through and release nodes. Alternatively
	 * only update if we have not run before, and have a way to force
	 * a refresh after enabling/disabling a share.
	 */
	smb_shares = new_shares;

	return (SA_OK);
}

/*
 * Used internally by smb_enable_share to enable sharing for a single host.
 */
static int
smb_enable_share_one(const char *sharename, const char *sharepath)
{
	const char *argv[10];
	int rc;
	smb_share_t *shares = smb_shares;
	(void) sharename;

	/* Loop through shares and check if our share is also smbshared */
	while (shares != NULL) {
		if (strcmp(sharepath, shares->path) == 0) {
			break;
		}
		shares = shares->next;
	}

	/*
	 * CMD: sharing -a /mountpoint -s 001 -g 001
	 * Where -s 001 specified sharing smb, not ftp nor afp.
	 *   and -g 001 specifies to enable guest on smb.
	 * Note that the OS X 10.11 man-page incorrectly claims 010 for smb
	 */
	argv[0] = (const char *)SHARING_CMD_PATH;
	argv[1] = "-a";
	argv[2] = sharepath;
	argv[3] = "-s";
	argv[4] = "001";
	argv[5] = "-g";
	argv[6] = "001";
	argv[7] = NULL;

#ifdef DEBUG
	fprintf(stderr,
	    "ZFS: enabling share '%s' at '%s'\r\n",
	    sharename, sharepath);
#endif

	rc = libzfs_run_process(argv[0], (char **)argv, 0);
	if (rc < 0)
		return (SA_SYSTEM_ERR);

	/* Reload the share file */
	(void) smb_retrieve_shares();

	return (SA_OK);
}

/*
 * Enables SMB sharing for the specified share.
 */
static int
smb_enable_share(sa_share_impl_t impl_share)
{
	const char *shareopts;

	if (!smb_available())
		return (SA_SYSTEM_ERR);

	if (smb_is_share_active(impl_share))
		smb_disable_share(impl_share);

	shareopts = impl_share->sa_shareopts;
	if (shareopts == NULL) /* on/off */
		return (SA_SYSTEM_ERR);

	if (strcmp(shareopts, "off") == 0)
		return (SA_OK);

	/* Magic: Enable (i.e., 'create new') share */
	return (smb_enable_share_one(impl_share->sa_zfsname,
	    impl_share->sa_mountpoint));
}

/*
 * Used internally by smb_disable_share to disable sharing for a single host.
 */
static int
smb_disable_share_one(const char *sharename)
{
	int rc;
	const char *argv[8];

	/* CMD: sharing -r name */
	argv[0] = SHARING_CMD_PATH;
	argv[1] = (char *)"-r";
	argv[2] = (char *)sharename;
	argv[3] = NULL;

#ifdef DEBUG
	fprintf(stderr, "ZFS: disabling share '%s' \r\n",
	    sharename);
#endif

	rc = libzfs_run_process(argv[0], (char **)argv, 0);
	if (rc < 0)
		return (SA_SYSTEM_ERR);
	else
		return (SA_OK);
}

/*
 * Disables SMB sharing for the specified share.
 */
static int
smb_disable_share(sa_share_impl_t impl_share)
{
	smb_share_t *shares = smb_shares;

	if (!smb_available()) {
		/*
		 * The share can't possibly be active, so nothing
		 * needs to be done to disable it.
		 */
		return (SA_OK);
	}

	while (shares != NULL) {
		if (strcmp(impl_share->sa_mountpoint, shares->path) == 0)
			return (smb_disable_share_one(shares->name));

		shares = shares->next;
	}

	return (SA_OK);
}

/*
 * Checks whether the specified SMB share options are syntactically correct.
 */
static int
smb_validate_shareopts(const char *shareopts)
{
	/* TODO: Accept 'name' and sec/acl (?) */
	if ((strcmp(shareopts, "off") == 0) || (strcmp(shareopts, "on") == 0))
		return (SA_OK);

	return (SA_SYNTAX_ERR);
}

/*
 * Checks whether a share is currently active.
 */
static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	smb_share_t *iter = smb_shares;

	if (!smb_available())
		return (B_FALSE);

	/* Retrieve the list of (possible) active shares */
	smb_retrieve_shares();

	while (iter != NULL) {
		if (strcmp(impl_share->sa_mountpoint, iter->path) == 0)
			return (B_TRUE);

		iter = iter->next;
	}

	return (B_FALSE);
}

static int
smb_update_shares(void)
{
	/* Not implemented */
	return (0);
}

const sa_fstype_t libshare_smb_type = {
	.enable_share = smb_enable_share,
	.disable_share = smb_disable_share,
	.is_shared = smb_is_share_active,

	.validate_shareopts = smb_validate_shareopts,
	.commit_shares = smb_update_shares,
};


/*
 * Provides a convenient wrapper for determining SMB availability
 */
static boolean_t
smb_available(void)
{

	if (access(SHARING_CMD_PATH, F_OK) != 0)
		return (B_FALSE);

	return (B_TRUE);
}
