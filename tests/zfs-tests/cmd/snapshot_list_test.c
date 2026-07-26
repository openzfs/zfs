// SPDX-License-Identifier: CDDL-1.0
/*
 * Exercise projected snapshot iteration behavior which is not exposed by the
 * zfs command line.
 */

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libzfs.h>

static int
close_snapshot(zfs_handle_t *zhp, void *arg __attribute__((unused)))
{
	zfs_close(zhp);
	return (0);
}

static int
count_snapshot(zfs_handle_t *zhp, void *arg)
{
	unsigned int *count = arg;

	(*count)++;
	zfs_close(zhp);
	return (0);
}

static int
print_snapshot(zfs_handle_t *zhp, void *arg __attribute__((unused)))
{
	int error = 0;

	if (puts(zfs_get_name(zhp)) == EOF)
		error = EIO;
	zfs_close(zhp);
	return (error);
}

typedef struct callback_error_arg {
	int cea_error;
	unsigned int cea_calls;
} callback_error_arg_t;

typedef struct snapshot_metadata_arg {
	zfs_type_t sma_underlying_type;
	boolean_t sma_encrypted;
	unsigned int sma_calls;
} snapshot_metadata_arg_t;

static int
return_callback_error(zfs_handle_t *zhp, void *arg)
{
	callback_error_arg_t *callback = arg;

	zfs_close(zhp);
	callback->cea_calls++;
	return (callback->cea_calls >= 2 ? callback->cea_error : 0);
}

static int
collect_snapshot_metadata(zfs_handle_t *zhp, void *arg)
{
	snapshot_metadata_arg_t *metadata = arg;

	metadata->sma_underlying_type = zfs_get_underlying_type(zhp);
	metadata->sma_encrypted = zfs_is_encrypted(zhp);
	metadata->sma_calls++;
	zfs_close(zhp);
	return (0);
}

static int
check_encrypted_snapshot_metadata(zfs_handle_t *zhp, void *arg)
{
	unsigned int *callbacks = arg;
	zfs_type_t underlying_type = zfs_get_underlying_type(zhp);
	boolean_t encrypted = zfs_is_encrypted(zhp);
	int error = 0;

	(*callbacks)++;
	if (underlying_type != ZFS_TYPE_FILESYSTEM || encrypted != B_TRUE) {
		(void) fprintf(stderr,
		    "expected %s type/encryption %d/%d, got %d/%d\n",
		    zfs_get_name(zhp), ZFS_TYPE_FILESYSTEM, B_TRUE,
		    underlying_type, encrypted);
		error = EPROTO;
	}
	zfs_close(zhp);
	return (error);
}

static uint64_t
parse_uint64(const char *value)
{
	char *end;
	uint64_t result;

	errno = 0;
	result = strtoull(value, &end, 10);
	if (errno != 0 || value[0] == '\0' || end[0] != '\0') {
		(void) fprintf(stderr, "invalid integer: %s\n", value);
		exit(EXIT_FAILURE);
	}
	return (result);
}

