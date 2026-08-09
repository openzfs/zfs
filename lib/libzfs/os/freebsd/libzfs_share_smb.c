// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

/*
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <libzfs.h>
#include "../../libzfs_share.h"

/*
 * Enables SMB sharing for the specified share.
 */
static int
smb_enable_share(sa_share_impl_t impl_share)
{
	(void) impl_share;
	fputs("No SMB support in FreeBSD yet.\n", stderr);
	return (SA_NOT_SUPPORTED);
}
/*
 * Disables SMB sharing for the specified share.
 */
static int
smb_disable_share(sa_share_impl_t impl_share)
{
	(void) impl_share;
	fputs("No SMB support in FreeBSD yet.\n", stderr);
	return (SA_NOT_SUPPORTED);
}

/*
 * Checks whether the specified SMB share options are syntactically correct.
 */
static int
smb_validate_shareopts(const char *shareopts)
{
	(void) shareopts;
	fputs("No SMB support in FreeBSD yet.\n", stderr);
	return (SA_NOT_SUPPORTED);
}

/*
 * Checks whether a share is currently active.
 */
static boolean_t
smb_is_share_active(sa_share_impl_t impl_share)
{
	(void) impl_share;
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
