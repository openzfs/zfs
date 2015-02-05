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
 * Make sure that samba listens to 'localhost' (127.0.0.1) and
 * that the option 'registry shares' is set to 'yes'.
 *
 * Once configuration in samba have been done, test that this
 * works with the following three commands (in this case, my ZFS
 * filesystem is called 'share/Test1'):
 *
 *	(root)# net -U root -S 127.0.0.1 conf addshare Test1 /share/Test1 \
 *		writeable=y guest_ok=y "Dataset name: share/Test1"
 *	(root)# net conf list | grep -i '^\[test'
 *	(root)# net -U root -S 127.0.0.1 conf delshare Test1
 *
 * The first command will create a registry share that gives
 * everyone full access. To limit the access below that, use
 * normal UNIX commands (chmod, chown etc).
 */

#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <strings.h>
#include <stddef.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "smb.h"

static boolean_t smb_available(void);
static int smb_validate_shareopts(const char *shareopts);

static sa_fstype_t *smb_fstype;
static list_t all_smb_shares_list;

typedef struct smb_shares_list_s {
	char name[SMB_NAME_MAX];
	list_node_t next;
} smb_shares_list_t;

void trim_whitespace(char *s) {
	char *p = s;
	int l = strlen(p);

	while (isspace(p[l - 1])) p[--l] = 0;
	while (*p && isspace(*p)) ++p, --l;

	memmove(s, p, l + 1);
}

static smb_share_t *
smb_retrieve_share_info(char *share_name)
{
	int ret, buffer_len;
	FILE *sharesmb_temp_fp;
	char buffer[512], cmd[PATH_MAX];
	char *token, *key, *value;
	char *dup_value, *path = NULL, *comment = NULL, *guest_ok = NULL,
		*read_only = NULL;
	smb_share_t *share = NULL;

	/* CMD: net conf showshare <share_name> */
	ret = snprintf(cmd, sizeof (cmd), "%s -S %s conf showshare %s",
			NET_CMD_PATH, NET_CMD_ARG_HOST, share_name);
	if (ret < 0 || ret >= sizeof (cmd))
		return (NULL);

	sharesmb_temp_fp = popen(cmd, "r");
	if (sharesmb_temp_fp == NULL)
		return (NULL);

	while (fgets(buffer, sizeof (buffer), sharesmb_temp_fp) != 0) {
		/* Trim trailing new-line character(s). */
		buffer_len = strlen(buffer);
		while (buffer_len > 0) {
			buffer_len--;
			if (buffer[buffer_len] == '\r' ||
			    buffer[buffer_len] == '\n') {
				buffer[buffer_len] = 0;
			} else
				break;
		}

		/* Split the line in two, separated by '=' */
		token = strchr(buffer, '=');
		if (token == NULL)
			/* This will also catch empty lines */
			continue;

		key = buffer;
		value = token + 1;
		*token = '\0';

		trim_whitespace(key);
		trim_whitespace(value);

		dup_value = strdup(value);
		if (dup_value == NULL) {
			if (pclose(sharesmb_temp_fp) != 0)
				fprintf(stderr, "Failed to pclose stream\n");
			return (NULL);
		}

		if (strcmp(key, "path") == 0)
			path = dup_value;
		if (strcmp(key, "comment") == 0)
			comment = dup_value;
		if (strcmp(key, "guest ok") == 0)
			guest_ok = dup_value;
		if (strcmp(key, "read only") == 0)
			read_only = dup_value;

		if (path == NULL || comment == NULL ||
		    guest_ok == NULL || read_only == NULL)
			continue; /* Incomplete share definition */
		else {
			share = (smb_share_t *) malloc(sizeof (smb_share_t));
			if (share == NULL) {
				if (pclose(sharesmb_temp_fp) != 0)
					fprintf(stderr,
						"Failed to pclose stream\n");
				return (NULL);
			}

			strncpy(share->name, share_name, sizeof (share->name));
			share->name[sizeof (share->name)-1] = '\0';

			strncpy(share->path, path, sizeof (share->path));
			share->path[sizeof (share->path)-1] = '\0';

			strncpy(share->comment, comment,
				sizeof (share->comment));
			share->comment[sizeof (share->comment)-1] = '\0';

			if (strcmp(guest_ok, "yes") == 0)
				share->guest_ok = B_TRUE;
			else
				share->guest_ok = B_FALSE;

			if (strcmp(read_only, "no") == 0)
				share->writeable = B_TRUE;
			else
				share->writeable = B_FALSE;

			path = NULL;
			comment = NULL;
			guest_ok = NULL;
			read_only = NULL;
		}
	}

	if (pclose(sharesmb_temp_fp) != 0)
		fprintf(stderr, "Failed to pclose stream\n");

	return (share);
}

