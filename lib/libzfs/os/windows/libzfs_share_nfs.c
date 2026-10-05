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
 * Copyright (c) 2025 Jorgen Lundman <lundman@lundman.net>.
 */

#include <sys/param.h>
#include <sys/vfs.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <libintl.h>

#include "libzfs_impl.h"

static sa_fstype_t *nfs_fstype;

static int
nfs_enable_share(sa_share_impl_t impl_share)
{
	return (SA_OK);
}

static int
nfs_disable_share(sa_share_impl_t impl_share)
{
	return (SA_OK);
}

/*
 * NOTE: This function returns a static buffer and thus is not thread-safe.
 */
static boolean_t
nfs_is_shared(sa_share_impl_t impl_share)
{
	return (B_FALSE);
}

static int
nfs_validate_shareopts(const char *shareopts)
{
	return (SA_OK);
}

static int
nfs_commit_shares(void)
{
	return (SA_OK);
}

const sa_fstype_t libshare_nfs_type = {
	.enable_share = nfs_enable_share,
	.disable_share = nfs_disable_share,
	.is_shared = nfs_is_shared,

	.validate_shareopts = nfs_validate_shareopts,
	.commit_shares = nfs_commit_shares,
};
