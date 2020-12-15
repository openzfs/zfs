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
 * Copyright (c) 2020 by Delphix. All rights reserved.
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

static sa_fstype_t *smb_fstype;

/*
 * Enables SMB sharing for the specified share.
 */
static int
smb_enable_share(sa_share_impl_t impl_share)
{
	fprintf(stderr, "No SMB support in FreeBSD yet.\n");
	return (SA_NOT_SUPPORTED);
}
/*
 * Disables SMB sharing for the specified share.
 */
static int
smb_disable_share(sa_share_impl_t impl_share)
{
	fprintf(stderr, "No SMB support in FreeBSD yet.\n");
	return (SA_NOT_SUPPORTED);
}

/*
 * Checks whether the specified SMB share options are syntactically correct.
 */
static int
smb_validate_shareopts(const char *shareopts)
{
	fprintf(stderr, "No SMB support in FreeBSD yet.\n");
	return (SA_NOT_SUPPORTED);
}

/*
 * Checks whether a share is currently active.
 */
static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	return (B_FALSE);
}

/*
 * Called to update a share's options. A share's options might be out of
 * date if the share was loaded from disk and the "sharesmb" dataset
 * property has changed in the meantime. This function also takes care
 * of re-enabling the share if necessary.
 */
static int
smb_update_shareopts(sa_share_impl_t impl_share, const char *shareopts)
{
	return (SA_OK);
}

static int
smb_update_shares(void)
{
	/* Not implemented */
	return (0);
}
/*
 * Clears a share's SMB options. Used by libshare to
 * clean up shares that are about to be free()'d.
 */
static void
smb_clear_shareopts(sa_share_impl_t impl_share)
{
	FSINFO(impl_share, smb_fstype)->shareopts = NULL;
}

static const sa_share_ops_t smb_shareops = {
	.enable_share = smb_enable_share,
	.disable_share = smb_disable_share,
	.is_shared = smb_is_share_active,

	.validate_shareopts = smb_validate_shareopts,
	.update_shareopts = smb_update_shareopts,
	.clear_shareopts = smb_clear_shareopts,
	.commit_shares = smb_update_shares,
};

/*
 * Initializes the SMB functionality of libshare.
 */
void
libshare_smb_init(void)
{
	smb_fstype = register_fstype("smb", &smb_shareops);
}
