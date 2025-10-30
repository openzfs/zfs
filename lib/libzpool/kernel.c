// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2016 Actifio, Inc. All rights reserved.
 * Copyright (c) 2025, Klara, Inc.
 */

#include <assert.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <libzutil.h>
#include <sys/crypto/icp.h>
#include <sys/processor.h>
#include <sys/rrwlock.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/sid.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/time.h>
#include <sys/tsd.h>

#include <libspl.h>
#include <libzpool.h>
#include <sys/zfs_context.h>
#include <sys/zfs_onexit.h>
#include <sys/zfs_vfsops.h>
#include <sys/zstd/zstd.h>
#include <sys/zvol.h>
#include <zfs_fletcher.h>
#include <zlib.h>

/*
 * Emulation of kernel services in userland.
 */

uint32_t hostid;

/* If set, all blocks read will be copied to the specified directory. */
char *vn_dumpdir = NULL;

uint32_t
zone_get_hostid(void *zonep)
{
	/*
	 * We're emulating the system's hostid in userland.
	 */
	(void) zonep;
	return (hostid);
}

/*
 * =========================================================================
 * vnode operations
 * =========================================================================
 */

/*
 * =========================================================================
 * Figure out which debugging statements to print
 * =========================================================================
 */

static char *dprintf_string;
static int dprintf_print_all;

int
dprintf_find_string(const char *string)
{
	char *tmp_str = dprintf_string;
	int len = strlen(string);

	/*
	 * Find out if this is a string we want to print.
	 * String format: file1.c,function_name1,file2.c,file3.c
	 */

	while (tmp_str != NULL) {
		if (strncmp(tmp_str, string, len) == 0 &&
		    (tmp_str[len] == ',' || tmp_str[len] == '\0'))
			return (1);
		tmp_str = strchr(tmp_str, ',');
		if (tmp_str != NULL)
			tmp_str++; /* Get rid of , */
	}
	return (0);
}

void
dprintf_setup(int *argc, char **argv)
{
	int i, j;

	/*
	 * Debugging can be specified two ways: by setting the
	 * environment variable ZFS_DEBUG, or by including a
	 * "debug=..."  argument on the command line.  The command
	 * line setting overrides the environment variable.
	 */

	for (i = 1; i < *argc; i++) {
		int len = strlen("debug=");
		/* First look for a command line argument */
		if (strncmp("debug=", argv[i], len) == 0) {
			dprintf_string = argv[i] + len;
			/* Remove from args */
			for (j = i; j < *argc; j++)
				argv[j] = argv[j+1];
			argv[j] = NULL;
			(*argc)--;
		}
	}

	if (dprintf_string == NULL) {
		/* Look for ZFS_DEBUG environment variable */
		dprintf_string = getenv("ZFS_DEBUG");
	}

	/*
	 * Are we just turning on all debugging?
	 */
	if (dprintf_find_string("on"))
		dprintf_print_all = 1;

	if (dprintf_string != NULL)
		zfs_flags |= ZFS_DEBUG_DPRINTF;
}

/*
 * =========================================================================
 * debug printfs
 * =========================================================================
 */
void
__dprintf(boolean_t dprint, const char *file, const char *func,
    int line, const char *fmt, ...)
{
	/* Get rid of annoying "../common/" prefix to filename. */
	const char *newfile = zfs_basename(file);

	va_list adx;
	if (dprint) {
		/* dprintf messages are printed immediately */

		if (!dprintf_print_all &&
		    !dprintf_find_string(newfile) &&
		    !dprintf_find_string(func))
			return;

		/* Print out just the function name if requested */
		flockfile(stdout);
		if (dprintf_find_string("pid"))
			(void) printf("%d ", getpid());
		if (dprintf_find_string("tid"))
			(void) printf("%ju ",
			    (uintmax_t)(uintptr_t)pthread_self());
		if (dprintf_find_string("cpu"))
			(void) printf("%u ", getcpuid());
		if (dprintf_find_string("time"))
			(void) printf("%llu ", gethrtime());
		if (dprintf_find_string("long"))
			(void) printf("%s, line %d: ", newfile, line);
		(void) printf("dprintf: %s: ", func);
		va_start(adx, fmt);
		(void) vprintf(fmt, adx);
		va_end(adx);
		funlockfile(stdout);
	} else {
		/* zfs_dbgmsg is logged for dumping later */
		size_t size;
		char *buf;
		int i;

		size = 1024;
		buf = umem_alloc(size, UMEM_NOFAIL);
		i = snprintf(buf, size, "%s:%d:%s(): ", newfile, line, func);

		if (i < size) {
			va_start(adx, fmt);
			(void) vsnprintf(buf + i, size - i, fmt, adx);
			va_end(adx);
		}

		__zfs_dbgmsg(buf);

		umem_free(buf, size);
	}
}

