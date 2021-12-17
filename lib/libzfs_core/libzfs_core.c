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
 * Copyright (c) 2012, 2020 by Delphix. All rights reserved.
 * Copyright (c) 2013 Steven Hartland. All rights reserved.
 * Copyright 2017 RackTop Systems.
 * Copyright (c) 2017 Open-E, Inc. All Rights Reserved.
 * Copyright (c) 2019, 2020 by Christian Schwarz. All rights reserved.
 * Copyright (c) 2019 Datto Inc.
 */

/*
 * LibZFS_Core (lzc) is intended to replace most functionality in libzfs.
 * It has the following characteristics:
 *
 *  - Thread Safe.  libzfs_core is accessible concurrently from multiple
 *  threads.  This is accomplished primarily by avoiding global data
 *  (e.g. caching).  Since it's thread-safe, there is no reason for a
 *  process to have multiple libzfs "instances".  Therefore, we store
 *  our few pieces of data (e.g. the file descriptor) in global
 *  variables.  The fd is reference-counted so that the libzfs_core
 *  library can be "initialized" multiple times (e.g. by different
 *  consumers within the same process).
 *
 *  - Committed Interface.  The libzfs_core interface will be committed,
 *  therefore consumers can compile against it and be confident that
 *  their code will continue to work on future releases of this code.
 *  Currently, the interface is Evolving (not Committed), but we intend
 *  to commit to it once it is more complete and we determine that it
 *  meets the needs of all consumers.
 *
 *  - Programmatic Error Handling.  libzfs_core communicates errors with
 *  defined error numbers, and doesn't print anything to stdout/stderr.
 *
 *  - Thin Layer.  libzfs_core is a thin layer, marshaling arguments
 *  to/from the kernel ioctls.  There is generally a 1:1 correspondence
 *  between libzfs_core functions and ioctls to ZFS_DEV.
 *
 *  - Clear Atomicity.  Because libzfs_core functions are generally 1:1
 *  with kernel ioctls, and kernel ioctls are general atomic, each
 *  libzfs_core function is atomic.  For example, creating multiple
 *  snapshots with a single call to lzc_snapshot() is atomic -- it
 *  can't fail with only some of the requested snapshots created, even
 *  in the event of power loss or system crash.
 *
 *  - Continued libzfs Support.  Some higher-level operations (e.g.
 *  support for "zfs send -R") are too complicated to fit the scope of
 *  libzfs_core.  This functionality will continue to live in libzfs.
 *  Where appropriate, libzfs will use the underlying atomic operations
 *  of libzfs_core.  For example, libzfs may implement "zfs send -R |
 *  zfs receive" by using individual "send one snapshot", rename,
 *  destroy, and "receive one snapshot" operations in libzfs_core.
 *  /sbin/zfs and /sbin/zpool will link with both libzfs and
 *  libzfs_core.  Other consumers should aim to use only libzfs_core,
 *  since that will be the supported, stable interface going forwards.
 */

#include <libzfs_core.h>
#include <ctype.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#ifdef ZFS_DEBUG
#include <stdio.h>
#endif
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <libzutil.h>
#include <sys/nvpair.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/zfs_ioctl.h>
#if __FreeBSD__
#define	BIG_PIPE_SIZE (64 * 1024) /* From sys/pipe.h */
#endif

static int g_fd = -1;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_refcount;

#ifdef ZFS_DEBUG
static zfs_ioc_t fail_ioc_cmd = ZFS_IOC_LAST;
static zfs_errno_t fail_ioc_err;

static void
libzfs_core_debug_ioc(void)
{
	/*
	 * To test running newer user space binaries with kernel's
	 * that don't yet support an ioctl or a new ioctl arg we
	 * provide an override to intentionally fail an ioctl.
	 *
	 * USAGE:
	 * The override variable, ZFS_IOC_TEST, is of the form "cmd:err"
	 *
	 * For example, to fail a ZFS_IOC_POOL_CHECKPOINT with a
	 * ZFS_ERR_IOC_CMD_UNAVAIL, the string would be "0x5a4d:1029"
	 *
	 * $ sudo sh -c "ZFS_IOC_TEST=0x5a4d:1029 zpool checkpoint tank"
	 * cannot checkpoint 'tank': the loaded zfs module does not support
	 * this operation. A reboot may be required to enable this operation.
	 */
	if (fail_ioc_cmd == ZFS_IOC_LAST) {
		char *ioc_test = getenv("ZFS_IOC_TEST");
		unsigned int ioc_num = 0, ioc_err = 0;

		if (ioc_test != NULL &&
		    sscanf(ioc_test, "%i:%i", &ioc_num, &ioc_err) == 2 &&
		    ioc_num < ZFS_IOC_LAST)  {
			fail_ioc_cmd = ioc_num;
			fail_ioc_err = ioc_err;
		}
	}
}
#endif

int
libzfs_core_init(void)
{
	(void) pthread_mutex_lock(&g_lock);
	if (g_refcount == 0) {
		g_fd = open(ZFS_DEV, O_RDWR|O_CLOEXEC);
		if (g_fd < 0) {
			(void) pthread_mutex_unlock(&g_lock);
			return (errno);
		}
	}
	g_refcount++;

#ifdef ZFS_DEBUG
	libzfs_core_debug_ioc();
#endif
	(void) pthread_mutex_unlock(&g_lock);
	return (0);
}

void
libzfs_core_fini(void)
{
	(void) pthread_mutex_lock(&g_lock);
	ASSERT3S(g_refcount, >, 0);

	g_refcount--;

	if (g_refcount == 0 && g_fd != -1) {
		(void) close(g_fd);
		g_fd = -1;
	}
	(void) pthread_mutex_unlock(&g_lock);
}

static int
lzc_ioctl(zfs_ioc_t ioc, const char *name,
    nvlist_t *source, nvlist_t **resultp)
{
	zfs_cmd_t zc = {"\0"};
	int error = 0;
	char *packed = NULL;
	size_t size = 0;

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

#ifdef ZFS_DEBUG
	if (ioc == fail_ioc_cmd)
		return (fail_ioc_err);
#endif

	if (name != NULL)
		(void) strlcpy(zc.zc_name, name, sizeof (zc.zc_name));

	if (source != NULL) {
		packed = fnvlist_pack(source, &size);
		zc.zc_nvlist_src = (uint64_t)(uintptr_t)packed;
		zc.zc_nvlist_src_size = size;
	}

	if (resultp != NULL) {
		*resultp = NULL;
		if (ioc == ZFS_IOC_CHANNEL_PROGRAM) {
			zc.zc_nvlist_dst_size = fnvlist_lookup_uint64(source,
			    ZCP_ARG_MEMLIMIT);
		} else {
			zc.zc_nvlist_dst_size = MAX(size * 2, 128 * 1024);
		}
		zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
		    malloc(zc.zc_nvlist_dst_size);
		if (zc.zc_nvlist_dst == (uint64_t)0) {
			error = ENOMEM;
			goto out;
		}
	}

	while (lzc_ioctl_fd(g_fd, ioc, &zc) != 0) {
		/*
		 * If ioctl exited with ENOMEM, we retry the ioctl after
		 * increasing the size of the destination nvlist.
		 *
		 * Channel programs that exit with ENOMEM ran over the
		 * lua memory sandbox; they should not be retried.
		 */
		if (errno == ENOMEM && resultp != NULL &&
		    ioc != ZFS_IOC_CHANNEL_PROGRAM) {
			free((void *)(uintptr_t)zc.zc_nvlist_dst);
			zc.zc_nvlist_dst_size *= 2;
			zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
			    malloc(zc.zc_nvlist_dst_size);
			if (zc.zc_nvlist_dst == (uint64_t)0) {
				error = ENOMEM;
				goto out;
			}
		} else {
			error = errno;
			break;
		}
	}
	if (zc.zc_nvlist_dst_filled && resultp != NULL) {
		*resultp = fnvlist_unpack((void *)(uintptr_t)zc.zc_nvlist_dst,
		    zc.zc_nvlist_dst_size);
	}

out:
	if (packed != NULL)
		fnvlist_pack_free(packed, size);
	free((void *)(uintptr_t)zc.zc_nvlist_dst);
	return (error);
}