/*
 * Retrieve the list of SMB shares.
 * Do this only if we haven't already.
 * TODO: That doesn't work exactly as intended. Threading?
 */
static int
smb_retrieve_shares(void)
{
	int ret, buffer_len;
	FILE *sharesmb_temp_fp;
	char buffer[512], cmd[PATH_MAX];
	smb_share_t *share_info = NULL;

//	if (!list_is_empty(&all_smb_shares_list)) {
// fprintf(stderr, "smb_retrieve_shares: !list_is_empty()\n");
//		/* Try to limit the number of times we do this */
// 		return (SA_OK);
//	}

	/* Create the global share list  */
	list_create(&all_smb_shares_list, sizeof (smb_share_t),
		    offsetof(smb_share_t, next));

	/* First retrieve a list of all shares, without info */
	/* CMD: net conf listshares */
	ret = snprintf(cmd, sizeof (cmd), "%s -S %s conf listshares",
			NET_CMD_PATH, NET_CMD_ARG_HOST);
	if (ret < 0 || ret >= sizeof (cmd))
		return (SA_SYSTEM_ERR);

	sharesmb_temp_fp = popen(cmd, "r");
	if (sharesmb_temp_fp == NULL)
		return (SA_SYSTEM_ERR);

	while (fgets(buffer, sizeof (buffer), sharesmb_temp_fp) != 0) {
		/* Trim trailing new-line character(s). */
		buffer_len = strlen(buffer);
		while (buffer_len > 0) {
			buffer_len--;
			if (buffer[buffer_len] == '\r' ||
			    buffer[buffer_len] == '\n') {
				buffer[buffer_len] = 0;
			} else
				break;
		}

		if (strcmp(buffer, "global") == 0)
			continue; /* Not a share */

		/* Get the detailed info for the share */
		share_info = smb_retrieve_share_info(buffer);

#ifdef DEBUG
		fprintf(stderr, "smb_retrieve_shares: name='%s', "
			"path='%s', comment='%s', guest_ok=%d, "
			"writeable=%d\n",
			share_info->name, share_info->path,
			share_info->comment, share_info->guest_ok,
			share_info->writeable);
#endif

		/* Append the share to the list of new shares */
		list_insert_tail(&all_smb_shares_list, share_info);
	}

	if (pclose(sharesmb_temp_fp) != 0)
		fprintf(stderr, "Failed to pclose stream\n");

	return (SA_OK);
}

/*
 * Validates share option(s).
 */
static int
smb_get_shareopts_cb(const char *key, const char *value, void *cookie)
{
	char *dup_value;
	smb_share_t *opts = (smb_share_t *)cookie;

	if (strcmp(key, "on") == 0)
		return (SA_OK);

	/* guest_ok and guestok is the same */
	if (strcmp(key, "guestok") == 0)
		key = "guest_ok";

	/* Verify all options */
	if (strcmp(key, "name") != 0 &&
	    strcmp(key, "comment") != 0 &&
	    strcmp(key, "writeable") != 0 &&
	    strcmp(key, "guest_ok") != 0)
		return (SA_SYNTAX_ERR);

	dup_value = strdup(value);
	if (dup_value == NULL)
		return (SA_NO_MEMORY);


	/* Get share option values */
	if (strcmp(key, "name") == 0) {
		strncpy(opts->name, dup_value, sizeof (opts->name));
		opts->name [sizeof (opts->name)-1] = '\0';
	}

	if (strcmp(key, "comment") == 0) {
		strncpy(opts->comment, dup_value, sizeof (opts->comment));
		opts->comment[sizeof (opts->comment) - 1] = '\0';
	}

	if (strcmp(key, "writeable") == 0) {
		if (strcmp(dup_value, "y") == 0 ||
		    strcmp(dup_value, "yes") == 0 ||
		    strcmp(dup_value, "true") == 0)
			opts->writeable = B_TRUE;
		else
			opts->writeable = B_FALSE;
	}

	if (strcmp(key, "guest_ok") == 0) {
		if (strcmp(dup_value, "y") == 0 ||
		    strcmp(dup_value, "yes") == 0 ||
		    strcmp(dup_value, "true") == 0)
			opts->guest_ok = B_TRUE;
		else
			opts->guest_ok = B_FALSE;
	}

	return (SA_OK);
}