/*
 * =========================================================================
 * cmn_err() and panic()
 * =========================================================================
 */

static __attribute__((noreturn)) void
panic_stop_or_abort(void)
{
	const char *stopenv = getenv("LIBZPOOL_PANIC_STOP");
	if (stopenv != NULL && atoi(stopenv)) {
		fputs("libzpool: LIBZPOOL_PANIC_STOP is set, sending "
		    "SIGSTOP to process group\n", stderr);
		fflush(stderr);

		kill(0, SIGSTOP);

		fputs("libzpool: continued after panic stop, "
		    "aborting\n", stderr);
	}

	abort();	/* think of it as a "user-level crash dump" */
}

static void
vcmn_msg(int ce, const char *fmt, va_list adx)
{
	switch (ce) {
	case CE_IGNORE:
		return;
	case CE_CONT:
		break;
	case CE_NOTE:
		fputs("libzpool: NOTICE: ", stderr);
		break;
	case CE_WARN:
		fputs("libzpool: WARNING: ", stderr);
		break;
	case CE_PANIC:
		fputs("libzpool: PANIC: ", stderr);
		break;
	default:
		fputs("libzpool: [unknown severity %d]: ", stderr);
		break;
	}

	vfprintf(stderr, fmt, adx);
	if (ce != CE_CONT)
		fputc('\n', stderr);
	fflush(stderr);
}

void
vcmn_err(int ce, const char *fmt, va_list adx)
{
	vcmn_msg(ce, fmt, adx);

	if (ce == CE_PANIC)
		panic_stop_or_abort();
}

void
cmn_err(int ce, const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_err(ce, fmt, adx);
	va_end(adx);
}

__attribute__((noreturn)) void
panic(const char *fmt, ...)
{
	va_list adx;

	va_start(adx, fmt);
	vcmn_msg(CE_PANIC, fmt, adx);
	va_end(adx);

	panic_stop_or_abort();
}

__attribute__((noreturn)) void
vpanic(const char *fmt, va_list adx)
{
	vcmn_msg(CE_PANIC, fmt, adx);
	panic_stop_or_abort();
}

/*
 * =========================================================================
 * misc routines
 * =========================================================================
 */

void
delay(clock_t ticks)
{
	(void) poll(0, 0, ticks * (1000 / hz));
}

/*
 * Find highest one bit set.
 * Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 * The __builtin_clzll() function is supported by both GCC and Clang.
 */
int
highbit64(uint64_t i)
{
	if (i == 0)
	return (0);

	return (NBBY * sizeof (uint64_t) - __builtin_clzll(i));
}

/*
 * Find lowest one bit set.
 * Returns bit number + 1 of lowest bit that is set, otherwise returns 0.
 * The __builtin_ffsll() function is supported by both GCC and Clang.
 */
int
lowbit64(uint64_t i)
{
	if (i == 0)
		return (0);

	return (__builtin_ffsll(i));
}

int
ddi_strtoull(const char *str, char **nptr, int base, u_longlong_t *result)
{
	errno = 0;
	*result = strtoull(str, nptr, base);
	if (*result == 0)
		return (errno);
	return (0);
}

/*
 * =========================================================================
 * kernel emulation setup & teardown
 * =========================================================================
 */
static int
umem_out_of_memory(void)
{
	char errmsg[] = "out of memory -- generating core dump\n";

	(void) fprintf(stderr, "%s", errmsg);
	abort();
	return (0);
}

