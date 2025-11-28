// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libzfs_core.h>
#include <libzutil.h>

#include <sys/nvpair.h>
#include <sys/vdev_impl.h>
#include <sys/zfs_ioctl.h>
#include <sys/fs/zfs.h>

/*
 * Test the nvpair inputs for the non-legacy zfs ioctl commands.
 */

static boolean_t unexpected_failures;
static int zfs_fd;
static const char *active_test;

/*
 * Tracks which zfs_ioc_t commands were tested
 */
static boolean_t ioc_tested[ZFS_IOC_LAST - ZFS_IOC_FIRST];

/*
 * Legacy ioctls that are skipped (for now)
 */
static const zfs_ioc_t ioc_skip[] = {
	ZFS_IOC_POOL_CREATE,
	ZFS_IOC_POOL_DESTROY,
	ZFS_IOC_POOL_IMPORT,
	ZFS_IOC_POOL_EXPORT,
	ZFS_IOC_POOL_CONFIGS,
	ZFS_IOC_POOL_STATS,
	ZFS_IOC_POOL_TRYIMPORT,
	ZFS_IOC_POOL_SCAN,
	ZFS_IOC_POOL_FREEZE,
	ZFS_IOC_POOL_UPGRADE,
	ZFS_IOC_POOL_GET_HISTORY,

	ZFS_IOC_VDEV_ADD,
	ZFS_IOC_VDEV_REMOVE,
	ZFS_IOC_VDEV_SET_STATE,
	ZFS_IOC_VDEV_ATTACH,
	ZFS_IOC_VDEV_DETACH,
	ZFS_IOC_VDEV_SETPATH,
	ZFS_IOC_VDEV_SETFRU,

	ZFS_IOC_OBJSET_STATS,
	ZFS_IOC_OBJSET_ZPLPROPS,
	ZFS_IOC_DATASET_LIST_NEXT,
	ZFS_IOC_SNAPSHOT_LIST_NEXT,
	ZFS_IOC_SET_PROP,
	ZFS_IOC_DESTROY,
	ZFS_IOC_RENAME,
	ZFS_IOC_RECV,
	ZFS_IOC_SEND,
	ZFS_IOC_INJECT_FAULT,
	ZFS_IOC_CLEAR_FAULT,
	ZFS_IOC_INJECT_LIST_NEXT,
	ZFS_IOC_ERROR_LOG,
	ZFS_IOC_CLEAR,
	ZFS_IOC_PROMOTE,
	ZFS_IOC_DSOBJ_TO_DSNAME,
	ZFS_IOC_OBJ_TO_PATH,
	ZFS_IOC_POOL_SET_PROPS,
	ZFS_IOC_POOL_GET_PROPS,
	ZFS_IOC_SET_FSACL,
	ZFS_IOC_GET_FSACL,
	ZFS_IOC_SHARE,
	ZFS_IOC_INHERIT_PROP,
	ZFS_IOC_SMB_ACL,
	ZFS_IOC_USERSPACE_ONE,
	ZFS_IOC_USERSPACE_MANY,
	ZFS_IOC_USERSPACE_UPGRADE,
	ZFS_IOC_OBJSET_RECVD_PROPS,
	ZFS_IOC_VDEV_SPLIT,
	ZFS_IOC_NEXT_OBJ,
	ZFS_IOC_DIFF,
	ZFS_IOC_TMP_SNAPSHOT,
	ZFS_IOC_OBJ_TO_STATS,
	ZFS_IOC_SPACE_WRITTEN,
	ZFS_IOC_POOL_REGUID,
	ZFS_IOC_SEND_PROGRESS,
	ZFS_IOC_EVENTS_NEXT,
	ZFS_IOC_EVENTS_CLEAR,
	ZFS_IOC_EVENTS_SEEK,
	ZFS_IOC_NEXTBOOT,
	ZFS_IOC_JAIL,
	ZFS_IOC_UNJAIL,
};


#define	IOC_INPUT_TEST(ioc, name, req, opt, err)		\
	IOC_INPUT_TEST_IMPL(ioc, name, req, opt, err, B_FALSE)

#define	IOC_INPUT_TEST_WILD(ioc, name, req, opt, err)		\
	IOC_INPUT_TEST_IMPL(ioc, name, req, opt, err, B_TRUE)

#define	IOC_INPUT_TEST_IMPL(ioc, name, req, opt, err, wild)	\
	do {							\
		active_test = __func__ + 5;			\
		ioc_tested[ioc - ZFS_IOC_FIRST] = B_TRUE;	\
		lzc_ioctl_test(ioc, name, req, opt, err, wild);	\
	} while (0)

/*
 * run a zfs ioctl command, verify expected results and log failures
 */