int
lzc_scrub(zfs_ioc_t ioc, const char *name,
    nvlist_t *source, nvlist_t **resultp)
{
	return (lzc_ioctl(ioc, name, source, resultp));
}

int
lzc_create(const char *fsname, enum lzc_dataset_type type, nvlist_t *props,
    uint8_t *wkeydata, uint_t wkeylen)
{
	int error;
	nvlist_t *hidden_args = NULL;
	nvlist_t *args = fnvlist_alloc();

	fnvlist_add_int32(args, "type", (dmu_objset_type_t)type);
	if (props != NULL)
		fnvlist_add_nvlist(args, "props", props);

	if (wkeydata != NULL) {
		hidden_args = fnvlist_alloc();
		fnvlist_add_uint8_array(hidden_args, "wkeydata", wkeydata,
		    wkeylen);
		fnvlist_add_nvlist(args, ZPOOL_HIDDEN_ARGS, hidden_args);
	}

	error = lzc_ioctl(ZFS_IOC_CREATE, fsname, args, NULL);
	nvlist_free(hidden_args);
	nvlist_free(args);
	return (error);
}

int
lzc_clone(const char *fsname, const char *origin, nvlist_t *props)
{
	int error;
	nvlist_t *hidden_args = NULL;
	nvlist_t *args = fnvlist_alloc();

	fnvlist_add_string(args, "origin", origin);
	if (props != NULL)
		fnvlist_add_nvlist(args, "props", props);
	error = lzc_ioctl(ZFS_IOC_CLONE, fsname, args, NULL);
	nvlist_free(hidden_args);
	nvlist_free(args);
	return (error);
}

int
lzc_promote(const char *fsname, char *snapnamebuf, int snapnamelen)
{
	/*
	 * The promote ioctl is still legacy, so we need to construct our
	 * own zfs_cmd_t rather than using lzc_ioctl().
	 */
	zfs_cmd_t zc = {"\0"};

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	(void) strlcpy(zc.zc_name, fsname, sizeof (zc.zc_name));
	if (lzc_ioctl_fd(g_fd, ZFS_IOC_PROMOTE, &zc) != 0) {
		int error = errno;
		if (error == EEXIST && snapnamebuf != NULL)
			(void) strlcpy(snapnamebuf, zc.zc_string, snapnamelen);
		return (error);
	}
	return (0);
}

int
lzc_rename(const char *source, const char *target)
{
	zfs_cmd_t zc = {"\0"};
	int error;

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);
	(void) strlcpy(zc.zc_name, source, sizeof (zc.zc_name));
	(void) strlcpy(zc.zc_value, target, sizeof (zc.zc_value));
	error = lzc_ioctl_fd(g_fd, ZFS_IOC_RENAME, &zc);
	if (error != 0)
		error = errno;
	return (error);
}

int
lzc_destroy(const char *fsname)
{
	int error;
	nvlist_t *args = fnvlist_alloc();
	error = lzc_ioctl(ZFS_IOC_DESTROY, fsname, args, NULL);
	nvlist_free(args);
	return (error);
}

/*
 * Creates snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be created.
 * They must all be in the same pool.
 *
 * The props nvlist is properties to set.  Currently only user properties
 * are supported.  { user:prop_name -> string value }
 *
 * The returned results nvlist will have an entry for each snapshot that failed.
 * The value will be the (int32) error code.
 *
 * The return value will be 0 if all snapshots were created, otherwise it will
 * be the errno of a (unspecified) snapshot that failed.
 */
int
lzc_snapshot(nvlist_t *snaps, nvlist_t *props, nvlist_t **errlist)
{
	nvpair_t *elem;
	nvlist_t *args;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	*errlist = NULL;

	/* determine the pool name */
	elem = nvlist_next_nvpair(snaps, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "snaps", snaps);
	if (props != NULL)
		fnvlist_add_nvlist(args, "props", props);

	error = lzc_ioctl(ZFS_IOC_SNAPSHOT, pool, args, errlist);
	nvlist_free(args);

	return (error);
}

/*
 * Destroys snapshots.
 *
 * The keys in the snaps nvlist are the snapshots to be destroyed.
 * They must all be in the same pool.
 *
 * Snapshots that do not exist will be silently ignored.
 *
 * If 'defer' is not set, and a snapshot has user holds or clones, the
 * destroy operation will fail and none of the snapshots will be
 * destroyed.
 *
 * If 'defer' is set, and a snapshot has user holds or clones, it will be
 * marked for deferred destruction, and will be destroyed when the last hold
 * or clone is removed/destroyed.
 *
 * The return value will be 0 if all snapshots were destroyed (or marked for
 * later destruction if 'defer' is set) or didn't exist to begin with.
 *
 * Otherwise the return value will be the errno of a (unspecified) snapshot
 * that failed, no snapshots will be destroyed, and the errlist will have an
 * entry for each snapshot that failed.  The value in the errlist will be
 * the (int32) error code.
 */
int
lzc_destroy_snaps(nvlist_t *snaps, boolean_t defer, nvlist_t **errlist)
{
	nvpair_t *elem;
	nvlist_t *args;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(snaps, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "snaps", snaps);
	if (defer)
		fnvlist_add_boolean(args, "defer");

	error = lzc_ioctl(ZFS_IOC_DESTROY_SNAPS, pool, args, errlist);
	nvlist_free(args);

	return (error);
}

int
lzc_snaprange_space(const char *firstsnap, const char *lastsnap,
    uint64_t *usedp)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;
	char fs[ZFS_MAX_DATASET_NAME_LEN];
	char *atp;

	/* determine the fs name */
	(void) strlcpy(fs, firstsnap, sizeof (fs));
	atp = strchr(fs, '@');
	if (atp == NULL)
		return (EINVAL);
	*atp = '\0';

	args = fnvlist_alloc();
	fnvlist_add_string(args, "firstsnap", firstsnap);

	err = lzc_ioctl(ZFS_IOC_SPACE_SNAPS, lastsnap, args, &result);
	nvlist_free(args);
	if (err == 0)
		*usedp = fnvlist_lookup_uint64(result, "used");
	fnvlist_free(result);

	return (err);
}

boolean_t
lzc_exists(const char *dataset)
{
	/*
	 * The objset_stats ioctl is still legacy, so we need to construct our
	 * own zfs_cmd_t rather than using lzc_ioctl().
	 */
	zfs_cmd_t zc = {"\0"};

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	(void) strlcpy(zc.zc_name, dataset, sizeof (zc.zc_name));
	return (lzc_ioctl_fd(g_fd, ZFS_IOC_OBJSET_STATS, &zc) == 0);
}

/*
 * outnvl is unused.
 * It was added to preserve the function signature in case it is
 * needed in the future.
 */
int
lzc_sync(const char *pool_name, nvlist_t *innvl, nvlist_t **outnvl)
{
	(void) outnvl;
	return (lzc_ioctl(ZFS_IOC_POOL_SYNC, pool_name, innvl, NULL));
}

/*
 * Create "user holds" on snapshots.  If there is a hold on a snapshot,
 * the snapshot can not be destroyed.  (However, it can be marked for deletion
 * by lzc_destroy_snaps(defer=B_TRUE).)
 *
 * The keys in the nvlist are snapshot names.
 * The snapshots must all be in the same pool.
 * The value is the name of the hold (string type).
 *
 * If cleanup_fd is not -1, it must be the result of open(ZFS_DEV, O_EXCL).
 * In this case, when the cleanup_fd is closed (including on process
 * termination), the holds will be released.  If the system is shut down
 * uncleanly, the holds will be released when the pool is next opened
 * or imported.
 *
 * Holds for snapshots which don't exist will be skipped and have an entry
 * added to errlist, but will not cause an overall failure.
 *
 * The return value will be 0 if all holds, for snapshots that existed,
 * were successfully created.
 *
 * Otherwise the return value will be the errno of a (unspecified) hold that
 * failed and no holds will be created.
 *
 * In all cases the errlist will have an entry for each hold that failed
 * (name = snapshot), with its value being the error code (int32).
 */