static void
spa_config_load(void)
{
	void *buf = NULL;
	nvlist_t *nvlist, *child;
	nvpair_t *nvpair;
	char *pathname;
	zfs_file_t *fp;
	zfs_file_attr_t zfa;
	uint64_t fsize;
	int err;

	/*
	 * Open the configuration file.
	 */
	pathname = kmem_alloc(MAXPATHLEN, KM_SLEEP);

	(void) snprintf(pathname, MAXPATHLEN, "%s", spa_config_path);

	err = zfs_file_open(pathname, O_RDONLY, 0, &fp);
	if (err)
		err = zfs_file_open(ZPOOL_CACHE_BOOT, O_RDONLY, 0, &fp);

	kmem_free(pathname, MAXPATHLEN);

	if (err)
		return;

	if (zfs_file_getattr(fp, &zfa))
		goto out;

	fsize = zfa.zfa_size;
	buf = kmem_alloc(fsize, KM_SLEEP);

	/*
	 * Read the nvlist from the file.
	 */
	if (zfs_file_read(fp, buf, fsize, NULL) < 0)
		goto out;

	/*
	 * Unpack the nvlist.
	 */
	if (nvlist_unpack(buf, fsize, &nvlist, KM_SLEEP) != 0)
		goto out;

	/*
	 * Iterate over all elements in the nvlist, creating a new spa_t for
	 * each one with the specified configuration.
	 */
	mutex_enter(&spa_namespace_lock);
	nvpair = NULL;
	while ((nvpair = nvlist_next_nvpair(nvlist, nvpair)) != NULL) {
		if (nvpair_type(nvpair) != DATA_TYPE_NVLIST)
			continue;

		child = fnvpair_value_nvlist(nvpair);

		if (spa_lookup(nvpair_name(nvpair)) != NULL)
			continue;
		(void) spa_add(nvpair_name(nvpair), child, NULL);
	}
	mutex_exit(&spa_namespace_lock);

	nvlist_free(nvlist);

out:
	if (buf != NULL)
		kmem_free(buf, fsize);

	zfs_file_close(fp);
}

void
kernel_init(int mode)
{
	extern uint_t rrw_tsd_key;

	libspl_init();

	umem_nofail_callback(umem_out_of_memory);

	dprintf("physmem = %llu pages (%.2f GB)\n", (u_longlong_t)physmem,
	    (double)physmem * sysconf(_SC_PAGE_SIZE) / (1ULL << 30));

	hostid = (mode & SPA_MODE_WRITE) ? get_system_hostid() : 0;

	system_taskq_init();
	icp_init();

	zstd_init();

	spa_init((spa_mode_t)mode);
	spa_config_load();

	fletcher_4_init();

	tsd_create(&rrw_tsd_key, rrw_tsd_destroy);
}

void
kernel_fini(void)
{
	fletcher_4_fini();
	spa_fini();

	zstd_fini();

	icp_fini();
	system_taskq_fini();

	libspl_fini();
}

zfs_file_t *
zfs_onexit_fd_hold(int fd, minor_t *minorp)
{
	(void) fd;
	*minorp = 0;
	return (NULL);
}

void
zfs_onexit_fd_rele(zfs_file_t *fp)
{
	(void) fp;
}

int
zfs_onexit_add_cb(minor_t minor, void (*func)(void *), void *data,
    uintptr_t *action_handle)
{
	(void) minor, (void) func, (void) data, (void) action_handle;
	return (0);
}

void
zvol_create_minors(const char *name)
{
	(void) name;
}

void
zvol_remove_minors(spa_t *spa, const char *name, boolean_t async)
{
	(void) spa, (void) name, (void) async;
}

void
zvol_rename_minors(spa_t *spa, const char *oldname, const char *newname,
    boolean_t async)
{
	(void) spa, (void) oldname, (void) newname, (void) async;
}

/*
 * Open file
 *
 * path - fully qualified path to file
 * flags - file attributes O_READ / O_WRITE / O_EXCL
 * fpp - pointer to return file pointer
 *
 * Returns 0 on success underlying error on failure.
 */
