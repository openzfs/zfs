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
#include <errno.h>
#include <strings.h>
#include <libintl.h>
#include <sys/file.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libzfs_impl.h"
#include "libshare_impl.h"
#include "nfs.h"
#include "smb.h"

static sa_share_impl_t alloc_share(const char *zfsname, const char *path);
static void free_share(sa_share_impl_t share);

static int fstypes_count;
static sa_fstype_t *fstypes;

sa_fstype_t *
register_fstype(const char *name, const sa_share_ops_t *ops)
{
	sa_fstype_t *fstype;

	fstype = calloc(1, sizeof (sa_fstype_t));

	if (fstype == NULL)
		return (NULL);

	fstype->name = name;
	fstype->ops = ops;
	fstype->fsinfo_index = fstypes_count;

	fstypes_count++;

	fstype->next = fstypes;
	fstypes = fstype;

	return (fstype);
}

__attribute__((constructor)) static void
libshare_init(void)
{
	libshare_nfs_init();
	libshare_smb_init();
}

int
sa_enable_share(const char *zfsname, const char *mountpoint,
    const char *shareopts, char *protocol)
{
	int rc, ret = SA_OK;
	boolean_t found_protocol = B_FALSE;
	sa_fstype_t *fstype;

	sa_share_impl_t impl_share = alloc_share(zfsname, mountpoint);
	if (impl_share == NULL)
		return (SA_NO_MEMORY);

	fstype = fstypes;
	while (fstype != NULL) {
		if (strcmp(fstype->name, protocol) == 0) {

			rc = fstype->ops->update_shareopts(impl_share,
			    shareopts);
			if (rc != SA_OK)
				break;

			rc = fstype->ops->enable_share(impl_share);
			if (rc != SA_OK)
				ret = rc;

			found_protocol = B_TRUE;
		}

		fstype = fstype->next;
	}
	free_share(impl_share);

	return (found_protocol ? ret : SA_INVALID_PROTOCOL);
}

int
sa_disable_share(const char *mountpoint, char *protocol)
{
	int rc, ret = SA_OK;
	boolean_t found_protocol = B_FALSE;
	sa_fstype_t *fstype;

	sa_share_impl_t impl_share = alloc_share(NULL, mountpoint);
	if (impl_share == NULL)
		return (SA_NO_MEMORY);

	fstype = fstypes;
	while (fstype != NULL) {
		if (strcmp(fstype->name, protocol) == 0) {

			rc = fstype->ops->disable_share(impl_share);
			if (rc != SA_OK)
				ret = rc;

			found_protocol = B_TRUE;
		}

		fstype = fstype->next;
	}
	free_share(impl_share);

	return (found_protocol ? ret : SA_INVALID_PROTOCOL);
}

boolean_t
sa_is_shared(const char *mountpoint, char *protocol)
{
	sa_fstype_t *fstype;
	boolean_t ret = B_FALSE;

	/* guid value is not used */
	sa_share_impl_t impl_share = alloc_share(NULL, mountpoint);
	if (impl_share == NULL)
		return (B_FALSE);

	fstype = fstypes;
	while (fstype != NULL) {
		if (strcmp(fstype->name, protocol) == 0) {
			ret = fstype->ops->is_shared(impl_share);
		}
		fstype = fstype->next;
	}
	free_share(impl_share);
	return (ret);
}

void
sa_commit_shares(const char *protocol)
{
	sa_fstype_t *fstype = fstypes;
	while (fstype != NULL) {
		if (strcmp(fstype->name, protocol) == 0)
			fstype->ops->commit_shares();
		fstype = fstype->next;
	}
}

/*
 * sa_errorstr(err)
 *
 * convert an error value to an error string
 */