static void
lzc_ioctl_run(zfs_ioc_t ioc, const char *name, nvlist_t *innvl, int expected)
{
	zfs_cmd_t zc = {"\0"};
	char *packed = NULL;
	const char *variant;
	size_t size = 0;
	int error = 0;

	switch (expected) {
	case ZFS_ERR_IOC_ARG_UNAVAIL:
		variant = "unsupported input";
		break;
	case ZFS_ERR_IOC_ARG_REQUIRED:
		variant = "missing input";
		break;
	case ZFS_ERR_IOC_ARG_BADTYPE:
		variant = "invalid input type";
		break;
	default:
		variant = "valid input";
		break;
	}

	packed = fnvlist_pack(innvl, &size);
	(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));
	zc.zc_name[sizeof (zc.zc_name) - 1] = '\0';
	zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
	zc.zc_nvlist_src_size = size;
	zc.zc_nvlist_dst_size = MAX(size * 2, 128 * 1024);
	zc.zc_nvlist_dst = (uint64_t)(uintptr_t)malloc(zc.zc_nvlist_dst_size);

	if (lzc_ioctl_fd(zfs_fd, ioc, &zc) != 0)
		error = errno;

	if (error != expected) {
		unexpected_failures = B_TRUE;
		(void) fprintf(stderr, "%s: Unexpected result with %s, "
		    "error %d (expecting %d)\n",
		    active_test, variant, error, expected);
	}

	fnvlist_pack_free(packed, size);
	free((void *)(uintptr_t)zc.zc_nvlist_dst);
}

/*
 * Test each ioc for the following ioctl input errors:
 *   ZFS_ERR_IOC_ARG_UNAVAIL	an input argument is not supported by kernel
 *   ZFS_ERR_IOC_ARG_REQUIRED	a required input argument is missing
 *   ZFS_ERR_IOC_ARG_BADTYPE	an input argument has an invalid type
 */
static int
lzc_ioctl_test(zfs_ioc_t ioc, const char *name, nvlist_t *required,
    nvlist_t *optional, int expected_error, boolean_t wildcard)
{
	nvlist_t *input = fnvlist_alloc();
	nvlist_t *future = fnvlist_alloc();
	int error = 0;

	if (required != NULL) {
		for (nvpair_t *pair = nvlist_next_nvpair(required, NULL);
		    pair != NULL; pair = nvlist_next_nvpair(required, pair)) {
			fnvlist_add_nvpair(input, pair);
		}
	}
	if (optional != NULL) {
		for (nvpair_t *pair = nvlist_next_nvpair(optional, NULL);
		    pair != NULL; pair = nvlist_next_nvpair(optional, pair)) {
			fnvlist_add_nvpair(input, pair);
		}
	}

	/*
	 * Generic input run with 'optional' nvlist pair
	 */
	if (!wildcard)
		fnvlist_add_nvlist(input, "optional", future);
	lzc_ioctl_run(ioc, name, input, expected_error);
	if (!wildcard)
		fnvlist_remove(input, "optional");

	/*
	 * Bogus input value
	 */
	if (!wildcard) {
		fnvlist_add_string(input, "bogus_input", "bogus");
		lzc_ioctl_run(ioc, name, input, ZFS_ERR_IOC_ARG_UNAVAIL);
		fnvlist_remove(input, "bogus_input");
	}

	/*
	 * Missing required inputs
	 */
	if (required != NULL) {
		nvlist_t *empty = fnvlist_alloc();
		lzc_ioctl_run(ioc, name, empty, ZFS_ERR_IOC_ARG_REQUIRED);
		nvlist_free(empty);
	}

	/*
	 * Wrong nvpair type
	 */
	if (required != NULL || optional != NULL) {
		/*
		 * switch the type of one of the input pairs
		 */
		for (nvpair_t *pair = nvlist_next_nvpair(input, NULL);
		    pair != NULL; pair = nvlist_next_nvpair(input, pair)) {
			char pname[MAXNAMELEN];
			data_type_t ptype;

			strlcpy(pname, nvpair_name(pair), sizeof (pname));
			pname[sizeof (pname) - 1] = '\0';
			ptype = nvpair_type(pair);
			fnvlist_remove_nvpair(input, pair);

			switch (ptype) {
			case DATA_TYPE_STRING:
				fnvlist_add_uint64(input, pname, 42);
				break;
			default:
				fnvlist_add_string(input, pname, "bogus");
				break;
			}
		}
		lzc_ioctl_run(ioc, name, input, ZFS_ERR_IOC_ARG_BADTYPE);
	}

	nvlist_free(future);
	nvlist_free(input);

	return (error);
}

static void
test_pool_sync(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();

	fnvlist_add_boolean_value(required, "force", B_TRUE);

	IOC_INPUT_TEST(ZFS_IOC_POOL_SYNC, pool, required, NULL, 0);

	nvlist_free(required);
}

static void
test_pool_reopen(const char *pool)
{
	nvlist_t *optional = fnvlist_alloc();

	fnvlist_add_boolean_value(optional, "scrub_restart", B_FALSE);

	IOC_INPUT_TEST(ZFS_IOC_POOL_REOPEN, pool, NULL, optional, 0);

	nvlist_free(optional);
}

static void
test_pool_checkpoint(const char *pool)
{
	IOC_INPUT_TEST(ZFS_IOC_POOL_CHECKPOINT, pool, NULL, NULL, 0);
}

static void
test_pool_discard_checkpoint(const char *pool)
{
	int err = lzc_pool_checkpoint(pool);
	if (err == 0 || err == ZFS_ERR_CHECKPOINT_EXISTS)
		IOC_INPUT_TEST(ZFS_IOC_POOL_DISCARD_CHECKPOINT, pool, NULL,
		    NULL, 0);
}

static void
test_log_history(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();

	fnvlist_add_string(required, "message", "input check");

	IOC_INPUT_TEST(ZFS_IOC_LOG_HISTORY, pool, required, NULL, 0);

	nvlist_free(required);
}

