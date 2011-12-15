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
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "smb.h"

static sa_fstype_t *smb_fstype;
boolean_t smb_available;

int smb_retrieve_shares(void);

/*
 * By Jerome Bettis
 * http://ubuntuforums.org/showthread.php?t=141670
 */
static void
strrep(char *str, char old, char new)
{
	char *pos;

	if (new == old)
		return;

	pos = strchr(str, old);
	while (pos != NULL)  {
		*pos = new;
		pos = strchr(pos + 1, old);
	}
}

int
smb_enable_share_one(const char *sharename, const char *sharepath)
{
	char *argv[12], name[255], comment[255];
	int rc;

// DEBUG
fprintf(stderr, "    smb_enable_share_one(%s, %s)\n",
	sharename, sharepath);

	/* Remove the slash(es) in the share name */
	strncpy(name, sharename, sizeof(name));
	strrep(name, '/', '_');

	/*
	 * CMD: net -U root -S 127.0.0.1 usershare add Test1 /share/Test1 "Comment" "Everyone:F"
	 */

	snprintf(comment, sizeof (comment), "Comment: %s", sharepath);

	argv[0]  = "/usr/bin/net";
	argv[1]  = "-U";
	argv[2]  = "root";
	argv[3]  = "-S";
	argv[4]  = "127.0.0.1";
	argv[5]  = "usershare";
	argv[6]  = "add";
	argv[7]  = name;
	argv[8]  = strdup(sharepath);
	argv[9]  = comment;
	argv[10] = "Everyone:F";
	argv[11] = NULL;

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

// DEBUG
fprintf(stderr, "smb_enable_share(): dataset=%s\n", impl_share->dataset);

	if (!smb_available) {
// DEBUG
fprintf(stderr, "  smb_enable_share(): -> !smb_available\n");
		return SA_SYSTEM_ERR;
	}

	shareopts = FSINFO(impl_share, smb_fstype)->shareopts;
	if (shareopts == NULL) { /* on/off */
// DEBUG
fprintf(stderr, "  smb_enable_share(): -> SA_SYSTEM_ERR\n");
		return SA_SYSTEM_ERR;
	}

	if (strcmp(shareopts, "off") == 0) {
// DEBUG
fprintf(stderr, "  smb_enable_share(): -> off (0)\n");
		return (0);
	}

	/* Magic: Enable (i.e., 'create new') share */
	return smb_enable_share_one(impl_share->dataset,
		impl_share->sharepath);
}

int
smb_disable_share_one(int sid)
{
	int rc;
	char *argv[6];

// DEBUG
fprintf(stderr, "smb_disable_share_one()\n");

	/* CMD: net -U root -S 127.0.0.1 usershare delete Test1 */

	return 0;
}

int
smb_disable_share(sa_share_impl_t impl_share)
{
fprintf(stderr, "smb_disable_share()\n");
	return 0;
}

static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
fprintf(stderr, "smb_is_share_active()\n");
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
// DEBUG
fprintf(stderr, "smb_update_shareopts()\n");

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
// DEBUG
fprintf(stderr, "smb_clear_shareopts()\n");
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
// DEBUG
fprintf(stderr, "  smb_retrieve_shares()\n");

	/* CMD: net usershare list -l */

	return rc;
}

void
libshare_smb_init(void)
{
// DEBUG
fprintf(stderr, "libshare_smb_init()\n");
	smb_available = (smb_retrieve_shares() == SA_OK);

	smb_fstype = register_fstype("smb", &smb_shareops);
}
