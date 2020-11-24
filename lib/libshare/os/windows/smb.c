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
 * Copyright (c) 2016 Jorgen Lundman <lundman@lundman.net>, based on nfs.c
 *                         by Gunnar Beutner
 *
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
#include <ctype.h>
#include <sys/socket.h>
#include "libshare_impl.h"
#include "smb.h"

static boolean_t smb_available(void);

static sa_fstype_t *smb_fstype;

#define	SMB_NAME_MAX		255

#define	SHARING_CMD_PATH		"/usr/sbin/sharing"

typedef struct smb_share_s {
	char name[SMB_NAME_MAX];	/* Share name */
	char path[PATH_MAX];		/* Share path */
	boolean_t guest_ok;		    /* boolean */
	struct smb_share_s *next;
} smb_share_t;

smb_share_t *smb_shares = NULL;

/*
 * Parse out a "value" part of a "line" of input. By skipping white space.
 * If line ends up being empty, read the next line, skipping white spare.
 * strdup() value before returning.
 */
static int get_attribute(const char *attr, char *line, char **value, FILE *file)
{
	char *r = line;
	char line2[512];

	if (strncasecmp((char *)attr, line, strlen(attr))) return 0;

	r += strlen(attr);

	//fprintf(stderr, "ZFS: matched '%s' in '%s'\r\n", attr, line);

	while(isspace(*r)) r++; // Skip whitespace

	// Nothing left? Read next line
	if (!*r) {
		if (!fgets(line2, sizeof(line2), file)) return 0;
		// Eat newlines
		if ((r = strchr(line2, '\r'))) *r = 0;
		if ((r = strchr(line2, '\n'))) *r = 0;
		// Parse new input
		r = line2;
		while(isspace(*r)) r++; // Skip whitespace
	}

	// Did we get something?
	if (*r) {
		*value = strdup(r);
		return 1;
	}
	return 0;
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
	return (SA_OK);
}

/*
 * Used internally by smb_enable_share to enable sharing for a single host.
 */
static int
smb_enable_share_one(const char *sharename, const char *sharepath)
{
	return (SA_OK);
}

/*
 * Enables SMB sharing for the specified share.
 */
static int
smb_enable_share(sa_share_impl_t impl_share)
{
	return SA_OK;
}

/*
 * Used internally by smb_disable_share to disable sharing for a single host.
 */
static int
smb_disable_share_one(const char *sharename, int afpshared)
{
	return (SA_OK);
}

/*
 * Disables SMB sharing for the specified share.
 */
static int
smb_disable_share(sa_share_impl_t impl_share)
{
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
 * Checks whether a share is currently active. Called from libzfs_mount
 */
boolean_t smb_is_mountpoint_active(const char *mountpoint)
{
	return (B_FALSE);
}

static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	return smb_is_mountpoint_active(impl_share->sharepath);
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

	if (access(SHARING_CMD_PATH, F_OK) != 0)
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