static void
test_create(const char *pool)
{
	char dataset[MAXNAMELEN + 32];

	(void) snprintf(dataset, sizeof (dataset), "%s/create-fs", pool);

	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *props = fnvlist_alloc();

	fnvlist_add_int32(required, "type", DMU_OST_ZFS);
	fnvlist_add_uint64(props, "recordsize", 8192);
	fnvlist_add_nvlist(optional, "props", props);

	IOC_INPUT_TEST(ZFS_IOC_CREATE, dataset, required, optional, 0);

	nvlist_free(required);
	nvlist_free(optional);
}

static void
test_snapshot(const char *pool, const char *snapshot)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *snaps = fnvlist_alloc();
	nvlist_t *props = fnvlist_alloc();

	fnvlist_add_boolean(snaps, snapshot);
	fnvlist_add_nvlist(required, "snaps", snaps);

	fnvlist_add_string(props, "org.openzfs:launch", "September 17th, 2013");
	fnvlist_add_nvlist(optional, "props", props);

	IOC_INPUT_TEST(ZFS_IOC_SNAPSHOT, pool, required, optional, 0);

	nvlist_free(props);
	nvlist_free(snaps);
	nvlist_free(optional);
	nvlist_free(required);
}

static void
test_space_snaps(const char *snapshot)
{
	nvlist_t *required = fnvlist_alloc();
	fnvlist_add_string(required, "firstsnap", snapshot);

	IOC_INPUT_TEST(ZFS_IOC_SPACE_SNAPS, snapshot, required, NULL, 0);

	nvlist_free(required);
}

static void
test_destroy_snaps(const char *pool, const char *snapshot)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *snaps = fnvlist_alloc();

	fnvlist_add_boolean(snaps, snapshot);
	fnvlist_add_nvlist(required, "snaps", snaps);

	IOC_INPUT_TEST(ZFS_IOC_DESTROY_SNAPS, pool, required, NULL, 0);

	nvlist_free(snaps);
	nvlist_free(required);
}


static void
test_bookmark(const char *pool, const char *snapshot, const char *bookmark)
{
	nvlist_t *required = fnvlist_alloc();

	fnvlist_add_string(required, bookmark, snapshot);

	IOC_INPUT_TEST_WILD(ZFS_IOC_BOOKMARK, pool, required, NULL, 0);

	nvlist_free(required);
}

static void
test_get_bookmarks(const char *dataset)
{
	nvlist_t *optional = fnvlist_alloc();

	fnvlist_add_boolean(optional, "guid");
	fnvlist_add_boolean(optional, "createtxg");
	fnvlist_add_boolean(optional, "creation");

	IOC_INPUT_TEST_WILD(ZFS_IOC_GET_BOOKMARKS, dataset, NULL, optional, 0);

	nvlist_free(optional);
}

static void
test_destroy_bookmarks(const char *pool, const char *bookmark)
{
	nvlist_t *required = fnvlist_alloc();

	fnvlist_add_boolean(required, bookmark);

	IOC_INPUT_TEST_WILD(ZFS_IOC_DESTROY_BOOKMARKS, pool, required, NULL, 0);

	nvlist_free(required);
}

static void
test_clone(const char *snapshot, const char *clone)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *props = fnvlist_alloc();

	fnvlist_add_string(required, "origin", snapshot);

	IOC_INPUT_TEST(ZFS_IOC_CLONE, clone, required, NULL, 0);

	nvlist_free(props);
	nvlist_free(optional);
	nvlist_free(required);
}

static void
test_rollback(const char *dataset, const char *snapshot)
{
	nvlist_t *optional = fnvlist_alloc();

	fnvlist_add_string(optional, "target", snapshot);

	IOC_INPUT_TEST(ZFS_IOC_ROLLBACK, dataset, NULL, optional, B_FALSE);

	nvlist_free(optional);
}

static void
test_hold(const char *pool, const char *snapshot)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *holds = fnvlist_alloc();

	fnvlist_add_string(holds, snapshot, "libzfs_check_hold");
	fnvlist_add_nvlist(required, "holds", holds);
	fnvlist_add_int32(optional, "cleanup_fd", zfs_fd);

	IOC_INPUT_TEST(ZFS_IOC_HOLD, pool, required, optional, 0);

	nvlist_free(holds);
	nvlist_free(optional);
	nvlist_free(required);
}

static void
test_get_holds(const char *snapshot)
{
	IOC_INPUT_TEST(ZFS_IOC_GET_HOLDS, snapshot, NULL, NULL, 0);
}

static void
test_release(const char *pool, const char *snapshot)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *release = fnvlist_alloc();

	fnvlist_add_boolean(release, "libzfs_check_hold");
	fnvlist_add_nvlist(required, snapshot, release);

	IOC_INPUT_TEST_WILD(ZFS_IOC_RELEASE, pool, required, NULL, 0);

	nvlist_free(release);
	nvlist_free(required);
}


static void
test_send_new(const char *snapshot, int fd)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();

	fnvlist_add_int32(required, "fd", fd);

	fnvlist_add_boolean(optional, "largeblockok");
	fnvlist_add_boolean(optional, "embedok");
	fnvlist_add_boolean(optional, "compressok");
	fnvlist_add_boolean(optional, "rawok");

	/*
	 * TODO - Resumable send is harder to set up. So we currently
	 * ignore testing for that variant.
	 */
