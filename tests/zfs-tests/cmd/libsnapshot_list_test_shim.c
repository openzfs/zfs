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
 * Trace or inject errors into projected snapshot listing.  This library is
 * only loaded explicitly by ZTS tests.
 */

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <libzfs.h>
#include <libzfs_core.h>
#include <sys/zfs_ioctl.h>

typedef int (*lzc_ioctl_fd_fn_t)(int, unsigned long, zfs_cmd_t *);
typedef int (*lzc_get_bookmarks_fn_t)(const char *, nvlist_t *, nvlist_t **);
typedef int (*nvlist_add_nvlist_fn_t)(nvlist_t *, const char *,
    const nvlist_t *);

static int handle_enomem_armed;
static int handle_enomem_injected;
static int empty_batch_injected;
static int real_enomem_injected;

static int
write_all(int fd, const char *buffer, size_t length)
{
	ssize_t written;

	while (length > 0) {
		written = write(fd, buffer, length);
		if (written > 0) {
			buffer += written;
			length -= written;
			continue;
		}
		if (written == -1 && errno == EINTR)
			continue;
		return (-1);
	}
	return (0);
}

static lzc_ioctl_fd_fn_t
find_lzc_ioctl_fd(void)
{
	void *symbol = dlsym(RTLD_NEXT, "lzc_ioctl_fd");
	lzc_ioctl_fd_fn_t function;

	(void) memcpy(&function, &symbol, sizeof (function));
	return (function);
}

static lzc_get_bookmarks_fn_t
find_lzc_get_bookmarks(void)
{
	void *symbol = dlsym(RTLD_NEXT, "lzc_get_bookmarks");
	lzc_get_bookmarks_fn_t function;

	(void) memcpy(&function, &symbol, sizeof (function));
	return (function);
}

static nvlist_add_nvlist_fn_t
find_nvlist_add_nvlist(void)
{
	void *symbol = dlsym(RTLD_NEXT, "nvlist_add_nvlist");
	nvlist_add_nvlist_fn_t function;

	(void) memcpy(&function, &symbol, sizeof (function));
	return (function);
}

static void
write_marker(const char *mode)
{
	const char *path;
	int fd;
	int saved_errno = errno;

	path = getenv("ZFS_SNAPSHOT_LIST_TEST_MARKER");
	if (path != NULL) {
		fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
		if (fd >= 0) {
			if (write_all(fd, mode, strlen(mode)) == 0)
				(void) write_all(fd, "\n", 1);
			(void) close(fd);
		}
	}
	errno = saved_errno;
}

static int
pack_batch(zfs_cmd_t *zc, nvlist_t *batch)
{
	char *packed = NULL;
	size_t packed_size = 0;
	int error;

	error = nvlist_pack(batch, &packed, &packed_size, NV_ENCODE_NATIVE, 0);
	if (error != 0)
		goto out;
	if (packed_size > zc->zc_nvlist_dst_size) {
		error = EPROTO;
		goto out;
	}

	(void) memcpy((void *)(uintptr_t)zc->zc_nvlist_dst, packed,
	    packed_size);
	zc->zc_nvlist_dst_size = packed_size;
	zc->zc_nvlist_dst_filled = B_TRUE;

out:
	free(packed);
	return (error);
}

static boolean_t
batch_has_snapshot_results(const zfs_cmd_t *zc)
{
	nvlist_t *batch = NULL;
	char **names;
	uint_t count;
	boolean_t result = B_FALSE;
	int saved_errno = errno;

	if (zc->zc_nvlist_dst_filled && zc->zc_nvlist_dst != 0 &&
	    zc->zc_nvlist_dst_size != 0 &&
	    nvlist_unpack((char *)(uintptr_t)zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, &batch, 0) == 0 &&
	    nvlist_lookup_string_array(batch, SNAP_ITER_BATCH_NAMES, &names,
	    &count) == 0 && count != 0) {
		result = B_TRUE;
	}

	nvlist_free(batch);
	errno = saved_errno;
	return (result);
}

/*
 * Preserve the kernel's opaque cursor while omitting this batch's results.
 * This deterministically exercises continuation after an empty, non-EOF reply.
 */