int
lzc_hold(nvlist_t *holds, int cleanup_fd, nvlist_t **errlist)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	nvlist_t *args;
	nvpair_t *elem;
	int error;

	/* determine the pool name */
	elem = nvlist_next_nvpair(holds, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	args = fnvlist_alloc();
	fnvlist_add_nvlist(args, "holds", holds);
	if (cleanup_fd != -1)
		fnvlist_add_int32(args, "cleanup_fd", cleanup_fd);

	error = lzc_ioctl(ZFS_IOC_HOLD, pool, args, errlist);
	nvlist_free(args);
	return (error);
}

/*
 * Release "user holds" on snapshots.  If the snapshot has been marked for
 * deferred destroy (by lzc_destroy_snaps(defer=B_TRUE)), it does not have
 * any clones, and all the user holds are removed, then the snapshot will be
 * destroyed.
 *
 * The keys in the nvlist are snapshot names.
 * The snapshots must all be in the same pool.
 * The value is an nvlist whose keys are the holds to remove.
 *
 * Holds which failed to release because they didn't exist will have an entry
 * added to errlist, but will not cause an overall failure.
 *
 * The return value will be 0 if the nvl holds was empty or all holds that
 * existed, were successfully removed.
 *
 * Otherwise the return value will be the errno of a (unspecified) hold that
 * failed to release and no holds will be released.
 *
 * In all cases the errlist will have an entry for each hold that failed to
 * to release.
 */
int
lzc_release(nvlist_t *holds, nvlist_t **errlist)
{
	char pool[ZFS_MAX_DATASET_NAME_LEN];
	nvpair_t *elem;

	/* determine the pool name */
	elem = nvlist_next_nvpair(holds, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/@")] = '\0';

	return (lzc_ioctl(ZFS_IOC_RELEASE, pool, holds, errlist));
}

/*
 * Retrieve list of user holds on the specified snapshot.
 *
 * On success, *holdsp will be set to an nvlist which the caller must free.
 * The keys are the names of the holds, and the value is the creation time
 * of the hold (uint64) in seconds since the epoch.
 */
int
lzc_get_holds(const char *snapname, nvlist_t **holdsp)
{
	return (lzc_ioctl(ZFS_IOC_GET_HOLDS, snapname, NULL, holdsp));
}

static unsigned int
max_pipe_buffer(int infd)
{
#if __linux__
	static unsigned int max;
	if (max == 0) {
		max = 1048576; /* fs/pipe.c default */

		FILE *procf = fopen("/proc/sys/fs/pipe-max-size", "re");
		if (procf != NULL) {
			if (fscanf(procf, "%u", &max) <= 0) {
				/* ignore error: max untouched if parse fails */
			}
			fclose(procf);
		}
	}

	unsigned int cur = fcntl(infd, F_GETPIPE_SZ);
	/*
	 * Sadly, Linux has an unfixed deadlock if you do SETPIPE_SZ on a pipe
	 * with data in it.
	 * cf. #13232, https://bugzilla.kernel.org/show_bug.cgi?id=212295
	 *
	 * And since the problem is in waking up the writer, there's nothing
	 * we can do about it from here.
	 *
	 * So if people want to, they can set this, but they
	 * may regret it...
	 */
	if (getenv("ZFS_SET_PIPE_MAX") == NULL)
		return (cur);
	if (cur < max && fcntl(infd, F_SETPIPE_SZ, max) != -1)
		cur = max;
	return (cur);
#else
	/* FreeBSD automatically resizes */
	(void) infd;
	return (BIG_PIPE_SIZE);
#endif
}

#if __linux__
struct send_worker_ctx {
	int from;	/* read end of pipe, with send data; closed on exit */
	int to;		/* original arbitrary output fd; mustn't be a pipe */
};

static void *
send_worker(void *arg)
{
	struct send_worker_ctx *ctx = arg;
	unsigned int bufsiz = max_pipe_buffer(ctx->from);
	ssize_t rd;

	while ((rd = splice(ctx->from, NULL, ctx->to, NULL, bufsiz,
	    SPLICE_F_MOVE | SPLICE_F_MORE)) > 0)
		;

	int err = (rd == -1) ? errno : 0;
	close(ctx->from);
	return ((void *)(uintptr_t)err);
}
#endif

/*
 * Since Linux 5.10, 4d03e3cc59828c82ee89ea6e27a2f3cdf95aaadf
 * ("fs: don't allow kernel reads and writes without iter ops"),
 * ZFS_IOC_SEND* will EINVAL when writing to /dev/null, /dev/zero, &c.
 *
 * This wrapper transparently executes func() with a pipe
 * by spawning a thread to copy from that pipe to the original output
 * in the background.
 *
 * Returns the error from func(), if nonzero,
 * otherwise the error from the thread.
 *
 * No-op if orig_fd is -1, already a pipe (but the buffer size is bumped),
 * and on not-Linux; as such, it is safe to wrap/call wrapped functions
 * in a wrapped context.
 */
int
lzc_send_wrapper(int (*func)(int, void *), int orig_fd, void *data)
{
#if __linux__
	struct stat sb;
	if (orig_fd != -1 && fstat(orig_fd, &sb) == -1)
		return (errno);
	if (orig_fd == -1 || S_ISFIFO(sb.st_mode)) {
		if (orig_fd != -1)
			(void) max_pipe_buffer(orig_fd);
		return (func(orig_fd, data));
	}
	if ((fcntl(orig_fd, F_GETFL) & O_ACCMODE) == O_RDONLY)
		return (errno = EBADF);

	int rw[2];
	if (pipe2(rw, O_CLOEXEC) == -1)
		return (errno);

	int err;
	pthread_t send_thread;
	struct send_worker_ctx ctx = {.from = rw[0], .to = orig_fd};
	if ((err = pthread_create(&send_thread, NULL, send_worker, &ctx))
	    != 0) {
		close(rw[0]);
		close(rw[1]);
		return (errno = err);
	}

	err = func(rw[1], data);

	void *send_err;
	close(rw[1]);
	pthread_join(send_thread, &send_err);
	if (err == 0 && send_err != 0)
		errno = err = (uintptr_t)send_err;

	return (err);
#else
	return (func(orig_fd, data));
#endif
}

/*
 * Generate a zfs send stream for the specified snapshot and write it to
 * the specified file descriptor.
 *
 * "snapname" is the full name of the snapshot to send (e.g. "pool/fs@snap")
 *
 * If "from" is NULL, a full (non-incremental) stream will be sent.
 * If "from" is non-NULL, it must be the full name of a snapshot or
 * bookmark to send an incremental from (e.g. "pool/fs@earlier_snap" or
 * "pool/fs#earlier_bmark").  If non-NULL, the specified snapshot or
 * bookmark must represent an earlier point in the history of "snapname").
 * It can be an earlier snapshot in the same filesystem or zvol as "snapname",
 * or it can be the origin of "snapname"'s filesystem, or an earlier
 * snapshot in the origin, etc.
 *
 * "fd" is the file descriptor to write the send stream to.
 *
 * If "flags" contains LZC_SEND_FLAG_LARGE_BLOCK, the stream is permitted
 * to contain DRR_WRITE records with drr_length > 128K, and DRR_OBJECT
 * records with drr_blksz > 128K.
 *
 * If "flags" contains LZC_SEND_FLAG_EMBED_DATA, the stream is permitted
 * to contain DRR_WRITE_EMBEDDED records with drr_etype==BP_EMBEDDED_TYPE_DATA,
 * which the receiving system must support (as indicated by support
 * for the "embedded_data" feature).
 *
 * If "flags" contains LZC_SEND_FLAG_COMPRESS, the stream is generated by using
 * compressed WRITE records for blocks which are compressed on disk and in
 * memory.  If the lz4_compress feature is active on the sending system, then
 * the receiving system must have that feature enabled as well.
 *
 * If "flags" contains LZC_SEND_FLAG_RAW, the stream is generated, for encrypted
 * datasets, by sending data exactly as it exists on disk.  This allows backups
 * to be taken even if encryption keys are not currently loaded.
 */
int
lzc_send(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags)
{
	return (lzc_send_resume_redacted(snapname, from, fd, flags, 0, 0,
	    NULL));
}

int
lzc_send_redacted(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags, const char *redactbook)
{
	return (lzc_send_resume_redacted(snapname, from, fd, flags, 0, 0,
	    redactbook));
}

int
lzc_send_resume(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff)
{
	return (lzc_send_resume_redacted(snapname, from, fd, flags, resumeobj,
	    resumeoff, NULL));
}

/*
 * snapname: The name of the "tosnap", or the snapshot whose contents we are
 * sending.
 * from: The name of the "fromsnap", or the incremental source.
 * fd: File descriptor to write the stream to.
 * flags: flags that determine features to be used by the stream.
 * resumeobj: Object to resume from, for resuming send
 * resumeoff: Offset to resume from, for resuming send.
 * redactnv: nvlist of string -> boolean(ignored) containing the names of all
 * the snapshots that we should redact with respect to.
 * redactbook: Name of the redaction bookmark to create.
 *
 * Pre-wrapped.
 */
static int
lzc_send_resume_redacted_cb_impl(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff,
    const char *redactbook)
{
	nvlist_t *args;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_int32(args, "fd", fd);
	if (from != NULL)
		fnvlist_add_string(args, "fromsnap", from);
	if (flags & LZC_SEND_FLAG_LARGE_BLOCK)
		fnvlist_add_boolean(args, "largeblockok");
	if (flags & LZC_SEND_FLAG_EMBED_DATA)
		fnvlist_add_boolean(args, "embedok");
	if (flags & LZC_SEND_FLAG_COMPRESS)
		fnvlist_add_boolean(args, "compressok");
	if (flags & LZC_SEND_FLAG_RAW)
		fnvlist_add_boolean(args, "rawok");
	if (flags & LZC_SEND_FLAG_SAVED)
		fnvlist_add_boolean(args, "savedok");
	if (resumeobj != 0 || resumeoff != 0) {
		fnvlist_add_uint64(args, "resume_object", resumeobj);
		fnvlist_add_uint64(args, "resume_offset", resumeoff);
	}
	if (redactbook != NULL)
		fnvlist_add_string(args, "redactbook", redactbook);

	err = lzc_ioctl(ZFS_IOC_SEND_NEW, snapname, args, NULL);
	nvlist_free(args);
	return (err);
}

struct lzc_send_resume_redacted {
	const char *snapname;
	const char *from;
	enum lzc_send_flags flags;
	uint64_t resumeobj;
	uint64_t resumeoff;
	const char *redactbook;
};

static int
lzc_send_resume_redacted_cb(int fd, void *arg)
{
	struct lzc_send_resume_redacted *zsrr = arg;
	return (lzc_send_resume_redacted_cb_impl(zsrr->snapname, zsrr->from,
	    fd, zsrr->flags, zsrr->resumeobj, zsrr->resumeoff,
	    zsrr->redactbook));
}

int
lzc_send_resume_redacted(const char *snapname, const char *from, int fd,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff,
    const char *redactbook)
{
	struct lzc_send_resume_redacted zsrr = {
		.snapname = snapname,
		.from = from,
		.flags = flags,
		.resumeobj = resumeobj,
		.resumeoff = resumeoff,
		.redactbook = redactbook,
	};
	return (lzc_send_wrapper(lzc_send_resume_redacted_cb, fd, &zsrr));
}

/*
 * "from" can be NULL, a snapshot, or a bookmark.
 *
 * If from is NULL, a full (non-incremental) stream will be estimated.  This
 * is calculated very efficiently.
 *
 * If from is a snapshot, lzc_send_space uses the deadlists attached to
 * each snapshot to efficiently estimate the stream size.
 *
 * If from is a bookmark, the indirect blocks in the destination snapshot
 * are traversed, looking for blocks with a birth time since the creation TXG of
 * the snapshot this bookmark was created from.  This will result in
 * significantly more I/O and be less efficient than a send space estimation on
 * an equivalent snapshot. This process is also used if redact_snaps is
 * non-null.
 *
 * Pre-wrapped.
 */
static int
lzc_send_space_resume_redacted_cb_impl(const char *snapname, const char *from,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff,
    uint64_t resume_bytes, const char *redactbook, int fd, uint64_t *spacep)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	if (from != NULL)
		fnvlist_add_string(args, "from", from);
	if (flags & LZC_SEND_FLAG_LARGE_BLOCK)
		fnvlist_add_boolean(args, "largeblockok");
	if (flags & LZC_SEND_FLAG_EMBED_DATA)
		fnvlist_add_boolean(args, "embedok");
	if (flags & LZC_SEND_FLAG_COMPRESS)
		fnvlist_add_boolean(args, "compressok");
	if (flags & LZC_SEND_FLAG_RAW)
		fnvlist_add_boolean(args, "rawok");
	if (resumeobj != 0 || resumeoff != 0) {
		fnvlist_add_uint64(args, "resume_object", resumeobj);
		fnvlist_add_uint64(args, "resume_offset", resumeoff);
		fnvlist_add_uint64(args, "bytes", resume_bytes);
	}
	if (redactbook != NULL)
		fnvlist_add_string(args, "redactbook", redactbook);
	if (fd != -1)
		fnvlist_add_int32(args, "fd", fd);

	err = lzc_ioctl(ZFS_IOC_SEND_SPACE, snapname, args, &result);
	nvlist_free(args);
	if (err == 0)
		*spacep = fnvlist_lookup_uint64(result, "space");
	nvlist_free(result);
	return (err);
}

