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
 *
 * This is an addition to the zfs device driver to add, modify and remove SMB
 * shares using the 'net share' command that comes with Samba.
 *
 * TESTING
 * Make sure that samba listens to 'localhost' (127.0.0.1) and that the options
 * 'usershare max shares' and 'usershare owner only' have been rewied/set
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

static boolean_t smb_available(void);

static sa_fstype_t *smb_fstype;

/*
 * Retrieve the list of SMB shares.
 */
static int
smb_retrieve_shares(void)
{
	int rc = SA_OK;
	char file_path[PATH_MAX], line[512], *token, *key, *value;
	char *dup_value, *path = NULL, *comment = NULL, *name = NULL;
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

		if ((share_file_fp = fopen(file_path, "r")) == NULL) {
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

			if (strcmp(key, "path") == 0)
				path = dup_value;
			if (strcmp(key, "comment") == 0)
				comment = dup_value;
			if (strcmp(key, "guest_ok") == 0)
				guest_ok = dup_value;

			if (path == NULL || comment == NULL || guest_ok == NULL)
				continue; /* Incomplete share definition */
			else {
				shares = (smb_share_t *)
						malloc(sizeof (smb_share_t));
				if (shares == NULL) {
					rc = SA_NO_MEMORY;
					goto out;
				}

				strncpy(shares->name, name,
					sizeof (shares->name));
				shares->name [sizeof (shares->name) - 1] = '\0';

				strncpy(shares->path, path,
				    sizeof (shares->path));
				shares->path [sizeof (shares->path) - 1] = '\0';

				strncpy(shares->comment, comment,
				    sizeof (shares->comment));
				shares->comment[sizeof (shares->comment)-1] =
				    '\0';

				shares->guest_ok = atoi(guest_ok);

				shares->next = new_shares;
				new_shares = shares;

				name = NULL;
				path = NULL;
				comment = NULL;
				guest_ok = NULL;
			}
		}

out:
		if (share_file_fp != NULL)
			fclose(share_file_fp);

		free(name);
		free(path);
		free(comment);
		free(guest_ok);
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
	char *argv[10], *pos;
	char name[SMB_NAME_MAX], comment[SMB_COMMENT_MAX];
	int rc;

	/* Support ZFS share name regexp '[[:alnum:]_-.: ]' */
	strncpy(name, sharename, sizeof (name));
	name [sizeof (name)-1] = '\0';

	pos = name;
	while (*pos != '\0') {
		switch (*pos) {
		case '/':
		case '-':
		case ':':
		case ' ':
			*pos = '_';
		}

		++pos;
	}

	/*
	 * CMD: net -S NET_CMD_ARG_HOST usershare add Test1 /share/Test1 \
	 *      "Comment" "Everyone:F"
	 */
	snprintf(comment, sizeof (comment), "Comment: %s", sharepath);

	argv[0] = NET_CMD_PATH;
	argv[1] = (char *)"-S";
	argv[2] = NET_CMD_ARG_HOST;
	argv[3] = (char *)"usershare";
	argv[4] = (char *)"add";
	argv[5] = (char *)name;
	argv[6] = (char *)sharepath;
	argv[7] = (char *)comment;
	argv[8] = "Everyone:F";
	argv[9] = NULL;

	rc = libzfs_run_process(argv[0], argv, 0);
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
	char *shareopts;

	if (!smb_available())
		return (SA_SYSTEM_ERR);

	shareopts = FSINFO(impl_share, smb_fstype)->shareopts;
	if (shareopts == NULL) /* on/off */
		return (SA_SYSTEM_ERR);

	if (strcmp(shareopts, "off") == 0)
		return (SA_OK);

	/* Magic: Enable (i.e., 'create new') share */
	return (smb_enable_share_one(impl_share->dataset,
	    impl_share->sharepath));
}

/*
 * Used internally by smb_disable_share to disable sharing for a single host.
 */
static int
smb_disable_share_one(const char *sharename)
{
	int rc;
	char *argv[7];

	/* CMD: net -S NET_CMD_ARG_HOST usershare delete Test1 */
	argv[0] = NET_CMD_PATH;
	argv[1] = (char *)"-S";
	argv[2] = NET_CMD_ARG_HOST;
	argv[3] = (char *)"usershare";
	argv[4] = (char *)"delete";
	argv[5] = strdup(sharename);
	argv[6] = NULL;

	rc = libzfs_run_process(argv[0], argv, 0);
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
		if (strcmp(impl_share->sharepath, shares->path) == 0)
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
	if (!smb_available())
		return (B_FALSE);

	/* Retrieve the list of (possible) active shares */
	smb_retrieve_shares();

	while (smb_shares != NULL) {
		if (strcmp(impl_share->sharepath, smb_shares->path) == 0)
			return (B_TRUE);

		smb_shares = smb_shares->next;
	}

	return (B_FALSE);
}

/*
 * Called to update a share's options. A share's options might be out of
 * date if the share was loaded from disk and the "sharesmb" dataset
 * property has changed in the meantime. This function also takes care
 * of re-enabling the share if necessary.
 */
static int
smb_update_shareopts(sa_share_impl_t impl_share, const char *resource,
    const char *shareopts)
{
	char *shareopts_dup;
	boolean_t needs_reshare = B_FALSE;
	char *old_shareopts;

	if (!impl_share)
		return (SA_SYSTEM_ERR);

	FSINFO(impl_share, smb_fstype)->active =
	    smb_is_share_active(impl_share);

	old_shareopts = FSINFO(impl_share, smb_fstype)->shareopts;

	if (FSINFO(impl_share, smb_fstype)->active && old_shareopts != NULL &&
		strcmp(old_shareopts, shareopts) != 0) {
		needs_reshare = B_TRUE;
		smb_disable_share(impl_share);
	}

	shareopts_dup = strdup(shareopts);

	if (shareopts_dup == NULL)
		return (SA_NO_MEMORY);

	if (old_shareopts != NULL)
		free(old_shareopts);

	FSINFO(impl_share, smb_fstype)->shareopts = shareopts_dup;

	if (needs_reshare)
		smb_enable_share(impl_share);

	return (SA_OK);
}

/*
 * Clears a share's SMB options. Used by libshare to
 * clean up shares that are about to be free()'d.
 */
static void
smb_clear_shareopts(sa_share_impl_t impl_share)
{
	free(FSINFO(impl_share, smb_fstype)->shareopts);
	FSINFO(impl_share, smb_fstype)->shareopts = NULL;
}

static const sa_share_ops_t smb_shareops = {
	.enable_share = smb_enable_share,
	.disable_share = smb_disable_share,

	.validate_shareopts = smb_validate_shareopts,
	.update_shareopts = smb_update_shareopts,
	.clear_shareopts = smb_clear_shareopts,
};

/*
 * Provides a convenient wrapper for determining SMB availability
 */
static boolean_t
smb_available(void)
{
	struct stat statbuf;

	if (lstat(SHARE_DIR, &statbuf) != 0 ||
	    !S_ISDIR(statbuf.st_mode))
		return (B_FALSE);

	if (access(NET_CMD_PATH, F_OK) != 0)
		return (B_FALSE);

	return (B_TRUE);
}

/*
 * Initializes the SMB functionality of libshare.
 */
void
libshare_smb_init(void)
{
	smb_fstype = register_fstype("smb", &smb_shareops);
}