static int
open_dataset(libzfs_handle_t **hdl, zfs_handle_t **zhp, const char *name)
{
	*hdl = libzfs_init();
	if (*hdl == NULL) {
		(void) fprintf(stderr, "libzfs_init failed\n");
		return (EXIT_FAILURE);
	}

	*zhp = zfs_open(*hdl, name, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (*zhp == NULL) {
		(void) fprintf(stderr, "cannot open %s\n", name);
		libzfs_fini(*hdl);
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_encrypted_snapshot_metadata(const char *name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	unsigned int callbacks = 0;
	int error;

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	error = zfs_iter_snapshots_v2(zhp, ZFS_ITER_BATCHED,
	    check_encrypted_snapshot_metadata, &callbacks, 0, 0);
	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != 0 || callbacks == 0) {
		(void) fprintf(stderr,
		    "expected encrypted projected snapshot callbacks, "
		    "got error %d and %u callbacks\n", error, callbacks);
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_filter(const char *name, const char *minimum, const char *maximum)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	int error;

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	error = zfs_iter_snapshots_v2(zhp, ZFS_ITER_BATCHED |
	    ZFS_ITER_BATCHED_CREATETXG | ZFS_ITER_BATCHED_GUID,
	    print_snapshot, NULL, parse_uint64(minimum), parse_uint64(maximum));
	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != 0) {
		(void) fprintf(stderr, "snapshot iteration failed: %s\n",
		    strerror(error));
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_sorted(const char *name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	int error;

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	error = zfs_iter_snapshots_sorted_v2(zhp, ZFS_ITER_BATCHED,
	    print_snapshot, NULL, 0, 0);
	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != 0) {
		(void) fprintf(stderr, "sorted snapshot iteration failed: %s\n",
		    strerror(error));
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_interrupt(const char *name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	int error, iteration_errno;

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	errno = 0;
	error = zfs_iter_snapshots_v2(zhp, ZFS_ITER_BATCHED |
	    ZFS_ITER_BATCHED_CREATETXG | ZFS_ITER_BATCHED_GUID |
	    ZFS_ITER_BATCHED_CREATION | ZFS_ITER_BATCHED_USERREFS,
	    close_snapshot, NULL, 0, 0);
	iteration_errno = errno;

	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != -1 || iteration_errno != EINTR) {
		(void) fprintf(stderr, "expected -1/EINTR, got %d/%d (%s)\n",
		    error, iteration_errno, error == 0 ? "success" :
		    strerror(iteration_errno));
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_handle_enomem(const char *name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	unsigned int callbacks = 0;
	int error, iteration_errno, libzfs_error;

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	libzfs_print_on_error(hdl, B_FALSE);
	errno = 0;
	error = zfs_iter_snapshots_v2(zhp, ZFS_ITER_BATCHED |
	    ZFS_ITER_BATCHED_CREATION, count_snapshot, &callbacks, 0, 0);
	iteration_errno = errno;
	libzfs_error = libzfs_errno(hdl);

	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != -1 || iteration_errno != ENOMEM ||
	    libzfs_error != EZFS_NOMEM || callbacks != 0) {
		(void) fprintf(stderr, "expected -1/%d/%d/0, got %d/%d/%d/%u\n",
		    ENOMEM, EZFS_NOMEM, error, iteration_errno, libzfs_error,
		    callbacks);
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_metadata_eproto(const char *name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	unsigned int callbacks = 0;
	int error, iteration_errno;

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	libzfs_print_on_error(hdl, B_FALSE);
	errno = 0;
	error = zfs_iter_snapshots_v2(zhp, ZFS_ITER_BATCHED,
	    count_snapshot, &callbacks, 0, 0);
	iteration_errno = errno;

	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != -1 || iteration_errno != EPROTO || callbacks != 0) {
		(void) fprintf(stderr, "expected -1/%d/0, got %d/%d/%u\n",
		    EPROTO, error, iteration_errno, callbacks);
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_callback_error(const char *name, const char *error_name,
    boolean_t bookmarks)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	callback_error_arg_t callback = { 0 };
	int error;

	if (strcmp(error_name, "cmd_unavail") == 0)
		callback.cea_error = ZFS_ERR_IOC_CMD_UNAVAIL;
	else if (strcmp(error_name, "arg_unavail") == 0)
		callback.cea_error = ZFS_ERR_IOC_ARG_UNAVAIL;
	else if (strcmp(error_name, "enotty") == 0)
		callback.cea_error = ENOTTY;
	else if (strcmp(error_name, "enotsup") == 0)
		callback.cea_error = ENOTSUP;
	else if (strcmp(error_name, "enoent") == 0)
		callback.cea_error = ENOENT;
	else if (strcmp(error_name, "esrch") == 0)
		callback.cea_error = ESRCH;
	else {
		(void) fprintf(stderr, "unknown callback error: %s\n",
		    error_name);
		return (EXIT_FAILURE);
	}

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	if (bookmarks) {
		error = zfs_iter_bookmarks_v2(zhp, ZFS_ITER_BATCHED,
		    return_callback_error, &callback);
	} else {
		error = zfs_iter_snapshots_v2(zhp, ZFS_ITER_BATCHED,
		    return_callback_error, &callback, 0, 0);
	}
	zfs_close(zhp);
	libzfs_fini(hdl);
	if (error != callback.cea_error || callback.cea_calls != 2) {
		(void) fprintf(stderr, "expected callback result/calls %d/2, "
		    "got %d/%u\n", callback.cea_error, error,
		    callback.cea_calls);
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_bookmark_error(const char *name, const char *error_name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *zhp;
	unsigned int callbacks = 0;
	int expected, wrapper_error, legacy_error, batched_error;

	if (strcmp(error_name, "eio") == 0)
		expected = EIO;
	else if (strcmp(error_name, "enoent") == 0)
		expected = ENOENT;
	else if (strcmp(error_name, "esrch") == 0)
		expected = ESRCH;
	else {
		(void) fprintf(stderr, "unknown bookmark error: %s\n",
		    error_name);
		return (EXIT_FAILURE);
	}

	if (open_dataset(&hdl, &zhp, name) != 0)
		return (EXIT_FAILURE);

	wrapper_error = zfs_iter_bookmarks(zhp, count_snapshot, &callbacks);
	legacy_error = zfs_iter_bookmarks_v2(zhp, 0, count_snapshot,
	    &callbacks);
	batched_error = zfs_iter_bookmarks_v2(zhp, ZFS_ITER_BATCHED,
	    count_snapshot, &callbacks);
	zfs_close(zhp);
	libzfs_fini(hdl);

	if (wrapper_error != expected || legacy_error != expected ||
	    batched_error != expected || callbacks != 0) {
		(void) fprintf(stderr, "expected bookmark errors/callbacks "
		    "%d/%d/%d/0, got %d/%d/%d/%u\n", expected, expected,
		    expected, wrapper_error, legacy_error, batched_error,
		    callbacks);
		return (EXIT_FAILURE);
	}
	return (EXIT_SUCCESS);
}

static int
run_stale_bookmarks(const char *name, const char *target)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *stale, *mutator = NULL;
	unsigned int callbacks = 0;
	const char *operation = target == NULL ? "destroy" : "rename";
	int error, wrapper_error, legacy_error, batched_error;
	int result = EXIT_FAILURE;

	if (open_dataset(&hdl, &stale, name) != 0)
		return (EXIT_FAILURE);

	mutator = zfs_open(hdl, name, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (mutator == NULL) {
		(void) fprintf(stderr, "cannot open second handle for %s\n",
		    name);
		goto out;
	}

	if (target == NULL) {
		error = zfs_destroy(mutator, B_FALSE);
	} else {
		renameflags_t flags = { .nounmount = B_TRUE };

		error = zfs_rename(mutator, target, flags);
	}
	if (error != 0) {
		(void) fprintf(stderr, "cannot %s %s: %s\n", operation, name,
		    libzfs_error_description(hdl));
		goto out;
	}
	zfs_close(mutator);
	mutator = NULL;

	wrapper_error = zfs_iter_bookmarks(stale, count_snapshot, &callbacks);
	legacy_error = zfs_iter_bookmarks_v2(stale, 0, count_snapshot,
	    &callbacks);
	batched_error = zfs_iter_bookmarks_v2(stale, ZFS_ITER_BATCHED,
	    count_snapshot, &callbacks);
	if (wrapper_error != ENOENT || legacy_error != ENOENT ||
	    batched_error != ENOENT || callbacks != 0) {
		(void) fprintf(stderr,
		    "expected stale bookmark errors/callbacks "
		    "after %s to be %d/%d/%d/0, got %d/%d/%d/%u\n",
		    operation, ENOENT, ENOENT, ENOENT, wrapper_error,
		    legacy_error, batched_error, callbacks);
		goto out;
	}
	result = EXIT_SUCCESS;

out:
	if (mutator != NULL)
		zfs_close(mutator);
	zfs_close(stale);
	libzfs_fini(hdl);
	return (result);
}

static int
run_stale_snapshot_metadata(const char *name, const char *type_name)
{
	libzfs_handle_t *hdl;
	zfs_handle_t *stale, *mutator = NULL;
	nvlist_t *props = NULL;
	snapshot_metadata_arg_t batched = { 0 }, legacy = { 0 };
	zfs_type_t expected_type;
	char snapname[ZFS_MAX_DATASET_NAME_LEN];
	int batched_error, legacy_error, result = EXIT_FAILURE;

	if (strcmp(type_name, "filesystem") == 0)
		expected_type = ZFS_TYPE_FILESYSTEM;
	else if (strcmp(type_name, "volume") == 0)
		expected_type = ZFS_TYPE_VOLUME;
	else {
		(void) fprintf(stderr, "unknown replacement type: %s\n",
		    type_name);
		return (EXIT_FAILURE);
	}
	if (open_dataset(&hdl, &stale, name) != 0)
		return (EXIT_FAILURE);
	if (zfs_get_underlying_type(stale) == expected_type &&
	    !zfs_is_encrypted(stale)) {
		(void) fprintf(stderr, "%s does not exercise stale metadata\n",
		    name);
		goto out;
	}

	mutator = zfs_open(hdl, name, ZFS_TYPE_FILESYSTEM | ZFS_TYPE_VOLUME);
	if (mutator == NULL) {
		(void) fprintf(stderr, "cannot open second handle for %s\n",
		    name);
		goto out;
	}
	if (zfs_destroy(mutator, B_FALSE) != 0) {
		(void) fprintf(stderr, "cannot destroy %s: %s\n", name,
		    libzfs_error_description(hdl));
		goto out;
	}
	zfs_close(mutator);
	mutator = NULL;

	if (nvlist_alloc(&props, NV_UNIQUE_NAME, 0) != 0) {
		(void) fprintf(stderr,
		    "cannot allocate replacement properties\n");
		goto out;
	}
	if (expected_type == ZFS_TYPE_VOLUME &&
	    nvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_VOLSIZE),
	    64 * 1024 * 1024) != 0) {
		(void) fprintf(stderr, "cannot set replacement volume size\n");
		goto out;
	}
	if (zfs_create(hdl, name, expected_type, props) != 0) {
		(void) fprintf(stderr, "cannot recreate %s: %s\n", name,
		    libzfs_error_description(hdl));
		goto out;
	}
	if (snprintf(snapname, sizeof (snapname), "%s@replacement", name) >=
	    sizeof (snapname)) {
		(void) fprintf(stderr, "snapshot name is too long: %s\n", name);
		goto out;
	}
	if (zfs_snapshot(hdl, snapname, B_FALSE, NULL) != 0) {
		(void) fprintf(stderr, "cannot snapshot %s: %s\n", name,
		    libzfs_error_description(hdl));
		goto out;
	}

	batched_error = zfs_iter_snapshots_v2(stale, ZFS_ITER_BATCHED,
	    collect_snapshot_metadata, &batched, 0, 0);
	legacy_error = zfs_iter_snapshots_v2(stale, 0,
	    collect_snapshot_metadata, &legacy, 0, 0);
	if (batched_error != 0 || legacy_error != 0 || batched.sma_calls != 1 ||
	    legacy.sma_calls != 1 ||
	    batched.sma_underlying_type != expected_type ||
	    legacy.sma_underlying_type != expected_type ||
	    batched.sma_encrypted || legacy.sma_encrypted) {
		(void) fprintf(stderr,
		    "expected errors/calls/types/encryption "
		    "0/0/1/1/%d/%d/%d/%d, got "
		    "%d/%d/%u/%u/%d/%d/%d/%d\n",
		    expected_type, expected_type, B_FALSE, B_FALSE,
		    batched_error, legacy_error,
		    batched.sma_calls, legacy.sma_calls,
		    batched.sma_underlying_type, legacy.sma_underlying_type,
		    batched.sma_encrypted, legacy.sma_encrypted);
		goto out;
	}
	result = EXIT_SUCCESS;

out:
	nvlist_free(props);
	if (mutator != NULL)
		zfs_close(mutator);
	zfs_close(stale);
	libzfs_fini(hdl);
	return (result);
}

int
main(int argc, char **argv)
{
	if (argc == 5 && strcmp(argv[1], "filter") == 0)
		return (run_filter(argv[2], argv[3], argv[4]));
	if (argc == 3 && strcmp(argv[1], "sorted") == 0)
		return (run_sorted(argv[2]));
	if (argc == 3 && strcmp(argv[1], "interrupt") == 0)
		return (run_interrupt(argv[2]));
	if (argc == 3 && strcmp(argv[1], "handle-enomem") == 0)
		return (run_handle_enomem(argv[2]));
	if (argc == 3 && strcmp(argv[1], "metadata-eproto") == 0)
		return (run_metadata_eproto(argv[2]));
	if (argc == 4 && strcmp(argv[1], "callback-error") == 0)
		return (run_callback_error(argv[2], argv[3], B_FALSE));
	if (argc == 4 && strcmp(argv[1], "bookmark-callback-error") == 0)
		return (run_callback_error(argv[2], argv[3], B_TRUE));
	if (argc == 4 && strcmp(argv[1], "bookmark-error") == 0)
		return (run_bookmark_error(argv[2], argv[3]));
	if (argc == 3 && strcmp(argv[1], "stale-bookmarks-destroy") == 0)
		return (run_stale_bookmarks(argv[2], NULL));
	if (argc == 4 && strcmp(argv[1], "stale-bookmarks-rename") == 0)
		return (run_stale_bookmarks(argv[2], argv[3]));
	if (argc == 4 && strcmp(argv[1], "stale-snapshot-metadata") == 0)
		return (run_stale_snapshot_metadata(argv[2], argv[3]));
	if (argc == 3 &&
	    strcmp(argv[1], "encrypted-snapshot-metadata") == 0)
		return (run_encrypted_snapshot_metadata(argv[2]));

	(void) fprintf(stderr, "usage: %s filter dataset min_txg max_txg\n"
	    "       %s encrypted-snapshot-metadata dataset\n"
	    "       %s sorted dataset\n"
	    "       %s interrupt dataset\n"
	    "       %s handle-enomem dataset\n"
	    "       %s metadata-eproto dataset\n"
	    "       %s callback-error dataset error\n"
	    "       %s bookmark-callback-error dataset error\n"
	    "       %s bookmark-error dataset error\n"
	    "       %s stale-bookmarks-destroy dataset\n"
	    "       %s stale-bookmarks-rename dataset target\n"
	    "       %s stale-snapshot-metadata dataset type\n",
	    argv[0], argv[0],
	    argv[0], argv[0], argv[0], argv[0], argv[0], argv[0], argv[0],
	    argv[0], argv[0], argv[0]);
	return (EXIT_FAILURE);
}