struct lzc_send_space_resume_redacted {
	const char *snapname;
	const char *from;
	enum lzc_send_flags flags;
	uint64_t resumeobj;
	uint64_t resumeoff;
	uint64_t resume_bytes;
	const char *redactbook;
	uint64_t *spacep;
};

static int
lzc_send_space_resume_redacted_cb(int fd, void *arg)
{
	struct lzc_send_space_resume_redacted *zssrr = arg;
	return (lzc_send_space_resume_redacted_cb_impl(zssrr->snapname,
	    zssrr->from, zssrr->flags, zssrr->resumeobj, zssrr->resumeoff,
	    zssrr->resume_bytes, zssrr->redactbook, fd, zssrr->spacep));
}

int
lzc_send_space_resume_redacted(const char *snapname, const char *from,
    enum lzc_send_flags flags, uint64_t resumeobj, uint64_t resumeoff,
    uint64_t resume_bytes, const char *redactbook, int fd, uint64_t *spacep)
{
	struct lzc_send_space_resume_redacted zssrr = {
		.snapname = snapname,
		.from = from,
		.flags = flags,
		.resumeobj = resumeobj,
		.resumeoff = resumeoff,
		.resume_bytes = resume_bytes,
		.redactbook = redactbook,
		.spacep = spacep,
	};
	return (lzc_send_wrapper(lzc_send_space_resume_redacted_cb,
	    fd, &zssrr));
}

int
lzc_send_space(const char *snapname, const char *from,
    enum lzc_send_flags flags, uint64_t *spacep)
{
	return (lzc_send_space_resume_redacted(snapname, from, flags, 0, 0, 0,
	    NULL, -1, spacep));
}

static int
recv_read(int fd, void *buf, int ilen)
{
	char *cp = buf;
	int rv;
	int len = ilen;

	do {
		rv = read(fd, cp, len);
		cp += rv;
		len -= rv;
	} while (rv > 0);

	if (rv < 0 || len != 0)
		return (EIO);

	return (0);
}

/*
 * Linux adds ZFS_IOC_RECV_NEW for resumable and raw streams and preserves the
 * legacy ZFS_IOC_RECV user/kernel interface.  The new interface supports all
 * stream options but is currently only used for resumable streams.  This way
 * updated user space utilities will interoperate with older kernel modules.
 *
 * Non-Linux OpenZFS platforms have opted to modify the legacy interface.
 */