char *
sa_errorstr(int err)
{
	static char errstr[32];
	char *ret = NULL;

	switch (err) {
	case SA_OK:
		ret = dgettext(TEXT_DOMAIN, "ok");
		break;
	case SA_NO_SUCH_PATH:
		ret = dgettext(TEXT_DOMAIN, "path doesn't exist");
		break;
	case SA_NO_MEMORY:
		ret = dgettext(TEXT_DOMAIN, "no memory");
		break;
	case SA_DUPLICATE_NAME:
		ret = dgettext(TEXT_DOMAIN, "name in use");
		break;
	case SA_BAD_PATH:
		ret = dgettext(TEXT_DOMAIN, "bad path");
		break;
	case SA_NO_SUCH_GROUP:
		ret = dgettext(TEXT_DOMAIN, "no such group");
		break;
	case SA_CONFIG_ERR:
		ret = dgettext(TEXT_DOMAIN, "configuration error");
		break;
	case SA_SYSTEM_ERR:
		ret = dgettext(TEXT_DOMAIN, "system error");
		break;
	case SA_SYNTAX_ERR:
		ret = dgettext(TEXT_DOMAIN, "syntax error");
		break;
	case SA_NO_PERMISSION:
		ret = dgettext(TEXT_DOMAIN, "no permission");
		break;
	case SA_BUSY:
		ret = dgettext(TEXT_DOMAIN, "busy");
		break;
	case SA_NO_SUCH_PROP:
		ret = dgettext(TEXT_DOMAIN, "no such property");
		break;
	case SA_INVALID_NAME:
		ret = dgettext(TEXT_DOMAIN, "invalid name");
		break;
	case SA_INVALID_PROTOCOL:
		ret = dgettext(TEXT_DOMAIN, "invalid protocol");
		break;
	case SA_NOT_ALLOWED:
		ret = dgettext(TEXT_DOMAIN, "operation not allowed");
		break;
	case SA_BAD_VALUE:
		ret = dgettext(TEXT_DOMAIN, "bad property value");
		break;
	case SA_INVALID_SECURITY:
		ret = dgettext(TEXT_DOMAIN, "invalid security type");
		break;
	case SA_NO_SUCH_SECURITY:
		ret = dgettext(TEXT_DOMAIN, "security type not found");
		break;
	case SA_VALUE_CONFLICT:
		ret = dgettext(TEXT_DOMAIN, "property value conflict");
		break;
	case SA_NOT_IMPLEMENTED:
		ret = dgettext(TEXT_DOMAIN, "not implemented");
		break;
	case SA_INVALID_PATH:
		ret = dgettext(TEXT_DOMAIN, "invalid path");
		break;
	case SA_NOT_SUPPORTED:
		ret = dgettext(TEXT_DOMAIN, "operation not supported");
		break;
	case SA_PROP_SHARE_ONLY:
		ret = dgettext(TEXT_DOMAIN, "property not valid for group");
		break;
	case SA_NOT_SHARED:
		ret = dgettext(TEXT_DOMAIN, "not shared");
		break;
	case SA_NO_SUCH_RESOURCE:
		ret = dgettext(TEXT_DOMAIN, "no such resource");
		break;
	case SA_RESOURCE_REQUIRED:
		ret = dgettext(TEXT_DOMAIN, "resource name required");
		break;
	case SA_MULTIPLE_ERROR:
		ret = dgettext(TEXT_DOMAIN, "errors from multiple protocols");
		break;
	case SA_PATH_IS_SUBDIR:
		ret = dgettext(TEXT_DOMAIN, "path is a subpath of share");
		break;
	case SA_PATH_IS_PARENTDIR:
		ret = dgettext(TEXT_DOMAIN, "path is parent of a share");
		break;
	case SA_NO_SECTION:
		ret = dgettext(TEXT_DOMAIN, "protocol requires a section");
		break;
	case SA_NO_PROPERTIES:
		ret = dgettext(TEXT_DOMAIN, "properties not found");
		break;
	case SA_NO_SUCH_SECTION:
		ret = dgettext(TEXT_DOMAIN, "section not found");
		break;
	case SA_PASSWORD_ENC:
		ret = dgettext(TEXT_DOMAIN, "passwords must be encrypted");
		break;
	case SA_SHARE_EXISTS:
		ret = dgettext(TEXT_DOMAIN, "path or file is already shared");
		break;
	default:
		(void) snprintf(errstr, sizeof (errstr),
		    dgettext(TEXT_DOMAIN, "unknown %d"), err);
		ret = errstr;
	}
	return (ret);
}

int
sa_validate_shareopts(char *options, char *proto)
{
	sa_fstype_t *fstype;

	fstype = fstypes;
	while (fstype != NULL) {
		if (strcmp(fstype->name, proto) != 0) {
			fstype = fstype->next;
			continue;
		}

		return (fstype->ops->validate_shareopts(options));
	}

	return (SA_INVALID_PROTOCOL);
}

static sa_share_impl_t
alloc_share(const char *zfsname, const char *mountpoint)
{
	sa_share_impl_t impl_share;

	impl_share = calloc(1, sizeof (struct sa_share_impl));

	if (impl_share == NULL)
		return (NULL);

	if (mountpoint != NULL &&
	    ((impl_share->sa_mountpoint = strdup(mountpoint)) == NULL)) {
		free(impl_share);
		return (NULL);
	}

	if (zfsname != NULL &&
	    ((impl_share->sa_zfsname = strdup(zfsname)) == NULL)) {
		free(impl_share->sa_mountpoint);
		free(impl_share);
		return (NULL);
	}

	impl_share->sa_fsinfo = calloc(fstypes_count,
	    sizeof (sa_share_fsinfo_t));
	if (impl_share->sa_fsinfo == NULL) {
		free(impl_share->sa_mountpoint);
		free(impl_share->sa_zfsname);
		free(impl_share);
		return (NULL);
	}

	return (impl_share);
}

static void
free_share(sa_share_impl_t impl_share)
{
	sa_fstype_t *fstype;

	fstype = fstypes;
	while (fstype != NULL) {
		fstype->ops->clear_shareopts(impl_share);
		fstype = fstype->next;
	}

	free(impl_share->sa_mountpoint);
	free(impl_share->sa_zfsname);
	free(impl_share->sa_fsinfo);
	free(impl_share);
}
