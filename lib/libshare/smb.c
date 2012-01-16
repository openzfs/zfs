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
 * Copyright (c) 2011 Turbo Fredriksson <turbo@bayour.com>
 *
 * This is an addition to the zfs device driver to add, modify and remove SMB
 * shares using the 'net share' command that comes with Samba.
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

/* The maximum SMB share name seems to be 254 characters, though good references are hard to find. */
#define SMB_NAME_MAX 255

typedef struct smb_share_s {
	char name[SMB_NAME_MAX];	/* Share name */
	char path[PATH_MAX];		/* Share path */
	char comment[255];		/* Share's comment */
	boolean_t guest_ok;		/* 'y' or 'n' */

	struct smb_share_s *next;
} smb_share_t;

static sa_fstype_t *smb_fstype;
boolean_t smb_available;
smb_share_t *smb_shares;

#define SHARE_DIR "/var/lib/samba/usershares"
#define NET_CMD_PATH "/usr/bin/net"

int smb_retrieve_shares(void);

int
smb_enable_share_one(const char *sharename, const char *sharepath)
{
	char *argv[10];
	char name[SMB_NAME_MAX], comment[SMB_NAME_MAX];
	int rc;

#ifdef DEBUG
	fprintf(stderr, "    smb_enable_share_one(%s, %s)\n",
		sharename, sharepath);
#endif

	/* Support ZFS share name regexp '[[:alnum:]_-.: ]' */
	strncpy(name, sharename, sizeof(name));
	name [sizeof(name)-1] = '\0';

	char *pos = name;
	if( pos == NULL )
		return SA_SYSTEM_ERR;

	while( *pos != '\0' ) {
		switch( *pos ) {
		case '/':
		case '-':
		case ':':
		case ' ':
			*pos = '_';
	  }
	  ++pos;
	}

	/* CMD: net -S 127.0.0.1 usershare add Test1 /share/Test1 "Comment" "Everyone:F" */
	snprintf(comment, sizeof (comment), "Comment: %s", sharepath);

	if (!file_is_executable(NET_CMD_PATH)) {
		fprintf(stderr, "ERROR: %s: Does not exists or is not executable.\n", NET_CMD_PATH);
		return SA_SYSTEM_ERR;
	}
	argv[0]  = NET_CMD_PATH;
	argv[1]  = "-S";
	argv[2]  = "127.0.0.1";
	argv[3]  = "usershare";
	argv[4]  = "add";
	argv[5]  = name;
	argv[6]  = sharepath;
	argv[7]  = comment;
	argv[8] = "Everyone:F";
	argv[9] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "  CMD: ");
	for (i=0; argv[i] != NULL; i++) {
		fprintf(stderr, "%s ", argv[i]);
	}
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, 0);
	if (rc < 0)
		return SA_SYSTEM_ERR;

	/* Reload the share file */
	smb_retrieve_shares();

	return 0;
}

int
smb_enable_share(sa_share_impl_t impl_share)
{
	char *shareopts;

#ifdef DEBUG
	fprintf(stderr, "smb_enable_share(): dataset=%s, path=%s\n",
		impl_share->dataset, impl_share->sharepath);
#endif

	if (!smb_available) {
#ifdef DEBUG
		fprintf(stderr, "  smb_enable_share(): -> !smb_available\n");
#endif
		return SA_SYSTEM_ERR;
	}

	shareopts = FSINFO(impl_share, smb_fstype)->shareopts;
	if (shareopts == NULL) { /* on/off */
#ifdef DEBUG
		fprintf(stderr, "  smb_enable_share(): -> SA_SYSTEM_ERR\n");
#endif
		return SA_SYSTEM_ERR;
	}

	if (strcmp(shareopts, "off") == 0) {
#ifdef DEBUG
		fprintf(stderr, "  smb_enable_share(): -> off (0)\n");
#endif
		return (0);
	}

	/* Magic: Enable (i.e., 'create new') share */
	return smb_enable_share_one(impl_share->dataset,
		impl_share->sharepath);
}

int
smb_disable_share_one(const char *sharename)
{
	int rc = SA_OK;
	char *argv[6];

#ifdef DEBUG
	fprintf(stderr, "    smb_disable_share_one()\n");
#endif

	/* CMD: net -S 127.0.0.1 usershare delete Test1 */
	if (!file_is_executable(NET_CMD_PATH)) {
		fprintf(stderr, "ERROR: %s: Does not exists or is not executable.\n", NET_CMD_PATH);
		return SA_SYSTEM_ERR;
	}
	argv[0] = NET_CMD_PATH;
	argv[1] = "usershare";
	argv[2] = "delete";
	argv[3] = strdup(sharename);
	argv[4] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "  CMD: ");
	for (i=0; argv[i] != NULL; i++) {
		fprintf(stderr, "%s ", argv[i]);
	}
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, 0);
	if (rc < 0)
		return SA_SYSTEM_ERR;

	return rc;
}

int
smb_disable_share(sa_share_impl_t impl_share)
{
	smb_share_t *shares = smb_shares;

#ifdef DEBUG
	fprintf(stderr, "  smb_disable_share(): dataset=%s, path=%s\n",
		impl_share->dataset, impl_share->sharepath);
#endif

	while (shares != NULL) {
		if (strcmp(impl_share->sharepath, shares->path) == 0)
			return smb_disable_share_one(shares->name);

		shares = shares->next;
	}

	/* Reload the share file */
	smb_retrieve_shares();

	return 0;
}