static int
replace_with_empty_batch(zfs_cmd_t *zc)
{
	nvlist_t *batch = NULL;
	nvlist_t *replacement = NULL;
	uint64_t cursor, dmu_type, dds_flags;
	boolean_t eof;
	int error;

	error = nvlist_unpack((void *)(uintptr_t)zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, &batch, 0);
	if (error != 0)
		goto out;
	if (nvlist_lookup_boolean_value(batch, SNAP_ITER_BATCH_EOF,
	    &eof) != 0 || eof != B_FALSE ||
	    nvlist_lookup_uint64(batch, SNAP_ITER_BATCH_CURSOR, &cursor) != 0 ||
	    nvlist_lookup_uint64(batch, SNAP_ITER_BATCH_DMU_TYPE,
	    &dmu_type) != 0 ||
	    nvlist_lookup_uint64(batch, SNAP_ITER_BATCH_DDS_FLAGS,
	    &dds_flags) != 0) {
		error = EPROTO;
		goto out;
	}

	error = nvlist_alloc(&replacement, NV_UNIQUE_NAME, 0);
	if (error != 0)
		goto out;
	error = nvlist_add_uint64(replacement, SNAP_ITER_BATCH_CURSOR, cursor);
	if (error != 0)
		goto out;
	error = nvlist_add_uint64(replacement, SNAP_ITER_BATCH_DMU_TYPE,
	    dmu_type);
	if (error != 0)
		goto out;
	error = nvlist_add_uint64(replacement, SNAP_ITER_BATCH_DDS_FLAGS,
	    dds_flags);
	if (error != 0)
		goto out;
	error = nvlist_add_boolean_value(replacement, SNAP_ITER_BATCH_EOF,
	    B_FALSE);
	if (error != 0)
		goto out;
	error = pack_batch(zc, replacement);

out:
	nvlist_free(replacement);
	nvlist_free(batch);
	return (error);
}

static boolean_t
is_metadata_mode(const char *mode)
{
	return (strcmp(mode, "missing_eof") == 0 ||
	    strcmp(mode, "unvalued_eof") == 0 ||
	    strcmp(mode, "missing_dmu_type") == 0 ||
	    strcmp(mode, "invalid_dmu_type") == 0 ||
	    strcmp(mode, "missing_dds_flags") == 0 ||
	    strcmp(mode, "invalid_dds_flags") == 0);
}

static int
replace_batch_metadata(zfs_cmd_t *zc, const char *mode)
{
	nvlist_t *batch = NULL;
	const char *name;
	boolean_t invalid;
	int error;

	if (strcmp(mode, "missing_eof") == 0 ||
	    strcmp(mode, "unvalued_eof") == 0) {
		name = SNAP_ITER_BATCH_EOF;
		invalid = strcmp(mode, "missing_eof") != 0;
	} else if (strcmp(mode, "missing_dmu_type") == 0 ||
	    strcmp(mode, "invalid_dmu_type") == 0) {
		name = SNAP_ITER_BATCH_DMU_TYPE;
		invalid = strcmp(mode, "invalid_dmu_type") == 0;
	} else {
		name = SNAP_ITER_BATCH_DDS_FLAGS;
		invalid = strcmp(mode, "invalid_dds_flags") == 0;
	}

	error = nvlist_unpack((void *)(uintptr_t)zc->zc_nvlist_dst,
	    zc->zc_nvlist_dst_size, &batch, 0);
	if (error != 0)
		goto out;
	(void) nvlist_remove_all(batch, name);
	if (invalid) {
		if (strcmp(mode, "unvalued_eof") == 0) {
			error = nvlist_add_boolean(batch, name);
		} else {
			error = nvlist_add_uint64(batch, name, UINT64_MAX);
		}
		if (error != 0)
			goto out;
	}
	error = pack_batch(zc, batch);

out:
	nvlist_free(batch);
	return (error);
}

int
lzc_get_bookmarks(const char *fsname, nvlist_t *props, nvlist_t **bmarks)
{
	static lzc_get_bookmarks_fn_t next;
	const char *mode = getenv("ZFS_SNAPSHOT_LIST_TEST_MODE");
	int error = 0;

	if (mode != NULL && strcmp(mode, "bookmark_eio") == 0)
		error = EIO;
	else if (mode != NULL && strcmp(mode, "bookmark_enoent") == 0)
		error = ENOENT;
	else if (mode != NULL && strcmp(mode, "bookmark_esrch") == 0)
		error = ESRCH;
	if (error != 0) {
		write_marker(mode);
		return (error);
	}

	if (next == NULL)
		next = find_lzc_get_bookmarks();
	if (next == NULL)
		return (ENOSYS);
	return (next(fsname, props, bmarks));
}

int
nvlist_add_nvlist(nvlist_t *nvl, const char *name, const nvlist_t *val)
{
	static nvlist_add_nvlist_fn_t next;
	const char *mode = getenv("ZFS_SNAPSHOT_LIST_TEST_MODE");

	if (handle_enomem_armed && mode != NULL) {
		if (!handle_enomem_injected &&
		    strcmp(mode, "handle_enomem") == 0 &&
		    strcmp(name, "userrefs") == 0) {
			handle_enomem_injected = 1;
			write_marker(mode);
			return (ENOMEM);
		}
		if (strcmp(mode, "direct_properties") == 0 &&
		    (strcmp(name, "creation") == 0 ||
		    strcmp(name, "userrefs") == 0)) {
			write_marker("unexpected_property_nvlist");
			return (ENOMEM);
		}
	}

	if (next == NULL)
		next = find_nvlist_add_nvlist();
	if (next == NULL)
		return (ENOSYS);
	return (next(nvl, name, val));
}

