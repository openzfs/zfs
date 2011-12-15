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

int
smb_enable_share_one(void)
{
fprintf(stderr, "smb_enable_share_one()\n");
	return 0;
}

int
smb_enable_share(sa_share_impl_t impl_share)
{
fprintf(stderr, "smb_enable_share()\n");
	return 0;
}

int
smb_disable_share_one(void)
{
fprintf(stderr, "smb_disable_share_one()\n");
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
	return 0;
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
fprintf(stderr, "  smb_retrieve_shares()\n");
	return 1;
}

void
libshare_smb_init(void)
{
fprintf(stderr, "libshare_smb_init()\n");
	smb_available = (smb_retrieve_shares() == SA_OK);

	smb_fstype = register_fstype("smb", &smb_shareops);
}
