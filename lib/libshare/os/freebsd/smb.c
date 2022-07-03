/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#include <stdio.h>
#include <libshare.h>
#include "libshare_impl.h"

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