#if 0
	fnvlist_add_string(optional, "fromsnap", from);
	fnvlist_add_uint64(optional, "resume_object", resumeobj);
	fnvlist_add_uint64(optional, "resume_offset", offset);
	fnvlist_add_boolean(optional, "savedok");
#endif
	IOC_INPUT_TEST(ZFS_IOC_SEND_NEW, snapshot, required, optional, 0);

	nvlist_free(optional);
	nvlist_free(required);
}

static void
test_recv_new(const char *dataset, int fd)
{
	dmu_replay_record_t drr;
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *props = fnvlist_alloc();
	char snapshot[MAXNAMELEN + 32];
	ssize_t count;

	memset(&drr, 0, sizeof (dmu_replay_record_t));

	int cleanup_fd = open(ZFS_DEV, O_RDWR);
	if (cleanup_fd == -1) {
		(void) fprintf(stderr, "open(%s) failed: %s\n", ZFS_DEV,
		    strerror(errno));
		exit(EXIT_FAILURE);
	}
	(void) snprintf(snapshot, sizeof (snapshot), "%s@replicant", dataset);

	count = pread(fd, &drr, sizeof (drr), 0);
	if (count != sizeof (drr)) {
		(void) fprintf(stderr, "could not read stream: %s\n",
		    strerror(errno));
	}

	fnvlist_add_string(required, "snapname", snapshot);
	fnvlist_add_byte_array(required, "begin_record", (uchar_t *)&drr,
	    sizeof (drr));
	fnvlist_add_int32(required, "input_fd", fd);

	fnvlist_add_string(props, "org.openzfs:launch", "September 17th, 2013");
	fnvlist_add_nvlist(optional, "localprops", props);
	fnvlist_add_boolean(optional, "force");
	fnvlist_add_boolean(optional, "heal");
	fnvlist_add_int32(optional, "cleanup_fd", cleanup_fd);

	/*
	 * TODO - Resumable receive is harder to set up. So we currently
	 * ignore testing for one.
	 */
#if 0
	fnvlist_add_nvlist(optional, "props", recvdprops);
	fnvlist_add_string(optional, "origin", origin);
	fnvlist_add_boolean(optional, "resumable");
	fnvlist_add_uint64(optional, "action_handle", *action_handle);
#endif
	IOC_INPUT_TEST(ZFS_IOC_RECV_NEW, dataset, required, optional,
	    ENOTSUP);

	nvlist_free(props);
	nvlist_free(optional);
	nvlist_free(required);

	(void) close(cleanup_fd);
}

static void
test_send_space(const char *snapshot1, const char *snapshot2)
{
	nvlist_t *optional = fnvlist_alloc();

	fnvlist_add_string(optional, "from", snapshot1);
	fnvlist_add_boolean(optional, "largeblockok");
	fnvlist_add_boolean(optional, "embedok");
	fnvlist_add_boolean(optional, "compressok");
	fnvlist_add_boolean(optional, "rawok");

	IOC_INPUT_TEST(ZFS_IOC_SEND_SPACE, snapshot2, NULL, optional, 0);

	nvlist_free(optional);
}

static void
test_remap(const char *dataset)
{
	IOC_INPUT_TEST(ZFS_IOC_REMAP, dataset, NULL, NULL, 0);
}

static void
test_channel_program(const char *pool)
{
	const char *program =
	    "arg = ...\n"
	    "argv = arg[\"argv\"]\n"
	    "return argv[1]";
	const char *const argv[1] = { "Hello World!" };
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *args = fnvlist_alloc();

	fnvlist_add_string(required, "program", program);
	fnvlist_add_string_array(args, "argv", argv, 1);
	fnvlist_add_nvlist(required, "arg", args);

	fnvlist_add_boolean_value(optional, "sync", B_TRUE);
	fnvlist_add_uint64(optional, "instrlimit", 1000 * 1000);
	fnvlist_add_uint64(optional, "memlimit", 8192 * 1024);

	IOC_INPUT_TEST(ZFS_IOC_CHANNEL_PROGRAM, pool, required, optional, 0);

	nvlist_free(args);
	nvlist_free(optional);
	nvlist_free(required);
}

#define	WRAPPING_KEY_LEN	32

static void
test_load_key(const char *dataset)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *hidden = fnvlist_alloc();
	uint8_t keydata[WRAPPING_KEY_LEN] = {0};

	fnvlist_add_uint8_array(hidden, "wkeydata", keydata, sizeof (keydata));
	fnvlist_add_nvlist(required, "hidden_args", hidden);
	fnvlist_add_boolean(optional, "noop");

	IOC_INPUT_TEST(ZFS_IOC_LOAD_KEY, dataset, required, optional, EINVAL);
	nvlist_free(hidden);
	nvlist_free(optional);
	nvlist_free(required);
}

static void
test_change_key(const char *dataset)
{
	IOC_INPUT_TEST(ZFS_IOC_CHANGE_KEY, dataset, NULL, NULL, EINVAL);
}

static void
test_unload_key(const char *dataset)
{
	IOC_INPUT_TEST(ZFS_IOC_UNLOAD_KEY, dataset, NULL, NULL, EACCES);
}

