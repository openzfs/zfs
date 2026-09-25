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
