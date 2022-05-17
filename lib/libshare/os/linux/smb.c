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
 * Copyright (c) 2011,2012 Turbo Fredriksson <turbo@bayour.com>, based on nfs.c
 *                         by Gunnar Beutner
 * Copyright (c) 2019, 2020 by Delphix. All rights reserved.
 *
 * This is an addition to the zfs device driver to add, modify and remove SMB
 * shares using the 'net share' command that comes with Samba.
 *
 * TESTING
 * Make sure that samba listens to 'localhost' (127.0.0.1) and that the options
 * 'usershare max shares' and 'usershare owner only' have been reviewed/set
 * accordingly (see zfs(8) for information).
 *
 * Once configuration in samba have been done, test that this
 * works with the following three commands (in this case, my ZFS
 * filesystem is called 'share/Test1'):
 *
 *	(root)# net -U root -S 127.0.0.1 usershare add Test1 /share/Test1 \
 *		"Comment: /share/Test1" "Everyone:F"
 *	(root)# net usershare list | grep -i test
 *	(root)# net -U root -S 127.0.0.1 usershare delete Test1
 *
 * The first command will create a user share that gives everyone full access.
 * To limit the access below that, use normal UNIX commands (chmod, chown etc).
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
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

static boolean_t smb_available(void);

static smb_share_t *smb_shares;
static int smb_disable_share(sa_share_impl_t impl_share);
static boolean_t smb_is_share_active(sa_share_impl_t impl_share);

/*
 * Retrieve the list of SMB shares.
 */
static int
smb_retrieve_shares(void)
{
	int rc = SA_OK;
	char file_path[PATH_MAX], line[512], *token, *key, *value;
	char *dup_value = NULL, *path = NULL, *comment = NULL, *name = NULL;
	char *guest_ok = NULL;
	DIR *shares_dir;
	FILE *share_file_fp = NULL;
	struct dirent *directory;
	struct stat eStat;
	smb_share_t *shares, *new_shares = NULL;

	/* opendir(), stat() */
	shares_dir = opendir(SHARE_DIR);
	if (shares_dir == NULL)
		return (SA_SYSTEM_ERR);

	/* Go through the directory, looking for shares */
	while ((directory = readdir(shares_dir))) {
		if (directory->d_name[0] == '.')
			continue;

		snprintf(file_path, sizeof (file_path),
		    "%s/%s", SHARE_DIR, directory->d_name);

		if (stat(file_path, &eStat) == -1) {
			rc = SA_SYSTEM_ERR;
			goto out;
		}

		if (!S_ISREG(eStat.st_mode))
			continue;

		if ((share_file_fp = fopen(file_path, "re")) == NULL) {
			rc = SA_SYSTEM_ERR;
			goto out;
		}

		name = strdup(directory->d_name);
		if (name == NULL) {
			rc = SA_NO_MEMORY;
			goto out;
		}

		while (fgets(line, sizeof (line), share_file_fp)) {
			if (line[0] == '#')
				continue;

			/* Trim trailing new-line character(s). */
			while (line[strlen(line) - 1] == '\r' ||
			    line[strlen(line) - 1] == '\n')
				line[strlen(line) - 1] = '\0';

			/* Split the line in two, separated by '=' */
			token = strchr(line, '=');
			if (token == NULL)
				continue;

			key = line;
			value = token + 1;
			*token = '\0';

			dup_value = strdup(value);
			if (dup_value == NULL) {
				rc = SA_NO_MEMORY;
				goto out;
			}

			if (strcmp(key, "path") == 0) {
				free(path);
				path = dup_value;
			} else if (strcmp(key, "comment") == 0) {
				free(comment);
				comment = dup_value;
			} else if (strcmp(key, "guest_ok") == 0) {
				free(guest_ok);
				guest_ok = dup_value;
			} else
				free(dup_value);

			dup_value = NULL;

			if (path == NULL || comment == NULL || guest_ok == NULL)
				continue; /* Incomplete share definition */
			else {
				shares = (smb_share_t *)
				    malloc(sizeof (smb_share_t));
				if (shares == NULL) {
					rc = SA_NO_MEMORY;
					goto out;
				}

				(void) strlcpy(shares->name, name,
				    sizeof (shares->name));

				(void) strlcpy(shares->path, path,
				    sizeof (shares->path));

				(void) strlcpy(shares->comment, comment,
				    sizeof (shares->comment));

				shares->guest_ok = atoi(guest_ok);

				shares->next = new_shares;
				new_shares = shares;

				free(path);
				free(comment);
				free(guest_ok);

				path = NULL;
				comment = NULL;
				guest_ok = NULL;
			}
		}

out:
		if (share_file_fp != NULL) {
			fclose(share_file_fp);
			share_file_fp = NULL;
		}

		free(name);
		free(path);
		free(comment);
		free(guest_ok);

		name = NULL;
		path = NULL;
		comment = NULL;
		guest_ok = NULL;
	}
	closedir(shares_dir);

	smb_shares = new_shares;

	return (rc);
}