static int
recv_impl(const char *snapname, nvlist_t *recvdprops, nvlist_t *localprops,
    uint8_t *wkeydata, uint_t wkeylen, const char *origin, boolean_t force,
    boolean_t heal, boolean_t resumable, boolean_t raw, int input_fd,
    const dmu_replay_record_t *begin_record, uint64_t *read_bytes,
    uint64_t *errflags, nvlist_t **errors)
{
	dmu_replay_record_t drr;
	char fsname[MAXPATHLEN];
	char *atp;
	int error;
	boolean_t payload = B_FALSE;

	ASSERT3S(g_refcount, >, 0);
	VERIFY3S(g_fd, !=, -1);

	/* Set 'fsname' to the name of containing filesystem */
	(void) strlcpy(fsname, snapname, sizeof (fsname));
	atp = strchr(fsname, '@');
	if (atp == NULL)
		return (EINVAL);
	*atp = '\0';

	/* If the fs does not exist, try its parent. */
	if (!lzc_exists(fsname)) {
		char *slashp = strrchr(fsname, '/');
		if (slashp == NULL)
			return (ENOENT);
		*slashp = '\0';
	}

	/*
	 * It is not uncommon for gigabytes to be processed by zfs receive.
	 * Speculatively increase the buffer size if supported by the platform.
	 */
	struct stat sb;
	if (fstat(input_fd, &sb) == -1)
		return (errno);
	if (S_ISFIFO(sb.st_mode))
		(void) max_pipe_buffer(input_fd);

	/*
	 * The begin_record is normally a non-byteswapped BEGIN record.
	 * For resumable streams it may be set to any non-byteswapped
	 * dmu_replay_record_t.
	 */
	if (begin_record == NULL) {
		error = recv_read(input_fd, &drr, sizeof (drr));
		if (error != 0)
			return (error);
	} else {
		drr = *begin_record;
		payload = (begin_record->drr_payloadlen != 0);
	}

	/*
	 * All receives with a payload should use the new interface.
	 */
	if (resumable || heal || raw || wkeydata != NULL || payload) {
		nvlist_t *outnvl = NULL;
		nvlist_t *innvl = fnvlist_alloc();

		fnvlist_add_string(innvl, "snapname", snapname);

		if (recvdprops != NULL)
			fnvlist_add_nvlist(innvl, "props", recvdprops);

		if (localprops != NULL)
			fnvlist_add_nvlist(innvl, "localprops", localprops);

		if (wkeydata != NULL) {
			/*
			 * wkeydata must be placed in the special
			 * ZPOOL_HIDDEN_ARGS nvlist so that it
			 * will not be printed to the zpool history.
			 */
			nvlist_t *hidden_args = fnvlist_alloc();
			fnvlist_add_uint8_array(hidden_args, "wkeydata",
			    wkeydata, wkeylen);
			fnvlist_add_nvlist(innvl, ZPOOL_HIDDEN_ARGS,
			    hidden_args);
			nvlist_free(hidden_args);
		}

		if (origin != NULL && strlen(origin))
			fnvlist_add_string(innvl, "origin", origin);

		fnvlist_add_byte_array(innvl, "begin_record",
		    (uchar_t *)&drr, sizeof (drr));

		fnvlist_add_int32(innvl, "input_fd", input_fd);

		if (force)
			fnvlist_add_boolean(innvl, "force");

		if (resumable)
			fnvlist_add_boolean(innvl, "resumable");

		if (heal)
			fnvlist_add_boolean(innvl, "heal");

		error = lzc_ioctl(ZFS_IOC_RECV_NEW, fsname, innvl, &outnvl);

		if (error == 0 && read_bytes != NULL)
			error = nvlist_lookup_uint64(outnvl, "read_bytes",
			    read_bytes);

		if (error == 0 && errflags != NULL)
			error = nvlist_lookup_uint64(outnvl, "error_flags",
			    errflags);

		if (error == 0 && errors != NULL) {
			nvlist_t *nvl;
			error = nvlist_lookup_nvlist(outnvl, "errors", &nvl);
			if (error == 0)
				*errors = fnvlist_dup(nvl);
		}

		fnvlist_free(innvl);
		fnvlist_free(outnvl);
	} else {
		zfs_cmd_t zc = {"\0"};
		char *rp_packed = NULL;
		char *lp_packed = NULL;
		size_t size;

		ASSERT3S(g_refcount, >, 0);

		(void) strlcpy(zc.zc_name, fsname, sizeof (zc.zc_name));
		(void) strlcpy(zc.zc_value, snapname, sizeof (zc.zc_value));

		if (recvdprops != NULL) {
			rp_packed = fnvlist_pack(recvdprops, &size);
			zc.zc_nvlist_src = (uint64_t)(uintptr_t)rp_packed;
			zc.zc_nvlist_src_size = size;
		}

		if (localprops != NULL) {
			lp_packed = fnvlist_pack(localprops, &size);
			zc.zc_nvlist_conf = (uint64_t)(uintptr_t)lp_packed;
			zc.zc_nvlist_conf_size = size;
		}

		if (origin != NULL)
			(void) strlcpy(zc.zc_string, origin,
			    sizeof (zc.zc_string));

		ASSERT3S(drr.drr_type, ==, DRR_BEGIN);
		zc.zc_begin_record = drr.drr_u.drr_begin;
		zc.zc_guid = force;
		zc.zc_cookie = input_fd;
		zc.zc_cleanup_fd = -1;
		zc.zc_action_handle = 0;

		zc.zc_nvlist_dst_size = 128 * 1024;
		zc.zc_nvlist_dst = (uint64_t)(uintptr_t)
		    malloc(zc.zc_nvlist_dst_size);

		error = lzc_ioctl_fd(g_fd, ZFS_IOC_RECV, &zc);
		if (error != 0) {
			error = errno;
		} else {
			if (read_bytes != NULL)
				*read_bytes = zc.zc_cookie;

			if (errflags != NULL)
				*errflags = zc.zc_obj;

			if (errors != NULL)
				VERIFY0(nvlist_unpack(
				    (void *)(uintptr_t)zc.zc_nvlist_dst,
				    zc.zc_nvlist_dst_size, errors, KM_SLEEP));
		}

		if (rp_packed != NULL)
			fnvlist_pack_free(rp_packed, size);
		if (lp_packed != NULL)
			fnvlist_pack_free(lp_packed, size);
		free((void *)(uintptr_t)zc.zc_nvlist_dst);
	}

	return (error);
}

/*
 * The simplest receive case: receive from the specified fd, creating the
 * specified snapshot.  Apply the specified properties as "received" properties
 * (which can be overridden by locally-set properties).  If the stream is a
 * clone, its origin snapshot must be specified by 'origin'.  The 'force'
 * flag will cause the target filesystem to be rolled back or destroyed if
 * necessary to receive.
 *
 * Return 0 on success or an errno on failure.
 *
 * Note: this interface does not work on dedup'd streams
 * (those with DMU_BACKUP_FEATURE_DEDUP).
 */
int
lzc_receive(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, boolean_t raw, int fd)
{
	return (recv_impl(snapname, props, NULL, NULL, 0, origin, force,
	    B_FALSE, B_FALSE, raw, fd, NULL, NULL, NULL, NULL));
}

/*
 * Like lzc_receive, but if the receive fails due to premature stream
 * termination, the intermediate state will be preserved on disk.  In this
 * case, ECKSUM will be returned.  The receive may subsequently be resumed
 * with a resuming send stream generated by lzc_send_resume().
 */
int
lzc_receive_resumable(const char *snapname, nvlist_t *props, const char *origin,
    boolean_t force, boolean_t raw, int fd)
{
	return (recv_impl(snapname, props, NULL, NULL, 0, origin, force,
	    B_FALSE, B_TRUE, raw, fd, NULL, NULL, NULL, NULL));
}

/*
 * Like lzc_receive, but allows the caller to read the begin record and then to
 * pass it in.  That could be useful if the caller wants to derive, for example,
 * the snapname or the origin parameters based on the information contained in
 * the begin record.
 * The begin record must be in its original form as read from the stream,
 * in other words, it should not be byteswapped.
 *
 * The 'resumable' parameter allows to obtain the same behavior as with
 * lzc_receive_resumable.
 */