int
lzc_ioctl_fd(int fd, unsigned long request, zfs_cmd_t *zc)
{
	static lzc_ioctl_fd_fn_t next;
	static int enomem_injected;
	static unsigned int batch_calls;
	const char *mode = getenv("ZFS_SNAPSHOT_LIST_TEST_MODE");
	int error;
	int injected_errno = 0;

	if (request == ZFS_IOC_SNAPSHOT_LIST_BATCH && mode != NULL) {
		batch_calls++;
		if (strcmp(mode, "count") == 0)
			write_marker(mode);
		else if (strcmp(mode, "enotty") == 0)
			injected_errno = ENOTTY;
		else if (strcmp(mode, "enotsup") == 0)
			injected_errno = ENOTSUP;
		else if (strcmp(mode, "enotsup_after_first") == 0 &&
		    batch_calls > 1)
			injected_errno = ENOTSUP;
		else if (strcmp(mode, "enoent_after_first") == 0 &&
		    batch_calls > 1)
			injected_errno = ENOENT;
		else if (strcmp(mode, "esrch_after_first") == 0 &&
		    batch_calls > 1)
			injected_errno = ESRCH;
		else if (strcmp(mode, "cmd_unavail") == 0)
			injected_errno = ZFS_ERR_IOC_CMD_UNAVAIL;
		else if (strcmp(mode, "arg_unavail") == 0)
			injected_errno = ZFS_ERR_IOC_ARG_UNAVAIL;
		else if (strcmp(mode, "enoent") == 0)
			injected_errno = ENOENT;
		else if (strcmp(mode, "esrch") == 0)
			injected_errno = ESRCH;
		else if (strcmp(mode, "eintr") == 0)
			injected_errno = EINTR;

		if (injected_errno != 0) {
			write_marker(mode);
			errno = injected_errno;
			return (-1);
		}
		if (strcmp(mode, "enomem") == 0 && !enomem_injected) {
			enomem_injected = 1;
			zc->zc_nvlist_dst_size *= 2;
			write_marker(mode);
			errno = ENOMEM;
			return (-1);
		}
	}

	if (next == NULL)
		next = find_lzc_ioctl_fd();
	if (next == NULL) {
		errno = ENOSYS;
		return (-1);
	}
	if (request == ZFS_IOC_SNAPSHOT_LIST_BATCH && mode != NULL &&
	    strcmp(mode, "real_enomem") == 0 && !real_enomem_injected) {
		/*
		 * Keep the allocation, but advertise too little room for
		 * copyout.
		 */
		real_enomem_injected = 1;
		zc->zc_nvlist_dst_size = 1;
		error = next(fd, request, zc);
		if (error == -1 && errno == ENOMEM &&
		    zc->zc_nvlist_dst_size > 1) {
			write_marker(mode);
			return (error);
		}
		errno = EPROTO;
		return (-1);
	}
	error = next(fd, request, zc);
	if (error == -1 && errno == EIO &&
	    request == ZFS_IOC_SNAPSHOT_LIST_BATCH && mode != NULL &&
	    strcmp(mode, "partial_eio_output") == 0 &&
	    batch_has_snapshot_results(zc)) {
		write_marker(mode);
	}
	if (error == 0 && request == ZFS_IOC_SNAPSHOT_LIST_BATCH &&
	    mode != NULL && strcmp(mode, "empty_non_eof") == 0 &&
	    !empty_batch_injected) {
		empty_batch_injected = 1;
		error = replace_with_empty_batch(zc);
		if (error != 0) {
			errno = error;
			return (-1);
		}
		write_marker(mode);
	}
	if (error == 0 && request == ZFS_IOC_SNAPSHOT_LIST_BATCH &&
	    mode != NULL && is_metadata_mode(mode)) {
		error = replace_batch_metadata(zc, mode);
		if (error != 0) {
			errno = error;
			return (-1);
		}
		write_marker(mode);
	}
	if (error == 0 && request == ZFS_IOC_SNAPSHOT_LIST_BATCH &&
	    mode != NULL && (strcmp(mode, "handle_enomem") == 0 ||
	    strcmp(mode, "direct_properties") == 0)) {
		handle_enomem_armed = 1;
		if (strcmp(mode, "direct_properties") == 0)
			write_marker(mode);
	}
	return (error);
}
