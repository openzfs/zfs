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
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <strings.h>
#include <libintl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <libzfs.h>
#include <libshare.h>
#include "libshare_impl.h"
#include "nfs.h"
#include "smb.h"

static sa_share_impl_t find_share(sa_handle_impl_t handle,
    const char *sharepath);
static sa_share_impl_t alloc_share(const char *sharepath);
static void free_share(sa_share_impl_t share);

static void parse_sharetab(sa_handle_impl_t impl_handle);
static int process_share(sa_handle_impl_t impl_handle,
    sa_share_impl_t impl_share, char *pathname, char *resource,
    char *fstype, char *options, char *description,
    char *dataset, boolean_t from_sharetab);
static void update_sharetab(sa_handle_impl_t impl_handle);

static int update_zfs_share(sa_share_impl_t impl_handle, const char *proto);
static int update_zfs_shares(sa_handle_impl_t impl_handle, const char *proto);

static int fstypes_count;
static sa_fstype_t *fstypes;

sa_fstype_t *
register_fstype(const char *name, const sa_share_ops_t *ops)
{
	sa_fstype_t *fstype;

	fstype = calloc(sizeof (sa_fstype_t), 1);

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

sa_handle_t
sa_init(int init_service)
{
	sa_handle_impl_t impl_handle;

	impl_handle = calloc(sizeof (struct sa_handle_impl), 1);

	if (impl_handle == NULL)
		return (NULL);

	impl_handle->zfs_libhandle = libzfs_init();

	if (impl_handle->zfs_libhandle != NULL) {
		libzfs_print_on_error(impl_handle->zfs_libhandle, B_TRUE);
	}

	parse_sharetab(impl_handle);
	update_zfs_shares(impl_handle, NULL);

	return ((sa_handle_t)impl_handle);
}

__attribute__((constructor)) static void
libshare_init(void)
{
	libshare_nfs_init();
	libshare_smb_init();
}

static void
parse_sharetab(sa_handle_impl_t impl_handle) {
	FILE *fp;
	char line[512];
	char *eol, *pathname, *resource, *fstype, *options, *description;

	fp = fopen("/etc/dfs/sharetab", "r");

	if (fp == NULL)
		return;

	while (fgets(line, sizeof (line), fp) != NULL) {
		eol = line + strlen(line) - 1;

		while (eol >= line) {
			if (*eol != '\r' && *eol != '\n')
				break;

			*eol = '\0';
			eol--;
		}

		pathname = line;

		if ((resource = strchr(pathname, '\t')) == NULL)
			continue;

		*resource = '\0';
		resource++;

		if ((fstype = strchr(resource, '\t')) == NULL)
			continue;

		*fstype = '\0';
		fstype++;

		if ((options = strchr(fstype, '\t')) == NULL)
			continue;

		*options = '\0';
		options++;

		if ((description = strchr(fstype, '\t')) != NULL) {
			*description = '\0';
			description++;
		}

		if (strcmp(resource, "-") == 0)
			resource = NULL;

		(void) process_share(impl_handle, NULL, pathname, resource,
		    fstype, options, description, NULL, B_TRUE);
	}

	fclose(fp);
}

static void
update_sharetab(sa_handle_impl_t impl_handle)
{
	sa_share_impl_t impl_share;
	int temp_fd;
	FILE *temp_fp;
	char tempfile[] = "/etc/dfs/sharetab.XXXXXX";
	sa_fstype_t *fstype;
	const char *resource;

	if (mkdir("/etc/dfs", 0755) < 0 && errno != EEXIST) {
		return;
	}

	temp_fd = mkstemp(tempfile);

	if (temp_fd < 0)
		return;

	temp_fp = fdopen(temp_fd, "w");

	if (temp_fp == NULL)
		return;

	impl_share = impl_handle->shares;
	while (impl_share != NULL) {
		fstype = fstypes;
		while (fstype != NULL) {
			if (FSINFO(impl_share, fstype)->active &&
			    FSINFO(impl_share, fstype)->shareopts != NULL) {
				resource = FSINFO(impl_share, fstype)->resource;

				if (resource == NULL)
					resource = "-";

				fprintf(temp_fp, "%s\t%s\t%s\t%s\n",
				    impl_share->sharepath, resource,
				    fstype->name,
				    FSINFO(impl_share, fstype)->shareopts);
			}

			fstype = fstype->next;
		}

		impl_share = impl_share->next;
	}

	fflush(temp_fp);
	fsync(temp_fd);
	fclose(temp_fp);

	rename(tempfile, "/etc/dfs/sharetab");
}

typedef struct update_cookie_s {
	sa_handle_impl_t handle;
	const char *proto;
} update_cookie_t;

static int
update_zfs_shares_cb(zfs_handle_t *zhp, void *pcookie)
{
	update_cookie_t *udata = (update_cookie_t *)pcookie;
	char mountpoint[ZFS_MAXPROPLEN];
	char shareopts[ZFS_MAXPROPLEN];
	char *dataset;
	zfs_type_t type = zfs_get_type(zhp);

	if (type == ZFS_TYPE_FILESYSTEM &&
	    zfs_iter_filesystems(zhp, update_zfs_shares_cb, pcookie) != 0) {
		zfs_close(zhp);
		return (1);
	}

	if (type != ZFS_TYPE_FILESYSTEM) {
		zfs_close(zhp);
		return (0);
	}

	if (zfs_prop_get(zhp, ZFS_PROP_MOUNTPOINT, mountpoint,
	    sizeof (mountpoint), NULL, NULL, 0, B_FALSE) != 0) {
		zfs_close(zhp);
		return (0);
	}

	dataset = (char *)zfs_get_name(zhp);

	if (dataset == NULL) {
		zfs_close(zhp);
		return (0);
	}

	if (!zfs_is_mounted(zhp, NULL)) {
		zfs_close(zhp);
		return (0);
	}

	if ((udata->proto == NULL || strcmp(udata->proto, "nfs") == 0) &&
	    zfs_prop_get(zhp, ZFS_PROP_SHARENFS, shareopts,
	    sizeof (shareopts), NULL, NULL, 0, B_FALSE) == 0 &&
	    strcmp(shareopts, "off") != 0) {
		(void) process_share(udata->handle, NULL, mountpoint, NULL,
		    "nfs", shareopts, NULL, dataset, B_FALSE);
	}

	if ((udata->proto == NULL || strcmp(udata->proto, "smb") == 0) &&
	    zfs_prop_get(zhp, ZFS_PROP_SHARESMB, shareopts,
	    sizeof (shareopts), NULL, NULL, 0, B_FALSE) == 0 &&
	    strcmp(shareopts, "off") != 0) {
		(void) process_share(udata->handle, NULL, mountpoint, NULL,
		    "smb", shareopts, NULL, dataset, B_FALSE);
	}

	zfs_close(zhp);

	return (0);
}

static int
update_zfs_share(sa_share_impl_t impl_share, const char *proto)
{
	sa_handle_impl_t impl_handle = impl_share->handle;
	zfs_handle_t *zhp;
	update_cookie_t udata;

	if (impl_handle->zfs_libhandle == NULL)
			return (SA_SYSTEM_ERR);

	assert(impl_share->dataset != NULL);

	zhp = zfs_open(impl_share->handle->zfs_libhandle, impl_share->dataset,
	    ZFS_TYPE_FILESYSTEM);

	if (zhp == NULL)
		return (SA_SYSTEM_ERR);

	udata.handle = impl_handle;
	udata.proto = proto;
	(void) update_zfs_shares_cb(zhp, &udata);

	return (SA_OK);
}

static int
update_zfs_shares(sa_handle_impl_t impl_handle, const char *proto)
{
	update_cookie_t udata;

	if (impl_handle->zfs_libhandle == NULL)
		return (SA_SYSTEM_ERR);

	udata.handle = impl_handle;
	udata.proto = proto;
	(void) zfs_iter_root(impl_handle->zfs_libhandle, update_zfs_shares_cb,
	    &udata);

	return (SA_OK);
}

static int
process_share(sa_handle_impl_t impl_handle, sa_share_impl_t impl_share,
    char *pathname, char *resource, char *proto,
    char *options, char *description, char *dataset,
    boolean_t from_sharetab)
{
	struct stat statbuf;
	int rc;
	char *resource_dup = NULL, *dataset_dup = NULL;
	boolean_t new_share;
	sa_fstype_t *fstype;

	new_share = B_FALSE;

	if (impl_share == NULL)
		impl_share = find_share(impl_handle, pathname);

	if (impl_share == NULL) {
		if (lstat(pathname, &statbuf) != 0 ||
		    !S_ISDIR(statbuf.st_mode))
			return (SA_BAD_PATH);

		impl_share = alloc_share(pathname);

		if (impl_share == NULL) {
			rc = SA_NO_MEMORY;
			goto err;
		}

		new_share = B_TRUE;
	}

	if (dataset != NULL) {
		dataset_dup = strdup(dataset);

		if (dataset_dup == NULL) {
			rc = SA_NO_MEMORY;
			goto err;
		}
	}

	free(impl_share->dataset);
	impl_share->dataset = dataset_dup;

	rc = SA_INVALID_PROTOCOL;

	fstype = fstypes;
	while (fstype != NULL) {
		if (strcmp(fstype->name, proto) == 0) {
			if (resource != NULL) {
				resource_dup = strdup(resource);

				if (resource_dup == NULL) {
					rc = SA_NO_MEMORY;
					goto err;
				}
			}

			free(FSINFO(impl_share, fstype)->resource);
			FSINFO(impl_share, fstype)->resource = resource_dup;

			rc = fstype->ops->update_shareopts(impl_share,
			    resource, options);

			if (rc == SA_OK && from_sharetab)
				FSINFO(impl_share, fstype)->active = B_TRUE;

			break;
		}

		fstype = fstype->next;
	}

	if (rc != SA_OK)
		goto err;

	if (new_share) {
		impl_share->handle = impl_handle;

		impl_share->next = impl_handle->shares;
		impl_handle->shares = impl_share;

	}

err:
	if (rc != SA_OK) {
		if (new_share)
			free_share(impl_share);
	}

	return (rc);
}

void
sa_fini(sa_handle_t handle)
{
	sa_handle_impl_t impl_handle = (sa_handle_impl_t)handle;
	sa_share_impl_t impl_share, next;
	sa_share_impl_t *pcurr;

	if (impl_handle == NULL)
		return;

	/*
	 * clean up shares which don't have a non-NULL dataset property,
	 * which means they're in sharetab but we couldn't find their
	 * ZFS dataset.
	 */
	pcurr = &(impl_handle->shares);
	impl_share = *pcurr;
	while (impl_share != NULL) {
		next = impl_share->next;

		if (impl_share->dataset == NULL) {
			/* remove item from the linked list */
			*pcurr = next;

			sa_disable_share(impl_share, NULL);

			free_share(impl_share);
		} else {
			pcurr = &(impl_share->next);
		}

		impl_share = next;
	}

	update_sharetab(impl_handle);

	if (impl_handle->zfs_libhandle != NULL)
		libzfs_fini(impl_handle->zfs_libhandle);

	impl_share = impl_handle->shares;
	while (impl_share != NULL) {
		next = impl_share->next;
		free_share(impl_share);
		impl_share = next;
	}

	free(impl_handle);
}

static sa_share_impl_t
find_share(sa_handle_impl_t impl_handle, const char *sharepath)
{
	sa_share_impl_t impl_share;

	impl_share = impl_handle->shares;
	while (impl_share != NULL) {
		if (strcmp(impl_share->sharepath, sharepath) == 0) {
			break;
		}

		impl_share = impl_share->next;
	}

	return (impl_share);
}

sa_share_t
sa_find_share(sa_handle_t handle, char *sharepath)
{
	return ((sa_share_t)find_share((sa_handle_impl_t)handle, sharepath));
}

int
sa_enable_share(sa_share_t share, char *protocol)
{
	sa_share_impl_t impl_share = (sa_share_impl_t)share;
	int rc, ret;
	boolean_t found_protocol;
	sa_fstype_t *fstype;

#ifdef DEBUG
	fprintf(stderr, "sa_enable_share: share->sharepath=%s, protocol=%s\n",
		impl_share->sharepath, protocol);
#endif

	assert(impl_share->handle != NULL);

	ret = SA_OK;
	found_protocol = B_FALSE;

	fstype = fstypes;
	while (fstype != NULL) {
		if (protocol == NULL || strcmp(fstype->name, protocol) == 0) {
			update_zfs_share(impl_share, fstype->name);

			rc = fstype->ops->enable_share(impl_share);

			if (rc != SA_OK)
				ret = rc;
			else
				FSINFO(impl_share, fstype)->active = B_TRUE;

			found_protocol = B_TRUE;
		}

		fstype = fstype->next;
	}

	update_sharetab(impl_share->handle);

	return (found_protocol ? ret : SA_INVALID_PROTOCOL);
}

int
sa_disable_share(sa_share_t share, char *protocol)
{
	sa_share_impl_t impl_share = (sa_share_impl_t)share;
	int rc, ret;
	boolean_t found_protocol;
	sa_fstype_t *fstype;

#ifdef DEBUG
	fprintf(stderr, "sa_disable_share: share->sharepath=%s, protocol=%s\n",
		impl_share->sharepath, protocol);
#endif

	ret = SA_OK;
	found_protocol = B_FALSE;

	fstype = fstypes;
	while (fstype != NULL) {
		if (protocol == NULL || strcmp(fstype->name, protocol) == 0) {
			rc = fstype->ops->disable_share(impl_share);

			if (rc == SA_OK) {
				fstype->ops->clear_shareopts(impl_share);

				FSINFO(impl_share, fstype)->active = B_FALSE;
			} else
				ret = rc;

			found_protocol = B_TRUE;
		}

		fstype = fstype->next;
	}

	update_sharetab(impl_share->handle);

	return (found_protocol ? ret : SA_INVALID_PROTOCOL);
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
sa_parse_legacy_options(sa_group_t group, char *options, char *proto)
{
	sa_fstype_t *fstype;

#ifdef DEBUG
	fprintf(stderr, "sa_parse_legacy_options: options=%s, proto=%s\n",
		options, proto);
#endif

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

boolean_t
sa_needs_refresh(sa_handle_t handle)
{
	return (B_TRUE);
}

libzfs_handle_t *
sa_get_zfs_handle(sa_handle_t handle)
{
	sa_handle_impl_t impl_handle = (sa_handle_impl_t)handle;

	if (impl_handle == NULL)
		return (NULL);

	return (impl_handle->zfs_libhandle);
}

static sa_share_impl_t
alloc_share(const char *sharepath)
{
	sa_share_impl_t impl_share;

	impl_share = calloc(sizeof (struct sa_share_impl), 1);

	if (impl_share == NULL)
		return (NULL);

	impl_share->sharepath = strdup(sharepath);

	if (impl_share->sharepath == NULL) {
		free(impl_share);
		return (NULL);
	}

	impl_share->fsinfo = calloc(sizeof (sa_share_fsinfo_t), fstypes_count);

	if (impl_share->fsinfo == NULL) {
		free(impl_share->sharepath);
		free(impl_share);
		return (NULL);
	}

	return (impl_share);
}

static void
free_share(sa_share_impl_t impl_share) {
	sa_fstype_t *fstype;

	fstype = fstypes;
	while (fstype != NULL) {
		fstype->ops->clear_shareopts(impl_share);

		free(FSINFO(impl_share, fstype)->resource);

		fstype = fstype->next;
	}

	free(impl_share->sharepath);
	free(impl_share->dataset);
	free(impl_share->fsinfo);
	free(impl_share);
}

int
sa_zfs_process_share(sa_handle_t handle, sa_group_t group, sa_share_t share,
    char *mountpoint, char *proto, zprop_source_t source, char *shareopts,
    char *sourcestr, char *dataset)
{
	sa_handle_impl_t impl_handle = (sa_handle_impl_t)handle;
	sa_share_impl_t impl_share = (sa_share_impl_t)share;

#ifdef DEBUG
	fprintf(stderr, "sa_zfs_process_share: mountpoint=%s, proto=%s, "
	    "shareopts=%s, sourcestr=%s, dataset=%s\n", mountpoint, proto,
	    shareopts, sourcestr, dataset);
#endif

	return (process_share(impl_handle, impl_share, mountpoint, NULL,
	    proto, shareopts, NULL, dataset, B_FALSE));
}

void
sa_update_sharetab_ts(sa_handle_t handle)
{
	sa_handle_impl_t impl_handle = (sa_handle_impl_t)handle;

	update_sharetab(impl_handle);
}