int
zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fpp)
{
	int fd;
	int dump_fd;
	int err;
	int old_umask = 0;
	zfs_file_t *fp;
	struct stat64 st;

	if (!(flags & O_CREAT) && stat64(path, &st) == -1)
		return (errno);

	if (!(flags & O_CREAT) && S_ISBLK(st.st_mode))
		flags |= O_DIRECT;

	if (flags & O_CREAT)
		old_umask = umask(0);

	fd = open64(path, flags, mode);
	if (fd == -1)
		return (errno);

	if (flags & O_CREAT)
		(void) umask(old_umask);

	if (vn_dumpdir != NULL) {
		char *dumppath = umem_zalloc(MAXPATHLEN, UMEM_NOFAIL);
		const char *inpath = zfs_basename(path);

		(void) snprintf(dumppath, MAXPATHLEN,
		    "%s/%s", vn_dumpdir, inpath);
		dump_fd = open64(dumppath, O_CREAT | O_WRONLY, 0666);
		umem_free(dumppath, MAXPATHLEN);
		if (dump_fd == -1) {
			err = errno;
			close(fd);
			return (err);
		}
	} else {
		dump_fd = -1;
	}

	(void) fcntl(fd, F_SETFD, FD_CLOEXEC);

	fp = umem_zalloc(sizeof (zfs_file_t), UMEM_NOFAIL);
	fp->f_fd = fd;
	fp->f_dump_fd = dump_fd;
	*fpp = fp;

	return (0);
}

void
zfs_file_close(zfs_file_t *fp)
{
	close(fp->f_fd);
	if (fp->f_dump_fd != -1)
		close(fp->f_dump_fd);

	umem_free(fp, sizeof (zfs_file_t));
}

/*
 * Stateful write - use os internal file pointer to determine where to
 * write and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * resid -  pointer to count of unwritten bytes  (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_write(zfs_file_t *fp, const void *buf, size_t count, ssize_t *resid)
{
	ssize_t rc;

	rc = write(fp->f_fd, buf, count);
	if (rc < 0)
		return (errno);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless write - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to write to
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to write to (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pwrite(zfs_file_t *fp, const void *buf,
    size_t count, loff_t pos, uint8_t ashift, ssize_t *resid)
{
	ssize_t rc, split, done;
	int sectors;

	/*
	 * To simulate partial disk writes, we split writes into two
	 * system calls so that the process can be killed in between.
	 * This is used by ztest to simulate realistic failure modes.
	 */
	sectors = count >> ashift;
	split = (sectors > 0 ? rand() % sectors : 0) << ashift;
	rc = pwrite64(fp->f_fd, buf, split, pos);
	if (rc != -1) {
		done = rc;
		rc = pwrite64(fp->f_fd, (char *)buf + split,
		    count - split, pos + split);
	}
#ifdef __linux__
	if (rc == -1 && errno == EINVAL) {
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order
		 * to catch the offender.
		 */
		abort();
	}