/*
 * Takes a string containing share options (e.g. "name=Whatever,guest_ok=n")
 * and converts them to a NULL-terminated array of options.
 */
static int
smb_get_shareopts(sa_share_impl_t impl_share, const char *shareopts,
    smb_share_t **opts)
{
	char *pos, name[SMB_NAME_MAX];
	int rc, ret;
	smb_share_t *new_opts;

	assert(opts != NULL);
	*opts = NULL;

	/* Set defaults */
	new_opts = (smb_share_t *) malloc(sizeof (smb_share_t));
	if (new_opts == NULL)
		return (SA_NO_MEMORY);

	if (impl_share && impl_share->dataset) {
		strncpy(name, impl_share->dataset, sizeof (name));
		name [sizeof (name)-1] = '\0';

		/* Support ZFS share name regexp '[[:alnum:]_-.: ]' */
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

		strncpy(new_opts->name, name, sizeof (name));
		new_opts->name [sizeof (new_opts->name)-1] = '\0';

		ret = snprintf(new_opts->comment, sizeof (new_opts->comment),
				"Dataset name: %s", impl_share->dataset);
		if (ret < 0 || ret >= sizeof (new_opts->comment))
			return (SA_SYSTEM_ERR);
	} else {
		new_opts->name[0] = '\0';
		new_opts->comment[0] = '\0';
	}

	new_opts->writeable = B_TRUE;
	new_opts->guest_ok = B_TRUE;
	*opts = new_opts;

	rc = foreach_shareopt(shareopts, smb_get_shareopts_cb, *opts);
	if (rc != SA_OK) {
		free(*opts);
		*opts = NULL;
	}

	return (rc);
}

/*
 * Used internally by smb_enable_share to enable sharing for a single host.
 */
static int
smb_enable_share_one(sa_share_impl_t impl_share)
{
	char *argv[11], *shareopts;
	smb_share_t *opts;
	int rc;

#ifdef DEBUG
	fprintf(stderr, "smb_enable_share_one: dataset=%s, path=%s\n",
		impl_share->dataset, impl_share->sharepath);
#endif

	opts = (smb_share_t *) malloc(sizeof (smb_share_t));
	if (opts == NULL)
		return (SA_NO_MEMORY);

	/* Get any share options */
	shareopts = FSINFO(impl_share, smb_fstype)->shareopts;
	rc = smb_get_shareopts(impl_share, shareopts, &opts);
	if (rc < 0) {
		free(opts);
		return (SA_SYSTEM_ERR);
	}

#ifdef DEBUG
	fprintf(stderr, "smb_enable_share_one: shareopts=%s, name=%s, "
		"comment=\"%s\", writeable=%d, guest_ok=%d\n", shareopts,
		opts->name, opts->comment, opts->writeable, opts->guest_ok);
#endif

	/* ====== */
	/* PART 1 - do the (inital) share. */
	/* CMD: net -S NET_CMD_ARG_HOST conf addshare <sharename> \ */
	/*	<path> [writeable={y|n} [guest_ok={y|n} [<comment>]] */
	argv[0]  = NET_CMD_PATH;
	argv[1]  = (char *)"-S";
	argv[2]  = NET_CMD_ARG_HOST;
	argv[3]  = (char *)"conf";
	argv[4]  = (char *)"addshare";
	argv[5]  = (char *)opts->name;
	argv[6]  = (char *)impl_share->sharepath;
	if (opts->writeable)
		argv[7]  = (char *)"writeable=y";
	else
		argv[7]  = (char *)"writeable=n";
	if (opts->guest_ok)
		argv[8]  = (char *)"guest_ok=y";
	else
		argv[8]  = (char *)"guest_ok=n";
	argv[9]  = (char *)opts->comment;
	argv[10] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 10; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0)
		return (SA_SYSTEM_ERR);

	/* ====== */
	/* PART 2 - Run local update script. */
	if (access(EXTRA_SMBFS_SHARE_SCRIPT, X_OK) == 0) {
		argv[0] = (char *)EXTRA_SMBFS_SHARE_SCRIPT;
		argv[1] = opts->name;
		argv[2] = NULL;

		rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
		if (rc != 0) {
			free(opts);
			return (SA_SYSTEM_ERR);
		}
	}

	free(opts);
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
	return (smb_enable_share_one(impl_share));
}

