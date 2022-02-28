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
 * Copyright (c) 2011 Gunnar Beutner
 * Copyright (c) 2018, 2020 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <libintl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"

#define	init_share(zfsname, path, shareopts) \
	{ \
		.sa_zfsname = zfsname, \
		.sa_mountpoint = path, \
		.sa_shareopts = shareopts, \
	}
#define	find_proto(pcol) \
	/* CSTYLED */ \
	({ \
		sa_fstype_t prot = { \
			.protocol = pcol, \
		}; \
		avl_find(&fstypes, &prot, NULL); \
	})

static avl_tree_t fstypes;

static int
fstypes_compar(const void *lhs, const void *rhs)
{
	const sa_fstype_t *l = lhs, *r = rhs;
	int cmp = strcmp(l->protocol, r->protocol);
	return ((0 < cmp) - (cmp < 0));
}

__attribute__((constructor)) static void
libshare_init(void)
{
	avl_create(&fstypes, fstypes_compar,
	    sizeof (sa_fstype_t), offsetof(sa_fstype_t, node));
	avl_add(&fstypes, &libshare_nfs_type);
	avl_add(&fstypes, &libshare_smb_type);
}

int
sa_enable_share(const char *zfsname, const char *mountpoint,
    const char *shareopts, const char *protocol)
{
	sa_fstype_t *fstype = find_proto(protocol);
	if (!fstype)
		return (SA_INVALID_PROTOCOL);

	const struct sa_share_impl args =
	    init_share(zfsname, mountpoint, shareopts);
	return (fstype->enable_share(&args));
}

int
sa_disable_share(const char *mountpoint, const char *protocol)
{
	sa_fstype_t *fstype = find_proto(protocol);
	if (!fstype)
		return (SA_INVALID_PROTOCOL);

	const struct sa_share_impl args = init_share(NULL, mountpoint, NULL);
	return (fstype->disable_share(&args));
}

boolean_t
sa_is_shared(const char *mountpoint, const char *protocol)
{
	sa_fstype_t *fstype = find_proto(protocol);
	if (!fstype)
		return (B_FALSE);

	const struct sa_share_impl args = init_share(NULL, mountpoint, NULL);
	return (fstype->is_shared(&args));
}

void
sa_commit_shares(const char *protocol)
{
	sa_fstype_t *fstype = find_proto(protocol);
	if (!fstype)
		return;

	fstype->commit_shares();
}

/*
 * sa_errorstr(err)
 *
 * convert an error value to an error string
 */
const char *
sa_errorstr(int err)
{
	static char errstr[32];

	switch (err) {
	case SA_OK:
		return (dgettext(TEXT_DOMAIN, "ok"));
	case SA_NO_SUCH_PATH:
		return (dgettext(TEXT_DOMAIN, "path doesn't exist"));
	case SA_NO_MEMORY:
		return (dgettext(TEXT_DOMAIN, "no memory"));
	case SA_DUPLICATE_NAME:
		return (dgettext(TEXT_DOMAIN, "name in use"));
	case SA_BAD_PATH:
		return (dgettext(TEXT_DOMAIN, "bad path"));
	case SA_NO_SUCH_GROUP:
		return (dgettext(TEXT_DOMAIN, "no such group"));
	case SA_CONFIG_ERR:
		return (dgettext(TEXT_DOMAIN, "configuration error"));
	case SA_SYSTEM_ERR:
		return (dgettext(TEXT_DOMAIN, "system error"));
	case SA_SYNTAX_ERR:
		return (dgettext(TEXT_DOMAIN, "syntax error"));
	case SA_NO_PERMISSION:
		return (dgettext(TEXT_DOMAIN, "no permission"));
	case SA_BUSY:
		return (dgettext(TEXT_DOMAIN, "busy"));
	case SA_NO_SUCH_PROP:
		return (dgettext(TEXT_DOMAIN, "no such property"));
	case SA_INVALID_NAME:
		return (dgettext(TEXT_DOMAIN, "invalid name"));
	case SA_INVALID_PROTOCOL:
		return (dgettext(TEXT_DOMAIN, "invalid protocol"));
	case SA_NOT_ALLOWED:
		return (dgettext(TEXT_DOMAIN, "operation not allowed"));
	case SA_BAD_VALUE:
		return (dgettext(TEXT_DOMAIN, "bad property value"));
	case SA_INVALID_SECURITY:
		return (dgettext(TEXT_DOMAIN, "invalid security type"));
	case SA_NO_SUCH_SECURITY:
		return (dgettext(TEXT_DOMAIN, "security type not found"));
	case SA_VALUE_CONFLICT:
		return (dgettext(TEXT_DOMAIN, "property value conflict"));
	case SA_NOT_IMPLEMENTED:
		return (dgettext(TEXT_DOMAIN, "not implemented"));
	case SA_INVALID_PATH:
		return (dgettext(TEXT_DOMAIN, "invalid path"));
	case SA_NOT_SUPPORTED:
		return (dgettext(TEXT_DOMAIN, "operation not supported"));
	case SA_PROP_SHARE_ONLY:
		return (dgettext(TEXT_DOMAIN, "property not valid for group"));
	case SA_NOT_SHARED:
		return (dgettext(TEXT_DOMAIN, "not shared"));
	case SA_NO_SUCH_RESOURCE:
		return (dgettext(TEXT_DOMAIN, "no such resource"));
	case SA_RESOURCE_REQUIRED:
		return (dgettext(TEXT_DOMAIN, "resource name required"));
	case SA_MULTIPLE_ERROR:
		return (dgettext(TEXT_DOMAIN,
		    "errors from multiple protocols"));
	case SA_PATH_IS_SUBDIR:
		return (dgettext(TEXT_DOMAIN, "path is a subpath of share"));
	case SA_PATH_IS_PARENTDIR:
		return (dgettext(TEXT_DOMAIN, "path is parent of a share"));
	case SA_NO_SECTION:
		return (dgettext(TEXT_DOMAIN, "protocol requires a section"));
	case SA_NO_PROPERTIES:
		return (dgettext(TEXT_DOMAIN, "properties not found"));
	case SA_NO_SUCH_SECTION:
		return (dgettext(TEXT_DOMAIN, "section not found"));
	case SA_PASSWORD_ENC:
		return (dgettext(TEXT_DOMAIN, "passwords must be encrypted"));
	case SA_SHARE_EXISTS:
		return (dgettext(TEXT_DOMAIN,
		    "path or file is already shared"));
	default:
		(void) snprintf(errstr, sizeof (errstr),
		    dgettext(TEXT_DOMAIN, "unknown %d"), err);
		return (errstr);
	}
}

int
sa_validate_shareopts(const char *options, const char *protocol)
{
	sa_fstype_t *fstype = find_proto(protocol);
	if (!fstype)
		return (SA_INVALID_PROTOCOL);

	return (fstype->validate_shareopts(options));
}