static void
test_vdev_initialize(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *vdev_guids = fnvlist_alloc();

	fnvlist_add_uint64(vdev_guids, "path", 0xdeadbeefdeadbeef);
	fnvlist_add_uint64(required, ZPOOL_INITIALIZE_COMMAND,
	    POOL_INITIALIZE_START);
	fnvlist_add_nvlist(required, ZPOOL_INITIALIZE_VDEVS, vdev_guids);

	IOC_INPUT_TEST(ZFS_IOC_POOL_INITIALIZE, pool, required, NULL, EINVAL);
	nvlist_free(vdev_guids);
	nvlist_free(required);
}

static void
test_vdev_trim(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();
	nvlist_t *vdev_guids = fnvlist_alloc();

	fnvlist_add_uint64(vdev_guids, "path", 0xdeadbeefdeadbeef);
	fnvlist_add_uint64(required, ZPOOL_TRIM_COMMAND, POOL_TRIM_START);
	fnvlist_add_nvlist(required, ZPOOL_TRIM_VDEVS, vdev_guids);
	fnvlist_add_uint64(optional, ZPOOL_TRIM_RATE, 1ULL << 30);
	fnvlist_add_boolean_value(optional, ZPOOL_TRIM_SECURE, B_TRUE);

	IOC_INPUT_TEST(ZFS_IOC_POOL_TRIM, pool, required, optional, EINVAL);
	nvlist_free(vdev_guids);
	nvlist_free(optional);
	nvlist_free(required);
}

/* Test with invalid values */
static void
test_scrub(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();
	fnvlist_add_uint64(required, "scan_type", POOL_SCAN_FUNCS + 1);
	fnvlist_add_uint64(required, "scan_command", POOL_SCRUB_FLAGS_END + 1);
	IOC_INPUT_TEST(ZFS_IOC_POOL_SCRUB, pool, required, NULL, EINVAL);
	nvlist_free(required);
}

static int
zfs_destroy(const char *dataset)
{
	zfs_cmd_t zc = {"\0"};
	int err;

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	zc.zc_name[sizeof (zc.zc_name) - 1] = '\0';
	err = lzc_ioctl_fd(zfs_fd, ZFS_IOC_DESTROY, &zc);

	return (err == 0 ? 0 : errno);
}

static void
test_redact(const char *snapshot1, const char *snapshot2)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *snapnv = fnvlist_alloc();
	char bookmark[MAXNAMELEN + 32];

	fnvlist_add_string(required, "bookname", "testbookmark");
	fnvlist_add_boolean(snapnv, snapshot2);
	fnvlist_add_nvlist(required, "snapnv", snapnv);

	IOC_INPUT_TEST(ZFS_IOC_REDACT, snapshot1, required, NULL, 0);

	nvlist_free(snapnv);
	nvlist_free(required);

	strlcpy(bookmark, snapshot1, sizeof (bookmark));
	*strchr(bookmark, '@') = '\0';
	strlcat(bookmark, "#testbookmark", sizeof (bookmark) -
	    strlen(bookmark));
	zfs_destroy(bookmark);
}

static void
test_get_bookmark_props(const char *bookmark)
{
	IOC_INPUT_TEST(ZFS_IOC_GET_BOOKMARK_PROPS, bookmark, NULL, NULL, 0);
}

static void
test_wait(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();
	nvlist_t *optional = fnvlist_alloc();

	fnvlist_add_int32(required, "wait_activity", 2);
	fnvlist_add_uint64(optional, "wait_tag", 0xdeadbeefdeadbeef);

	IOC_INPUT_TEST(ZFS_IOC_WAIT, pool, required, optional, EINVAL);

	nvlist_free(required);
	nvlist_free(optional);
}

static void
test_wait_fs(const char *dataset)
{
	nvlist_t *required = fnvlist_alloc();

	fnvlist_add_int32(required, "wait_activity", 2);

	IOC_INPUT_TEST(ZFS_IOC_WAIT_FS, dataset, required, NULL, EINVAL);

	nvlist_free(required);
}

static void
test_get_bootenv(const char *pool)
{
	IOC_INPUT_TEST(ZFS_IOC_GET_BOOTENV, pool, NULL, NULL, 0);
}

static void
test_set_bootenv(const char *pool)
{
	nvlist_t *required = fnvlist_alloc();

	fnvlist_add_uint64(required, "version", ZFS_BE_VERSION_GRUBENV);
	fnvlist_add_string(required, ZFS_BE_GRUB_ENVMAP, "test");

	IOC_INPUT_TEST_WILD(ZFS_IOC_SET_BOOTENV, pool, required, NULL, 0);

	nvlist_free(required);
}

