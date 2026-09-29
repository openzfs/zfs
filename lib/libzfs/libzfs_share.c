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
 * Copyright (c) 2002, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2011 Gunnar Beutner
 * Copyright (c) 2018, 2022 by Delphix. All rights reserved.
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
#include "libzfs_impl.h"

#define	init_share(zfsname, path, shareopts) \
	{ \
		.sa_zfsname = zfsname, \
		.sa_mountpoint = path, \
		.sa_shareopts = shareopts, \
	}

#define	VALIDATE_PROTOCOL(proto, ...) \
	if ((proto) < 0 || (proto) >= SA_PROTOCOL_COUNT) \
		return __VA_ARGS__

const char *const sa_protocol_names[SA_PROTOCOL_COUNT] = {
	[SA_PROTOCOL_NFS] = "nfs",
	[SA_PROTOCOL_SMB] = "smb",
};

static const sa_fstype_t *fstypes[SA_PROTOCOL_COUNT] =
	{&libshare_nfs_type, &libshare_smb_type};

int
sa_enable_share(const char *zfsname, const char *mountpoint,
    const char *shareopts, enum sa_protocol protocol)
{
	VALIDATE_PROTOCOL(protocol, SA_INVALID_PROTOCOL);

	int error = sa_validate_shareopts(shareopts, protocol);
	if (error != SA_OK)
		return (error);

	const struct sa_share_impl args =
	    init_share(zfsname, mountpoint, shareopts);
	return (fstypes[protocol]->enable_share(&args));
}

int
sa_disable_share(const char *mountpoint, enum sa_protocol protocol)
{
	VALIDATE_PROTOCOL(protocol, SA_INVALID_PROTOCOL);

	const struct sa_share_impl args = init_share(NULL, mountpoint, NULL);
	return (fstypes[protocol]->disable_share(&args));
}

boolean_t
sa_is_shared(const char *mountpoint, enum sa_protocol protocol)
{
	VALIDATE_PROTOCOL(protocol, B_FALSE);

	const struct sa_share_impl args = init_share(NULL, mountpoint, NULL);
	return (fstypes[protocol]->is_shared(&args));
}

void
sa_commit_shares(enum sa_protocol protocol)
{
	/* CSTYLED */
	VALIDATE_PROTOCOL(protocol, );

	fstypes[protocol]->commit_shares();
}

void
sa_truncate_shares(enum sa_protocol protocol)
{
	/* CSTYLED */
	VALIDATE_PROTOCOL(protocol, );

	if (fstypes[protocol]->truncate_shares != NULL)
		fstypes[protocol]->truncate_shares();
}

int
sa_validate_shareopts(const char *options, enum sa_protocol protocol)
{
	VALIDATE_PROTOCOL(protocol, SA_INVALID_PROTOCOL);

	/* error out on invalid characters */
	if (strpbrk(options, "\a\b\f\n\r") != NULL)
		return (SA_SYNTAX_ERR);

	return (fstypes[protocol]->validate_shareopts(options));
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
		return ("ok");
	case SA_NO_SUCH_PATH:
		return ("path doesn't exist");
	case SA_NO_MEMORY:
		return ("no memory");
	case SA_DUPLICATE_NAME:
		return ("name in use");
	case SA_BAD_PATH:
		return ("bad path");
	case SA_NO_SUCH_GROUP:
		return ("no such group");
	case SA_CONFIG_ERR:
		return ("configuration error");
	case SA_SYSTEM_ERR:
		return ("system error");
	case SA_SYNTAX_ERR:
		return ("syntax error");
	case SA_NO_PERMISSION:
		return ("no permission");
	case SA_BUSY:
		return ("busy");
	case SA_NO_SUCH_PROP:
		return ("no such property");
	case SA_INVALID_NAME:
		return ("invalid name");
	case SA_INVALID_PROTOCOL:
		return ("invalid protocol");
	case SA_NOT_ALLOWED:
		return ("operation not allowed");
	case SA_BAD_VALUE:
		return ("bad property value");
	case SA_INVALID_SECURITY:
		return ("invalid security type");
	case SA_NO_SUCH_SECURITY:
		return ("security type not found");
	case SA_VALUE_CONFLICT:
		return ("property value conflict");
	case SA_NOT_IMPLEMENTED:
		return ("not implemented");
	case SA_INVALID_PATH:
		return ("invalid path");
	case SA_NOT_SUPPORTED:
		return ("operation not supported");
	case SA_PROP_SHARE_ONLY:
		return ("property not valid for group");
	case SA_NOT_SHARED:
		return ("not shared");
	case SA_NO_SUCH_RESOURCE:
		return ("no such resource");
	case SA_RESOURCE_REQUIRED:
		return ("resource name required");
	case SA_MULTIPLE_ERROR:
		return (
		    "errors from multiple protocols");
	case SA_PATH_IS_SUBDIR:
		return ("path is a subpath of share");
	case SA_PATH_IS_PARENTDIR:
		return ("path is parent of a share");
	case SA_NO_SECTION:
		return ("protocol requires a section");
	case SA_NO_PROPERTIES:
		return ("properties not found");
	case SA_NO_SUCH_SECTION:
		return ("section not found");
	case SA_PASSWORD_ENC:
		return ("passwords must be encrypted");
	case SA_SHARE_EXISTS:
		return (
		    "path or file is already shared");
	default:
		(void) snprintf(errstr, sizeof (errstr),
		    "unknown %d", err);
		return (errstr);
	}
}