static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	smb_share_t *shares = smb_shares;

#ifdef DEBUG
	fprintf(stderr, "  smb_is_share_active(): dataset=%s, path=%s\n",
		impl_share->dataset, impl_share->sharepath);
#endif

	while (shares != NULL) {
		if (strcmp(impl_share->sharepath, shares->path) == 0)
			return B_TRUE;

		shares = shares->next;
	}

	return B_FALSE;
}

static int
smb_validate_shareopts(const char *shareopts)
{
	/* TODO: implement */
	return 0;
}

static int
smb_update_shareopts(sa_share_impl_t impl_share, const char *resource,
		       const char *shareopts)
{
	char *shareopts_dup;
	boolean_t needs_reshare = B_FALSE;
	char *old_shareopts;
#ifdef DEBUG
	fprintf(stderr, "smb_update_shareopts()\n");
#endif

	FSINFO(impl_share, smb_fstype)->active = smb_is_share_active(impl_share);

	old_shareopts = FSINFO(impl_share, smb_fstype)->shareopts;

	if (FSINFO(impl_share, smb_fstype)->active && old_shareopts != NULL &&
	    strcmp(old_shareopts, shareopts) != 0) {
		needs_reshare = B_TRUE;
		smb_disable_share(impl_share);
	}

	shareopts_dup = strdup(shareopts);

	if (shareopts_dup == NULL)
		return SA_NO_MEMORY;

	if (old_shareopts != NULL)
		free(old_shareopts);

	FSINFO(impl_share, smb_fstype)->shareopts = shareopts_dup;

	if (needs_reshare)
		smb_enable_share(impl_share);

	return 0;
}

static void
smb_clear_shareopts(sa_share_impl_t impl_share)
{
#ifdef DEBUG
	fprintf(stderr, "smb_clear_shareopts()\n");
#endif
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

int
smb_retrieve_shares(void)
{
	int rc = SA_OK;
	char file_path[PATH_MAX], line[512], *token, *key, *value, *dup_value, *c;
	char *path = NULL, *comment = NULL, *name = NULL, *guest_ok = NULL;
	DIR *shares_dir;
	FILE *share_file_fp;
	struct dirent *directory;
	struct stat eStat;
	smb_share_t *shares, *new_shares = NULL;

#ifdef DEBUG
	fprintf(stderr, "  smb_retrieve_shares()\n");
#endif

	/* opendir(), stat() */
	shares_dir = opendir(SHARE_DIR);
	if (shares_dir == NULL) {
#ifdef DEBUG
		fprintf(stderr, "    opendir() == NULL\n");
#endif
		return SA_SYSTEM_ERR;
	}

	/* Go through the directory, looking for shares */
	while ((directory = readdir(shares_dir))) {
		if (stat(directory->d_name, &eStat) == ENOMEM) {
			rc = SA_NO_MEMORY;
			goto out;
		}

		if ((directory->d_name[0] == '.') || 
		    !S_ISREG(eStat.st_mode))
			continue;
#ifdef DEBUG
		fprintf(stderr, "    %s\n", directory->d_name);
#endif

		snprintf(file_path, sizeof (file_path),
			 "%s/%s", SHARE_DIR, directory->d_name);
		if ((share_file_fp = fopen(file_path, "r")) == NULL) {
#ifdef DEBUG
			fprintf(stderr, "    fopen() == NULL\n");
#endif
			rc = SA_SYSTEM_ERR;
			goto out;
		}

		name = strdup(directory->d_name);
		if (name == NULL) {
			 rc = SA_NO_MEMORY;
			 goto out;
		}

		while (fgets(line, sizeof(line), share_file_fp)) {
			if (line[0] == '#')
				continue;

			/* Trim trailing new-line character(s). */
			while (line[strlen(line) - 1] == '\r' ||
			       line[strlen(line) - 1] == '\n')
				line[strlen(line) - 1] = '\0';
//			for (c = line; *c; c++) {
//				if (*c == '\r' || *c == '\n') {
//					c = '\0';
//					break;
//				}
//			}

#ifdef DEBUG
			fprintf(stderr, "      %s ", line);
#endif

			/* Split the line in two, separated by '=' */
			token = strchr(line, '=');
			if (token == NULL)
				continue;

			key = line;
			value = token + 1;
			*token = '\0';
#ifdef DEBUG
			fprintf(stderr, "(%s = %s)\n", key, value);
#endif

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
				shares = (smb_share_t *)malloc(sizeof (smb_share_t));
				if (shares == NULL) {
					rc = SA_NO_MEMORY;
					goto out;
				}

				strncpy(shares->name, name, sizeof (shares->name));
				shares->name [sizeof(shares->name)-1] = '\0';

				strncpy(shares->path, path, sizeof (shares->path));
				shares->path [sizeof(shares->path)-1] = '\0';

				strncpy(shares->comment, comment, sizeof (shares->comment));
				shares->comment [sizeof(shares->comment)-1] = '\0';

				shares->guest_ok = atoi(guest_ok);

				shares->next = new_shares;
				new_shares = shares;

				name     = NULL;
				path     = NULL;
				comment  = NULL;
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

	return rc;
}

void
libshare_smb_init(void)
{
#ifdef DEBUG
	fprintf(stderr, "libshare_smb_init()\n");
#endif
	smb_available = (smb_retrieve_shares() == SA_OK);

	smb_fstype = register_fstype("smb", &smb_shareops);
}