/*
 * Used internally by smb_enable_share to enable sharing for a single host.
 */
static int
smb_enable_share_one(const char *sharename, const char *sharepath)
{
	char name[SMB_NAME_MAX], comment[SMB_COMMENT_MAX];

	/* Support ZFS share name regexp '[[:alnum:]_-.: ]' */
	strlcpy(name, sharename, sizeof (name));
	for (char *itr = name; *itr != '\0'; ++itr)
		switch (*itr) {
		case '/':
		case '-':
		case ':':
		case ' ':
			*itr = '_';
		}

	/*
	 * CMD: net -S NET_CMD_ARG_HOST usershare add Test1 /share/Test1 \
	 *      "Comment" "Everyone:F"
	 */
	snprintf(comment, sizeof (comment), "Comment: %s", sharepath);

	char *argv[] = {
		(char *)NET_CMD_PATH,
		(char *)"-S",
		(char *)NET_CMD_ARG_HOST,
		(char *)"usershare",
		(char *)"add",
		name,
		(char *)sharepath,
		comment,
		(char *)"Everyone:F",
		NULL,
	};

	if (libzfs_run_process(argv[0], argv, 0) != 0)
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
	if (!smb_available())
		return (SA_SYSTEM_ERR);

	if (smb_is_share_active(impl_share))
		smb_disable_share(impl_share);

	if (impl_share->sa_shareopts == NULL) /* on/off */
		return (SA_SYSTEM_ERR);

	if (strcmp(impl_share->sa_shareopts, "off") == 0)
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
	/* CMD: net -S NET_CMD_ARG_HOST usershare delete Test1 */
	char *argv[] = {
		(char *)NET_CMD_PATH,
		(char *)"-S",
		(char *)NET_CMD_ARG_HOST,
		(char *)"usershare",
		(char *)"delete",
		(char *)sharename,
		NULL,
	};

	if (libzfs_run_process(argv[0], argv, 0) != 0)
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
	if (!smb_available()) {
		/*
		 * The share can't possibly be active, so nothing
		 * needs to be done to disable it.
		 */
		return (SA_OK);
	}

	for (const smb_share_t *i = smb_shares; i != NULL; i = i->next)
		if (strcmp(impl_share->sa_mountpoint, i->path) == 0)
			return (smb_disable_share_one(i->name));

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
	if (!smb_available())
		return (B_FALSE);

	/* Retrieve the list of (possible) active shares */
	smb_retrieve_shares();

	for (const smb_share_t *i = smb_shares; i != NULL; i = i->next)
		if (strcmp(impl_share->sa_mountpoint, i->path) == 0)
			return (B_TRUE);

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
	static int avail;

	if (!avail) {
		struct stat statbuf;

		if (access(NET_CMD_PATH, F_OK) != 0 ||
		    lstat(SHARE_DIR, &statbuf) != 0 ||
		    !S_ISDIR(statbuf.st_mode))
			avail = -1;
		else
			avail = 1;
	}

	return (avail == 1);
}