int
lzc_receive_with_header(const char *snapname, nvlist_t *props,
    const char *origin, boolean_t force, boolean_t resumable, boolean_t raw,
    int fd, const dmu_replay_record_t *begin_record)
{
	if (begin_record == NULL)
		return (EINVAL);

	return (recv_impl(snapname, props, NULL, NULL, 0, origin, force,
	    B_FALSE, resumable, raw, fd, begin_record, NULL, NULL, NULL));
}

/*
 * Like lzc_receive, but allows the caller to pass all supported arguments
 * and retrieve all values returned.  The only additional input parameter
 * is 'cleanup_fd' which is used to set a cleanup-on-exit file descriptor.
 *
 * The following parameters all provide return values.  Several may be set
 * in the failure case and will contain additional information.
 *
 * The 'read_bytes' value will be set to the total number of bytes read.
 *
 * The 'errflags' value will contain zprop_errflags_t flags which are
 * used to describe any failures.
 *
 * The 'action_handle' and 'cleanup_fd' are no longer used, and are ignored.
 *
 * The 'errors' nvlist contains an entry for each unapplied received
 * property.  Callers are responsible for freeing this nvlist.
 */
int
lzc_receive_one(const char *snapname, nvlist_t *props,
    const char *origin, boolean_t force, boolean_t resumable, boolean_t raw,
    int input_fd, const dmu_replay_record_t *begin_record, int cleanup_fd,
    uint64_t *read_bytes, uint64_t *errflags, uint64_t *action_handle,
    nvlist_t **errors)
{
	(void) action_handle, (void) cleanup_fd;
	return (recv_impl(snapname, props, NULL, NULL, 0, origin, force,
	    B_FALSE, resumable, raw, input_fd, begin_record,
	    read_bytes, errflags, errors));
}

/*
 * Like lzc_receive_one, but allows the caller to pass an additional 'cmdprops'
 * argument.
 *
 * The 'cmdprops' nvlist contains both override ('zfs receive -o') and
 * exclude ('zfs receive -x') properties. Callers are responsible for freeing
 * this nvlist
 */
int
lzc_receive_with_cmdprops(const char *snapname, nvlist_t *props,
    nvlist_t *cmdprops, uint8_t *wkeydata, uint_t wkeylen, const char *origin,
    boolean_t force, boolean_t resumable, boolean_t raw, int input_fd,
    const dmu_replay_record_t *begin_record, int cleanup_fd,
    uint64_t *read_bytes, uint64_t *errflags, uint64_t *action_handle,
    nvlist_t **errors)
{
	(void) action_handle, (void) cleanup_fd;
	return (recv_impl(snapname, props, cmdprops, wkeydata, wkeylen, origin,
	    force, B_FALSE, resumable, raw, input_fd, begin_record,
	    read_bytes, errflags, errors));
}

/*
 * Like lzc_receive_with_cmdprops, but allows the caller to pass an additional
 * 'heal' argument.
 *
 * The heal arguments tells us to heal the provided snapshot using the provided
 * send stream
 */
int lzc_receive_with_heal(const char *snapname, nvlist_t *props,
    nvlist_t *cmdprops, uint8_t *wkeydata, uint_t wkeylen, const char *origin,
    boolean_t force, boolean_t heal, boolean_t resumable, boolean_t raw,
    int input_fd, const dmu_replay_record_t *begin_record, int cleanup_fd,
    uint64_t *read_bytes, uint64_t *errflags, uint64_t *action_handle,
    nvlist_t **errors)
{
	(void) action_handle, (void) cleanup_fd;
	return (recv_impl(snapname, props, cmdprops, wkeydata, wkeylen, origin,
	    force, heal, resumable, raw, input_fd, begin_record,
	    read_bytes, errflags, errors));
}

/*
 * Roll back this filesystem or volume to its most recent snapshot.
 * If snapnamebuf is not NULL, it will be filled in with the name
 * of the most recent snapshot.
 * Note that the latest snapshot may change if a new one is concurrently
 * created or the current one is destroyed.  lzc_rollback_to can be used
 * to roll back to a specific latest snapshot.
 *
 * Return 0 on success or an errno on failure.
 */
int
lzc_rollback(const char *fsname, char *snapnamebuf, int snapnamelen)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	err = lzc_ioctl(ZFS_IOC_ROLLBACK, fsname, args, &result);
	nvlist_free(args);
	if (err == 0 && snapnamebuf != NULL) {
		const char *snapname = fnvlist_lookup_string(result, "target");
		(void) strlcpy(snapnamebuf, snapname, snapnamelen);
	}
	nvlist_free(result);

	return (err);
}

/*
 * Roll back this filesystem or volume to the specified snapshot,
 * if possible.
 *
 * Return 0 on success or an errno on failure.
 */
int
lzc_rollback_to(const char *fsname, const char *snapname)
{
	nvlist_t *args;
	nvlist_t *result;
	int err;

	args = fnvlist_alloc();
	fnvlist_add_string(args, "target", snapname);
	err = lzc_ioctl(ZFS_IOC_ROLLBACK, fsname, args, &result);
	nvlist_free(args);
	nvlist_free(result);
	return (err);
}

/*
 * Creates new bookmarks from existing snapshot or bookmark.
 *
 * The bookmarks nvlist maps from the full name of the new bookmark to
 * the full name of the source snapshot or bookmark.
 * All the bookmarks and snapshots must be in the same pool.
 * The new bookmarks names must be unique.
 * => see function dsl_bookmark_create_nvl_validate
 *
 * The returned results nvlist will have an entry for each bookmark that failed.
 * The value will be the (int32) error code.
 *
 * The return value will be 0 if all bookmarks were created, otherwise it will
 * be the errno of a (undetermined) bookmarks that failed.
 */