static void
zfs_ioc_input_tests(const char *pool)
{
	char filepath[] = "/tmp/ioc_test_file_XXXXXX";
	char dataset[ZFS_MAX_DATASET_NAME_LEN];
	char snapbase[ZFS_MAX_DATASET_NAME_LEN + 32];
	char snapshot[ZFS_MAX_DATASET_NAME_LEN + 32];
	char bookmark[ZFS_MAX_DATASET_NAME_LEN + 32];
	char backup[ZFS_MAX_DATASET_NAME_LEN];
	char clone[ZFS_MAX_DATASET_NAME_LEN];
	char clonesnap[ZFS_MAX_DATASET_NAME_LEN + 32];
	int tmpfd, err;

	/*
	 * Setup names and create a working dataset
	 */
	(void) snprintf(dataset, sizeof (dataset), "%s/test-fs", pool);
	(void) snprintf(snapbase, sizeof (snapbase), "%s@snapbase", dataset);
	(void) snprintf(snapshot, sizeof (snapshot), "%s@snapshot", dataset);
	(void) snprintf(bookmark, sizeof (bookmark), "%s#bookmark", dataset);
	(void) snprintf(clone, sizeof (clone), "%s/test-fs-clone", pool);
	(void) snprintf(clonesnap, sizeof (clonesnap), "%s@snap", clone);
	(void) snprintf(backup, sizeof (backup), "%s/backup", pool);

	err = lzc_create(dataset, LZC_DATSET_TYPE_ZFS, NULL, NULL, -1);
	if (err) {
		(void) fprintf(stderr, "could not create '%s': %s\n",
		    dataset, strerror(errno));
		exit(2);
	}

	tmpfd = mkstemp(filepath);
	if (tmpfd < 0) {
		(void) fprintf(stderr, "could not create '%s': %s\n",
		    filepath, strerror(errno));
		exit(2);
	}

	/*
	 * run a test for each ioctl
	 * Note that some test build on previous test operations
	 */
	test_pool_sync(pool);
	test_pool_reopen(pool);
	test_pool_checkpoint(pool);
	test_pool_discard_checkpoint(pool);
	test_log_history(pool);

	test_create(dataset);
	test_snapshot(pool, snapbase);
	test_snapshot(pool, snapshot);

	test_space_snaps(snapshot);
	test_send_space(snapbase, snapshot);
	test_send_new(snapshot, tmpfd);
	test_recv_new(backup, tmpfd);

	test_bookmark(pool, snapshot, bookmark);
	test_get_bookmarks(dataset);
	test_get_bookmark_props(bookmark);
	test_destroy_bookmarks(pool, bookmark);

	test_hold(pool, snapshot);
	test_get_holds(snapshot);
	test_release(pool, snapshot);

	test_clone(snapshot, clone);
	test_snapshot(pool, clonesnap);
	test_redact(snapshot, clonesnap);
	zfs_destroy(clonesnap);
	zfs_destroy(clone);

	test_rollback(dataset, snapshot);
	test_destroy_snaps(pool, snapshot);
	test_destroy_snaps(pool, snapbase);

	test_remap(dataset);
	test_channel_program(pool);

	test_load_key(dataset);
	test_change_key(dataset);
	test_unload_key(dataset);

	test_vdev_initialize(pool);
	test_vdev_trim(pool);

	test_wait(pool);
	test_wait_fs(dataset);

	test_set_bootenv(pool);
	test_get_bootenv(pool);

	test_scrub(pool);

	/*
	 * cleanup
	 */
	zfs_cmd_t zc = {"\0"};

	nvlist_t *snaps = fnvlist_alloc();
	fnvlist_add_boolean(snaps, snapshot);
	(void) lzc_destroy_snaps(snaps, B_FALSE, NULL);
	nvlist_free(snaps);

	(void) zfs_destroy(dataset);
	(void) zfs_destroy(backup);

	(void) close(tmpfd);
	(void) unlink(filepath);

	/*
	 * All the unused slots should yield ZFS_ERR_IOC_CMD_UNAVAIL
	 */
	for (int i = 0; i < ARRAY_SIZE(ioc_skip); i++) {
		if (ioc_tested[ioc_skip[i] - ZFS_IOC_FIRST])
			(void) fprintf(stderr, "cmd %d tested, not skipped!\n",
			    (int)(ioc_skip[i] - ZFS_IOC_FIRST));

		ioc_tested[ioc_skip[i] - ZFS_IOC_FIRST] = B_TRUE;
	}

	(void) strlcpy(zc.zc_name, pool, sizeof (zc.zc_name));
	zc.zc_name[sizeof (zc.zc_name) - 1] = '\0';

	for (unsigned ioc = ZFS_IOC_FIRST; ioc < ZFS_IOC_LAST; ioc++) {
		unsigned cmd = ioc - ZFS_IOC_FIRST;

		if (ioc_tested[cmd])
			continue;

		if (lzc_ioctl_fd(zfs_fd, ioc, &zc) != 0 &&
		    errno != ZFS_ERR_IOC_CMD_UNAVAIL) {
			(void) fprintf(stderr, "cmd %d is missing a test case "
			    "(%d)\n", cmd, errno);
		}
	}
}

enum zfs_ioc_ref {
#ifdef __FreeBSD__
	ZFS_IOC_BASE = 0,
#else
	ZFS_IOC_BASE = ('Z' << 8),
#endif
	ZFS_IOC_PLATFORM_BASE = ZFS_IOC_BASE + 0x80,
};

/*
 * Canonical reference check of /dev/zfs ioctl numbers.
 * These cannot change and new ioctl numbers must be appended.
 */