/*
 * Used internally by smb_disable_share to disable sharing for a single host.
 */
static int
smb_disable_share_one(const char *sharename)
{
	int rc;
	char *argv[7];

#ifdef DEBUG
	fprintf(stderr, "smb_disable_share_one: Disabling share %s\n",
		sharename);
#endif

	/* CMD: net -S NET_CMD_ARG_HOST conf delshare Test1 */
	argv[0] = NET_CMD_PATH;
	argv[1] = (char *)"-S";
	argv[2] = NET_CMD_ARG_HOST;
	argv[3] = (char *)"conf";
	argv[4] = (char *)"delshare";
	argv[5] = strdup(sharename);
	argv[6] = NULL;

#ifdef DEBUG
	int i;
	fprintf(stderr, "CMD: ");
	for (i = 0; i < 6; i++)
		fprintf(stderr, "%s ", argv[i]);
	fprintf(stderr, "\n");
#endif

	rc = libzfs_run_process(argv[0], argv, STDERR_VERBOSE);
	if (rc != 0)
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
	int ret;
	smb_share_t *share;

	if (!smb_available()) {
		/*
		 * The share can't possibly be active, so nothing
		 * needs to be done to disable it.
		 */
		return (SA_OK);
	}

	/* Retrieve the list of (possible) active shares */
	smb_retrieve_shares();
	for (share = list_head(&all_smb_shares_list);
		share != NULL;
		share = list_next(&all_smb_shares_list, share)) {
#ifdef DEBUG
		fprintf(stderr, "smb_disable_share: %s ?? %s (%s)\n",
			impl_share->sharepath, share->path, share->name);
#endif
		if (strcmp(impl_share->sharepath, share->path) == 0) {
#ifdef DEBUG
			fprintf(stderr, "=> disable %s (%s)\n", share->name,
				share->path);
#endif
			if ((ret = smb_disable_share_one(share->name))
			    == SA_OK)
				list_remove(&all_smb_shares_list, share);
			return (ret);
		}
	}

	return (SA_OK);
}

/*
 * Checks whether the specified SMB share options are syntactically correct.
 */
static int
smb_validate_shareopts(const char *shareopts)
{
	smb_share_t *opts;

	return (smb_get_shareopts(NULL, shareopts, &opts));
}

/*
 * Checks whether a share is currently active.
 */
static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	smb_share_t *share;

	if (!smb_available())
		return (B_FALSE);

	/* Retrieve the list of (possible) active shares */
	smb_retrieve_shares();
	for (share = list_head(&all_smb_shares_list); share != NULL;
		share = list_next(&all_smb_shares_list, share)) {
#ifdef DEBUG
		fprintf(stderr, "smb_is_share_active: %s ?? %s\n",
			impl_share->sharepath, share->path);
#endif
		if (strcmp(impl_share->sharepath, share->path) == 0) {
#ifdef DEBUG
			fprintf(stderr, "=> %s is active\n", share->name);
#endif
			return (B_TRUE);
		}
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

	if (impl_share->dataset == NULL)
		return (B_FALSE);

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
	/* If we got past libshare_smb_init(), then it is available! */
	return (B_TRUE);
}

/*
 * Initializes the SMB functionality of libshare.
 */
void
libshare_smb_init(void)
{
	int rc;
	char *argv[5];

	if (access(NET_CMD_PATH, X_OK) == 0) {
		/* The net command exists, now Check samba access */
		argv[0]  = NET_CMD_PATH;
		argv[1]  = (char *)"-S";
		argv[2]  = NET_CMD_ARG_HOST;
		argv[3]  = (char *)"time";
		argv[4] = NULL;

		rc = libzfs_run_process(argv[0], argv, 0);
		if (rc != 255)
			smb_fstype = register_fstype("smb", &smb_shareops);
#ifdef DEBUG
		else
			fprintf(stderr, "ERROR: %s can't talk to samba.\n",
				NET_CMD_PATH);
	} else {
		fprintf(stderr, "ERROR: %s does not exist or not executable\n",
			NET_CMD_PATH);
#endif
	}
}