int
lzc_bookmark(nvlist_t *bookmarks, nvlist_t **errlist)
{
	nvpair_t *elem;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine pool name from first bookmark */
	elem = nvlist_next_nvpair(bookmarks, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/#")] = '\0';

	error = lzc_ioctl(ZFS_IOC_BOOKMARK, pool, bookmarks, errlist);

	return (error);
}

/*
 * Retrieve bookmarks.
 *
 * Retrieve the list of bookmarks for the given file system. The props
 * parameter is an nvlist of property names (with no values) that will be
 * returned for each bookmark.
 *
 * The following are valid properties on bookmarks, most of which are numbers
 * (represented as uint64 in the nvlist), except redact_snaps, which is a
 * uint64 array, and redact_complete, which is a boolean
 *
 * "guid" - globally unique identifier of the snapshot it refers to
 * "createtxg" - txg when the snapshot it refers to was created
 * "creation" - timestamp when the snapshot it refers to was created
 * "ivsetguid" - IVset guid for identifying encrypted snapshots
 * "redact_snaps" - list of guids of the redaction snapshots for the specified
 *     bookmark.  If the bookmark is not a redaction bookmark, the nvlist will
 *     not contain an entry for this value.  If it is redacted with respect to
 *     no snapshots, it will contain value -> NULL uint64 array
 * "redact_complete" - boolean value; true if the redaction bookmark is
 *     complete, false otherwise.
 *
 * The format of the returned nvlist as follows:
 * <short name of bookmark> -> {
 *     <name of property> -> {
 *         "value" -> uint64
 *     }
 *     ...
 *     "redact_snaps" -> {
 *         "value" -> uint64 array
 *     }
 *     "redact_complete" -> {
 *         "value" -> boolean value
 *     }
 *  }
 */
int
lzc_get_bookmarks(const char *fsname, nvlist_t *props, nvlist_t **bmarks)
{
	return (lzc_ioctl(ZFS_IOC_GET_BOOKMARKS, fsname, props, bmarks));
}

/*
 * Get bookmark properties.
 *
 * Given a bookmark's full name, retrieve all properties for the bookmark.
 *
 * The format of the returned property list is as follows:
 * {
 *     <name of property> -> {
 *         "value" -> uint64
 *     }
 *     ...
 *     "redact_snaps" -> {
 *         "value" -> uint64 array
 * }
 */
int
lzc_get_bookmark_props(const char *bookmark, nvlist_t **props)
{
	int error;

	nvlist_t *innvl = fnvlist_alloc();
	error = lzc_ioctl(ZFS_IOC_GET_BOOKMARK_PROPS, bookmark, innvl, props);
	fnvlist_free(innvl);

	return (error);
}

/*
 * Destroys bookmarks.
 *
 * The keys in the bmarks nvlist are the bookmarks to be destroyed.
 * They must all be in the same pool.  Bookmarks are specified as
 * <fs>#<bmark>.
 *
 * Bookmarks that do not exist will be silently ignored.
 *
 * The return value will be 0 if all bookmarks that existed were destroyed.
 *
 * Otherwise the return value will be the errno of a (undetermined) bookmark
 * that failed, no bookmarks will be destroyed, and the errlist will have an
 * entry for each bookmarks that failed.  The value in the errlist will be
 * the (int32) error code.
 */
int
lzc_destroy_bookmarks(nvlist_t *bmarks, nvlist_t **errlist)
{
	nvpair_t *elem;
	int error;
	char pool[ZFS_MAX_DATASET_NAME_LEN];

	/* determine the pool name */
	elem = nvlist_next_nvpair(bmarks, NULL);
	if (elem == NULL)
		return (0);
	(void) strlcpy(pool, nvpair_name(elem), sizeof (pool));
	pool[strcspn(pool, "/#")] = '\0';

	error = lzc_ioctl(ZFS_IOC_DESTROY_BOOKMARKS, pool, bmarks, errlist);

	return (error);
}

static int
lzc_channel_program_impl(const char *pool, const char *program, boolean_t sync,
    uint64_t instrlimit, uint64_t memlimit, nvlist_t *argnvl, nvlist_t **outnvl)
{
	int error;
	nvlist_t *args;

	args = fnvlist_alloc();
	fnvlist_add_string(args, ZCP_ARG_PROGRAM, program);
	fnvlist_add_nvlist(args, ZCP_ARG_ARGLIST, argnvl);
	fnvlist_add_boolean_value(args, ZCP_ARG_SYNC, sync);
	fnvlist_add_uint64(args, ZCP_ARG_INSTRLIMIT, instrlimit);
	fnvlist_add_uint64(args, ZCP_ARG_MEMLIMIT, memlimit);
	error = lzc_ioctl(ZFS_IOC_CHANNEL_PROGRAM, pool, args, outnvl);
	fnvlist_free(args);

	return (error);
}

/*
 * Executes a channel program.
 *
 * If this function returns 0 the channel program was successfully loaded and
 * ran without failing. Note that individual commands the channel program ran
 * may have failed and the channel program is responsible for reporting such
 * errors through outnvl if they are important.
 *
 * This method may also return:
 *
 * EINVAL   The program contains syntax errors, or an invalid memory or time
 *          limit was given. No part of the channel program was executed.
 *          If caused by syntax errors, 'outnvl' contains information about the
 *          errors.
 *
 * ECHRNG   The program was executed, but encountered a runtime error, such as
 *          calling a function with incorrect arguments, invoking the error()
 *          function directly, failing an assert() command, etc. Some portion
 *          of the channel program may have executed and committed changes.
 *          Information about the failure can be found in 'outnvl'.
 *
 * ENOMEM   The program fully executed, but the output buffer was not large
 *          enough to store the returned value. No output is returned through
 *          'outnvl'.
 *
 * ENOSPC   The program was terminated because it exceeded its memory usage
 *          limit. Some portion of the channel program may have executed and
 *          committed changes to disk. No output is returned through 'outnvl'.
 *
 * ETIME    The program was terminated because it exceeded its Lua instruction
 *          limit. Some portion of the channel program may have executed and
 *          committed changes to disk. No output is returned through 'outnvl'.
 */
int
lzc_channel_program(const char *pool, const char *program, uint64_t instrlimit,
    uint64_t memlimit, nvlist_t *argnvl, nvlist_t **outnvl)
{
	return (lzc_channel_program_impl(pool, program, B_TRUE, instrlimit,
	    memlimit, argnvl, outnvl));
}

/*
 * Creates a checkpoint for the specified pool.
 *
 * If this function returns 0 the pool was successfully checkpointed.
 *
 * This method may also return:
 *
 * ZFS_ERR_CHECKPOINT_EXISTS
 *	The pool already has a checkpoint. A pools can only have one
 *	checkpoint at most, at any given time.
 *
 * ZFS_ERR_DISCARDING_CHECKPOINT
 * 	ZFS is in the middle of discarding a checkpoint for this pool.
 * 	The pool can be checkpointed again once the discard is done.
 *
 * ZFS_DEVRM_IN_PROGRESS
 * 	A vdev is currently being removed. The pool cannot be
 * 	checkpointed until the device removal is done.
 *
 * ZFS_VDEV_TOO_BIG
 * 	One or more top-level vdevs exceed the maximum vdev size
 * 	supported for this feature.
 */
int
lzc_pool_checkpoint(const char *pool)
{
	int error;

	nvlist_t *result = NULL;
	nvlist_t *args = fnvlist_alloc();

	error = lzc_ioctl(ZFS_IOC_POOL_CHECKPOINT, pool, args, &result);

	fnvlist_free(args);
	fnvlist_free(result);

	return (error);
}

/*
 * Discard the checkpoint from the specified pool.
 *
 * If this function returns 0 the checkpoint was successfully discarded.
 *
 * This method may also return:
 *
 * ZFS_ERR_NO_CHECKPOINT
 * 	The pool does not have a checkpoint.
 *
 * ZFS_ERR_DISCARDING_CHECKPOINT
 * 	ZFS is already in the middle of discarding the checkpoint.
 */
int
lzc_pool_checkpoint_discard(const char *pool)
{
	int error;

	nvlist_t *result = NULL;
	nvlist_t *args = fnvlist_alloc();

	error = lzc_ioctl(ZFS_IOC_POOL_DISCARD_CHECKPOINT, pool, args, &result);

	fnvlist_free(args);
	fnvlist_free(result);

	return (error);
}

/*
 * Executes a read-only channel program.
 *
 * A read-only channel program works programmatically the same way as a
 * normal channel program executed with lzc_channel_program(). The only
 * difference is it runs exclusively in open-context and therefore can
 * return faster. The downside to that, is that the program cannot change
 * on-disk state by calling functions from the zfs.sync submodule.
 *
 * The return values of this function (and their meaning) are exactly the
 * same as the ones described in lzc_channel_program().
 */
int
lzc_channel_program_nosync(const char *pool, const char *program,
    uint64_t timeout, uint64_t memlimit, nvlist_t *argnvl, nvlist_t **outnvl)
{
	return (lzc_channel_program_impl(pool, program, B_FALSE, timeout,
	    memlimit, argnvl, outnvl));
}

int
lzc_get_vdev_prop(const char *poolname, nvlist_t *innvl, nvlist_t **outnvl)
{
	return (lzc_ioctl(ZFS_IOC_VDEV_GET_PROPS, poolname, innvl, outnvl));
}

int
lzc_set_vdev_prop(const char *poolname, nvlist_t *innvl, nvlist_t **outnvl)
{
	return (lzc_ioctl(ZFS_IOC_VDEV_SET_PROPS, poolname, innvl, outnvl));
}

/*
 * Performs key management functions
 *
 * crypto_cmd should be a value from dcp_cmd_t. If the command specifies to
 * load or change a wrapping key, the key should be specified in the
 * hidden_args nvlist so that it is not logged.
 */
int
lzc_load_key(const char *fsname, boolean_t noop, uint8_t *wkeydata,
    uint_t wkeylen)
{
	int error;
	nvlist_t *ioc_args;
	nvlist_t *hidden_args;

	if (wkeydata == NULL)
		return (EINVAL);

	ioc_args = fnvlist_alloc();
	hidden_args = fnvlist_alloc();
	fnvlist_add_uint8_array(hidden_args, "wkeydata", wkeydata, wkeylen);
	fnvlist_add_nvlist(ioc_args, ZPOOL_HIDDEN_ARGS, hidden_args);
	if (noop)
		fnvlist_add_boolean(ioc_args, "noop");
	error = lzc_ioctl(ZFS_IOC_LOAD_KEY, fsname, ioc_args, NULL);
	nvlist_free(hidden_args);
	nvlist_free(ioc_args);

	return (error);
}

int
lzc_unload_key(const char *fsname)
{
	return (lzc_ioctl(ZFS_IOC_UNLOAD_KEY, fsname, NULL, NULL));
}

int
lzc_change_key(const char *fsname, uint64_t crypt_cmd, nvlist_t *props,
    uint8_t *wkeydata, uint_t wkeylen)
{
	int error;
	nvlist_t *ioc_args = fnvlist_alloc();
	nvlist_t *hidden_args = NULL;

	fnvlist_add_uint64(ioc_args, "crypt_cmd", crypt_cmd);

	if (wkeydata != NULL) {
		hidden_args = fnvlist_alloc();
		fnvlist_add_uint8_array(hidden_args, "wkeydata", wkeydata,
		    wkeylen);
		fnvlist_add_nvlist(ioc_args, ZPOOL_HIDDEN_ARGS, hidden_args);
	}

	if (props != NULL)
		fnvlist_add_nvlist(ioc_args, "props", props);

	error = lzc_ioctl(ZFS_IOC_CHANGE_KEY, fsname, ioc_args, NULL);
	nvlist_free(hidden_args);
	nvlist_free(ioc_args);

	return (error);
}

int
lzc_reopen(const char *pool_name, boolean_t scrub_restart)
{
	nvlist_t *args = fnvlist_alloc();
	int error;

	fnvlist_add_boolean_value(args, "scrub_restart", scrub_restart);

	error = lzc_ioctl(ZFS_IOC_POOL_REOPEN, pool_name, args, NULL);
	nvlist_free(args);
	return (error);
}

/*
 * Changes initializing state.
 *
 * vdevs should be a list of (<key>, guid) where guid is a uint64 vdev GUID.
 * The key is ignored.
 *
 * If there are errors related to vdev arguments, per-vdev errors are returned
 * in an nvlist with the key "vdevs". Each error is a (guid, errno) pair where
 * guid is stringified with PRIu64, and errno is one of the following as
 * an int64_t:
 *	- ENODEV if the device was not found
 *	- EINVAL if the devices is not a leaf or is not concrete (e.g. missing)
 *	- EROFS if the device is not writeable
 *	- EBUSY start requested but the device is already being either
 *	        initialized or trimmed
 *	- ESRCH cancel/suspend requested but device is not being initialized
 *
 * If the errlist is empty, then return value will be:
 *	- EINVAL if one or more arguments was invalid
 *	- Other spa_open failures
 *	- 0 if the operation succeeded
 */
int
lzc_initialize(const char *poolname, pool_initialize_func_t cmd_type,
    nvlist_t *vdevs, nvlist_t **errlist)
{
	int error;

	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_uint64(args, ZPOOL_INITIALIZE_COMMAND, (uint64_t)cmd_type);
	fnvlist_add_nvlist(args, ZPOOL_INITIALIZE_VDEVS, vdevs);

	error = lzc_ioctl(ZFS_IOC_POOL_INITIALIZE, poolname, args, errlist);

	fnvlist_free(args);

	return (error);
}

/*
 * Changes TRIM state.
 *
 * vdevs should be a list of (<key>, guid) where guid is a uint64 vdev GUID.
 * The key is ignored.
 *
 * If there are errors related to vdev arguments, per-vdev errors are returned
 * in an nvlist with the key "vdevs". Each error is a (guid, errno) pair where
 * guid is stringified with PRIu64, and errno is one of the following as
 * an int64_t:
 *	- ENODEV if the device was not found
 *	- EINVAL if the devices is not a leaf or is not concrete (e.g. missing)
 *	- EROFS if the device is not writeable
 *	- EBUSY start requested but the device is already being either trimmed
 *	        or initialized
 *	- ESRCH cancel/suspend requested but device is not being initialized
 *	- EOPNOTSUPP if the device does not support TRIM (or secure TRIM)
 *
 * If the errlist is empty, then return value will be:
 *	- EINVAL if one or more arguments was invalid
 *	- Other spa_open failures
 *	- 0 if the operation succeeded
 */
int
lzc_trim(const char *poolname, pool_trim_func_t cmd_type, uint64_t rate,
    boolean_t secure, nvlist_t *vdevs, nvlist_t **errlist)
{
	int error;

	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_uint64(args, ZPOOL_TRIM_COMMAND, (uint64_t)cmd_type);
	fnvlist_add_nvlist(args, ZPOOL_TRIM_VDEVS, vdevs);
	fnvlist_add_uint64(args, ZPOOL_TRIM_RATE, rate);
	fnvlist_add_boolean_value(args, ZPOOL_TRIM_SECURE, secure);

	error = lzc_ioctl(ZFS_IOC_POOL_TRIM, poolname, args, errlist);

	fnvlist_free(args);

	return (error);
}

/*
 * Create a redaction bookmark named bookname by redacting snapshot with respect
 * to all the snapshots in snapnv.
 */
int
lzc_redact(const char *snapshot, const char *bookname, nvlist_t *snapnv)
{
	nvlist_t *args = fnvlist_alloc();
	fnvlist_add_string(args, "bookname", bookname);
	fnvlist_add_nvlist(args, "snapnv", snapnv);
	int error = lzc_ioctl(ZFS_IOC_REDACT, snapshot, args, NULL);
	fnvlist_free(args);
	return (error);
}

static int
wait_common(const char *pool, zpool_wait_activity_t activity, boolean_t use_tag,
    uint64_t tag, boolean_t *waited)
{
	nvlist_t *args = fnvlist_alloc();
	nvlist_t *result = NULL;

	fnvlist_add_int32(args, ZPOOL_WAIT_ACTIVITY, activity);
	if (use_tag)
		fnvlist_add_uint64(args, ZPOOL_WAIT_TAG, tag);

	int error = lzc_ioctl(ZFS_IOC_WAIT, pool, args, &result);

	if (error == 0 && waited != NULL)
		*waited = fnvlist_lookup_boolean_value(result,
		    ZPOOL_WAIT_WAITED);

	fnvlist_free(args);
	fnvlist_free(result);

	return (error);
}

int
lzc_wait(const char *pool, zpool_wait_activity_t activity, boolean_t *waited)
{
	return (wait_common(pool, activity, B_FALSE, 0, waited));
}

int
lzc_wait_tag(const char *pool, zpool_wait_activity_t activity, uint64_t tag,
    boolean_t *waited)
{
	return (wait_common(pool, activity, B_TRUE, tag, waited));
}

int
lzc_wait_fs(const char *fs, zfs_wait_activity_t activity, boolean_t *waited)
{
	nvlist_t *args = fnvlist_alloc();
	nvlist_t *result = NULL;

	fnvlist_add_int32(args, ZFS_WAIT_ACTIVITY, activity);

	int error = lzc_ioctl(ZFS_IOC_WAIT_FS, fs, args, &result);

	if (error == 0 && waited != NULL)
		*waited = fnvlist_lookup_boolean_value(result,
		    ZFS_WAIT_WAITED);

	fnvlist_free(args);
	fnvlist_free(result);

	return (error);
}

/*
 * Set the bootenv contents for the given pool.
 */
int
lzc_set_bootenv(const char *pool, const nvlist_t *env)
{
	return (lzc_ioctl(ZFS_IOC_SET_BOOTENV, pool, (nvlist_t *)env, NULL));
}

/*
 * Get the contents of the bootenv of the given pool.
 */
int
lzc_get_bootenv(const char *pool, nvlist_t **outnvl)
{
	return (lzc_ioctl(ZFS_IOC_GET_BOOTENV, pool, NULL, outnvl));
}