static boolean_t
validate_ioc_values(void)
{
	boolean_t result = B_TRUE;

#define	CHECK(expr) do { \
	if (!(expr)) { \
		result = B_FALSE; \
		fprintf(stderr, "(%s) === FALSE\n", #expr); \
	} \
} while (0)

	CHECK(ZFS_IOC_BASE + 0 == ZFS_IOC_POOL_CREATE);
	CHECK(ZFS_IOC_BASE + 1 == ZFS_IOC_POOL_DESTROY);
	CHECK(ZFS_IOC_BASE + 2 == ZFS_IOC_POOL_IMPORT);
	CHECK(ZFS_IOC_BASE + 3 == ZFS_IOC_POOL_EXPORT);
	CHECK(ZFS_IOC_BASE + 4 == ZFS_IOC_POOL_CONFIGS);
	CHECK(ZFS_IOC_BASE + 5 == ZFS_IOC_POOL_STATS);
	CHECK(ZFS_IOC_BASE + 6 == ZFS_IOC_POOL_TRYIMPORT);
	CHECK(ZFS_IOC_BASE + 7 == ZFS_IOC_POOL_SCAN);
	CHECK(ZFS_IOC_BASE + 8 == ZFS_IOC_POOL_FREEZE);
	CHECK(ZFS_IOC_BASE + 9 == ZFS_IOC_POOL_UPGRADE);
	CHECK(ZFS_IOC_BASE + 10 == ZFS_IOC_POOL_GET_HISTORY);
	CHECK(ZFS_IOC_BASE + 11 == ZFS_IOC_VDEV_ADD);
	CHECK(ZFS_IOC_BASE + 12 == ZFS_IOC_VDEV_REMOVE);
	CHECK(ZFS_IOC_BASE + 13 == ZFS_IOC_VDEV_SET_STATE);
	CHECK(ZFS_IOC_BASE + 14 == ZFS_IOC_VDEV_ATTACH);
	CHECK(ZFS_IOC_BASE + 15 == ZFS_IOC_VDEV_DETACH);
	CHECK(ZFS_IOC_BASE + 16 == ZFS_IOC_VDEV_SETPATH);
	CHECK(ZFS_IOC_BASE + 17 == ZFS_IOC_VDEV_SETFRU);
	CHECK(ZFS_IOC_BASE + 18 == ZFS_IOC_OBJSET_STATS);
	CHECK(ZFS_IOC_BASE + 19 == ZFS_IOC_OBJSET_ZPLPROPS);
	CHECK(ZFS_IOC_BASE + 20 == ZFS_IOC_DATASET_LIST_NEXT);
	CHECK(ZFS_IOC_BASE + 21 == ZFS_IOC_SNAPSHOT_LIST_NEXT);
	CHECK(ZFS_IOC_BASE + 22 == ZFS_IOC_SET_PROP);
	CHECK(ZFS_IOC_BASE + 23 == ZFS_IOC_CREATE);
	CHECK(ZFS_IOC_BASE + 24 == ZFS_IOC_DESTROY);
	CHECK(ZFS_IOC_BASE + 25 == ZFS_IOC_ROLLBACK);
	CHECK(ZFS_IOC_BASE + 26 == ZFS_IOC_RENAME);
	CHECK(ZFS_IOC_BASE + 27 == ZFS_IOC_RECV);
	CHECK(ZFS_IOC_BASE + 28 == ZFS_IOC_SEND);
	CHECK(ZFS_IOC_BASE + 29 == ZFS_IOC_INJECT_FAULT);
	CHECK(ZFS_IOC_BASE + 30 == ZFS_IOC_CLEAR_FAULT);
	CHECK(ZFS_IOC_BASE + 31 == ZFS_IOC_INJECT_LIST_NEXT);
	CHECK(ZFS_IOC_BASE + 32 == ZFS_IOC_ERROR_LOG);
	CHECK(ZFS_IOC_BASE + 33 == ZFS_IOC_CLEAR);
	CHECK(ZFS_IOC_BASE + 34 == ZFS_IOC_PROMOTE);
	CHECK(ZFS_IOC_BASE + 35 == ZFS_IOC_SNAPSHOT);
	CHECK(ZFS_IOC_BASE + 36 == ZFS_IOC_DSOBJ_TO_DSNAME);
	CHECK(ZFS_IOC_BASE + 37 == ZFS_IOC_OBJ_TO_PATH);
	CHECK(ZFS_IOC_BASE + 38 == ZFS_IOC_POOL_SET_PROPS);
	CHECK(ZFS_IOC_BASE + 39 == ZFS_IOC_POOL_GET_PROPS);
	CHECK(ZFS_IOC_BASE + 40 == ZFS_IOC_SET_FSACL);
	CHECK(ZFS_IOC_BASE + 41 == ZFS_IOC_GET_FSACL);
	CHECK(ZFS_IOC_BASE + 42 == ZFS_IOC_SHARE);
	CHECK(ZFS_IOC_BASE + 43 == ZFS_IOC_INHERIT_PROP);
	CHECK(ZFS_IOC_BASE + 44 == ZFS_IOC_SMB_ACL);
	CHECK(ZFS_IOC_BASE + 45 == ZFS_IOC_USERSPACE_ONE);
	CHECK(ZFS_IOC_BASE + 46 == ZFS_IOC_USERSPACE_MANY);
	CHECK(ZFS_IOC_BASE + 47 == ZFS_IOC_USERSPACE_UPGRADE);
	CHECK(ZFS_IOC_BASE + 48 == ZFS_IOC_HOLD);
	CHECK(ZFS_IOC_BASE + 49 == ZFS_IOC_RELEASE);
	CHECK(ZFS_IOC_BASE + 50 == ZFS_IOC_GET_HOLDS);
	CHECK(ZFS_IOC_BASE + 51 == ZFS_IOC_OBJSET_RECVD_PROPS);
	CHECK(ZFS_IOC_BASE + 52 == ZFS_IOC_VDEV_SPLIT);
	CHECK(ZFS_IOC_BASE + 53 == ZFS_IOC_NEXT_OBJ);
	CHECK(ZFS_IOC_BASE + 54 == ZFS_IOC_DIFF);
	CHECK(ZFS_IOC_BASE + 55 == ZFS_IOC_TMP_SNAPSHOT);
	CHECK(ZFS_IOC_BASE + 56 == ZFS_IOC_OBJ_TO_STATS);
	CHECK(ZFS_IOC_BASE + 57 == ZFS_IOC_SPACE_WRITTEN);
	CHECK(ZFS_IOC_BASE + 58 == ZFS_IOC_SPACE_SNAPS);
	CHECK(ZFS_IOC_BASE + 59 == ZFS_IOC_DESTROY_SNAPS);
	CHECK(ZFS_IOC_BASE + 60 == ZFS_IOC_POOL_REGUID);
	CHECK(ZFS_IOC_BASE + 61 == ZFS_IOC_POOL_REOPEN);
	CHECK(ZFS_IOC_BASE + 62 == ZFS_IOC_SEND_PROGRESS);
	CHECK(ZFS_IOC_BASE + 63 == ZFS_IOC_LOG_HISTORY);
	CHECK(ZFS_IOC_BASE + 64 == ZFS_IOC_SEND_NEW);
	CHECK(ZFS_IOC_BASE + 65 == ZFS_IOC_SEND_SPACE);
	CHECK(ZFS_IOC_BASE + 66 == ZFS_IOC_CLONE);
	CHECK(ZFS_IOC_BASE + 67 == ZFS_IOC_BOOKMARK);
	CHECK(ZFS_IOC_BASE + 68 == ZFS_IOC_GET_BOOKMARKS);
	CHECK(ZFS_IOC_BASE + 69 == ZFS_IOC_DESTROY_BOOKMARKS);
	CHECK(ZFS_IOC_BASE + 70 == ZFS_IOC_RECV_NEW);
	CHECK(ZFS_IOC_BASE + 71 == ZFS_IOC_POOL_SYNC);
	CHECK(ZFS_IOC_BASE + 72 == ZFS_IOC_CHANNEL_PROGRAM);
	CHECK(ZFS_IOC_BASE + 73 == ZFS_IOC_LOAD_KEY);
	CHECK(ZFS_IOC_BASE + 74 == ZFS_IOC_UNLOAD_KEY);
	CHECK(ZFS_IOC_BASE + 75 == ZFS_IOC_CHANGE_KEY);
	CHECK(ZFS_IOC_BASE + 76 == ZFS_IOC_REMAP);
	CHECK(ZFS_IOC_BASE + 77 == ZFS_IOC_POOL_CHECKPOINT);
	CHECK(ZFS_IOC_BASE + 78 == ZFS_IOC_POOL_DISCARD_CHECKPOINT);
	CHECK(ZFS_IOC_BASE + 79 == ZFS_IOC_POOL_INITIALIZE);
	CHECK(ZFS_IOC_BASE + 80 == ZFS_IOC_POOL_TRIM);
	CHECK(ZFS_IOC_BASE + 81 == ZFS_IOC_REDACT);
	CHECK(ZFS_IOC_BASE + 82 == ZFS_IOC_GET_BOOKMARK_PROPS);
	CHECK(ZFS_IOC_BASE + 83 == ZFS_IOC_WAIT);
	CHECK(ZFS_IOC_BASE + 84 == ZFS_IOC_WAIT_FS);
	CHECK(ZFS_IOC_BASE + 87 == ZFS_IOC_POOL_SCRUB);
	CHECK(ZFS_IOC_PLATFORM_BASE + 1 == ZFS_IOC_EVENTS_NEXT);
	CHECK(ZFS_IOC_PLATFORM_BASE + 2 == ZFS_IOC_EVENTS_CLEAR);
	CHECK(ZFS_IOC_PLATFORM_BASE + 3 == ZFS_IOC_EVENTS_SEEK);
	CHECK(ZFS_IOC_PLATFORM_BASE + 4 == ZFS_IOC_NEXTBOOT);
	CHECK(ZFS_IOC_PLATFORM_BASE + 5 == ZFS_IOC_JAIL);
	CHECK(ZFS_IOC_PLATFORM_BASE + 6 == ZFS_IOC_UNJAIL);
	CHECK(ZFS_IOC_PLATFORM_BASE + 7 == ZFS_IOC_SET_BOOTENV);
	CHECK(ZFS_IOC_PLATFORM_BASE + 8 == ZFS_IOC_GET_BOOTENV);

#undef CHECK

	return (result);
}

int
main(int argc, const char *argv[])
{
	if (argc != 2) {
		(void) fprintf(stderr, "usage: %s <pool>\n", argv[0]);
		exit(2);
	}

	if (!validate_ioc_values()) {
		(void) fprintf(stderr, "WARNING: zfs_ioc_t has binary "
		    "incompatible command values\n");
		exit(3);
	}

	(void) libzfs_core_init();
	zfs_fd = open(ZFS_DEV, O_RDWR);
	if (zfs_fd < 0) {
		(void) fprintf(stderr, "open: %s\n", strerror(errno));
		libzfs_core_fini();
		exit(2);
	}

	zfs_ioc_input_tests(argv[1]);

	(void) close(zfs_fd);
	libzfs_core_fini();

	return (unexpected_failures);
}