#endif

	if (rc < 0)
		return (errno);

	done += rc;

	if (resid) {
		*resid = count - done;
	} else if (done != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateful read - use os internal file pointer to determine where to
 * read and update on successful completion.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to read
 * resid -  pointer to count of unread bytes (if short read)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_read(zfs_file_t *fp, void *buf, size_t count, ssize_t *resid)
{
	int rc;

	rc = read(fp->f_fd, buf, count);
	if (rc < 0)
		return (errno);

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * Stateless read - os internal file pointer is not updated.
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * buf - buffer to write
 * count - # of bytes to write
 * off - file offset to read from (only valid for seekable types)
 * resid -  pointer to count of unwritten bytes (if short write)
 *
 * Returns 0 on success errno on failure.
 */
int
zfs_file_pread(zfs_file_t *fp, void *buf, size_t count, loff_t off,
    ssize_t *resid)
{
	ssize_t rc;

	rc = pread64(fp->f_fd, buf, count, off);
	if (rc < 0) {
#ifdef __linux__
		/*
		 * Under Linux, this most likely means an alignment issue
		 * (memory or disk) due to O_DIRECT, so we abort() in order to
		 * catch the offender.
		 */
		if (errno == EINVAL)
			abort();
#endif
		return (errno);
	}

	if (fp->f_dump_fd != -1) {
		int status;

		status = pwrite64(fp->f_dump_fd, buf, rc, off);
		ASSERT(status != -1);
	}

	if (resid) {
		*resid = count - rc;
	} else if (rc != count) {
		return (EIO);
	}

	return (0);
}

/*
 * lseek - set / get file pointer
 *
 * fp -  pointer to file (pipe, socket, etc) to read from
 * offp - value to seek to, returns current value plus passed offset
 * whence - see man pages for standard lseek whence values
 *
 * Returns 0 on success errno on failure (ESPIPE for non seekable types)
 */
int
zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence)
{
	loff_t rc;

	rc = lseek(fp->f_fd, *offp, whence);
	if (rc < 0)
		return (errno);

	*offp = rc;

	return (0);
}

/*
 * Get file attributes
 *
 * filp - file pointer
 * zfattr - pointer to file attr structure
 *
 * Currently only used for fetching size and file mode
 *
 * Returns 0 on success or error code of underlying getattr call on failure.
 */
int
zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr)
{
	struct stat64 st;

	if (fstat64_blk(fp->f_fd, &st) == -1)
		return (errno);

	zfattr->zfa_size = st.st_size;
	zfattr->zfa_mode = st.st_mode;

	return (0);
}

/*
 * Sync file to disk
 *
 * filp - file pointer
 * flags - O_SYNC and or O_DSYNC
 *
 * Returns 0 on success or error code of underlying sync call on failure.
 */
int
zfs_file_fsync(zfs_file_t *fp, int flags)
{
	(void) flags;

	if (fsync(fp->f_fd) < 0)
		return (errno);

	return (0);
}

/*
 * deallocate - zero and/or deallocate file storage
 *
 * fp - file pointer
 * offset - offset to start zeroing or deallocating
 * len - length to zero or deallocate
 */
int
zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len)
{
	int rc;
#if defined(__linux__)
	rc = fallocate(fp->f_fd,
	    FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, offset, len);
#elif defined(__FreeBSD__) && (__FreeBSD_version >= 1400029)
	struct spacectl_range rqsr = {
		.r_offset = offset,
		.r_len = len,
	};
	rc = fspacectl(fp->f_fd, SPACECTL_DEALLOC, &rqsr, 0, &rqsr);
#else
	(void) fp, (void) offset, (void) len;
	rc = EOPNOTSUPP;
#endif
	if (rc)
		return (SET_ERROR(rc));
	return (0);
}

/*
 * Request current file pointer offset
 *
 * fp - pointer to file
 *
 * Returns current file offset.
 */
loff_t
zfs_file_off(zfs_file_t *fp)
{
	return (lseek(fp->f_fd, SEEK_CUR, 0));
}

/*
 * unlink file
 *
 * path - fully qualified file path
 *
 * Returns 0 on success.
 *
 * OPTIONAL
 */
int
zfs_file_unlink(const char *path)
{
	return (remove(path));
}

/*
 * Get reference to file pointer
 *
 * fd - input file descriptor
 *
 * Returns pointer to file struct or NULL.
 * Unsupported in user space.
 */
zfs_file_t *
zfs_file_get(int fd)
{
	(void) fd;
	abort();
	return (NULL);
}
/*
 * Drop reference to file pointer
 *
 * fp - pointer to file struct
 *
 * Unsupported in user space.
 */
void
zfs_file_put(zfs_file_t *fp)
{
	abort();
	(void) fp;
}

void
zfsvfs_update_fromname(const char *oldname, const char *newname)
{
	(void) oldname, (void) newname;
}

void
spa_import_os(spa_t *spa)
{
	(void) spa;
}

void
spa_export_os(spa_t *spa)
{
	(void) spa;
}

void
spa_activate_os(spa_t *spa)
{
	(void) spa;
}

void
spa_deactivate_os(spa_t *spa)
{
	(void) spa;
}
