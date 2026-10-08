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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/xattr.h>

#ifndef O_TMPFILE
#define	O_TMPFILE	(020000000 | O_DIRECTORY)
#endif

/* sudo zpool <sub> <pool>, without a shell; returns the exit status or -1 */
static int
zpool_cmd(const char *sub, const char *pool)
{
	char sudo[] = "sudo", zpool[] = "zpool";
	char *const argv[] = { sudo, zpool, (char *)sub, (char *)pool, NULL };
	pid_t pid;
	int status;

	if ((pid = fork()) < 0)
		return (-1);
	if (pid == 0) {
		(void) execvp("sudo", argv);
		_exit(127);
	}
	if (waitpid(pid, &status, 0) < 0 || !WIFEXITED(status))
		return (-1);
	return (WEXITSTATUS(status));
}

int main(void) {
	const char *dir	= getenv("TESTDIR");
	const char *fname = getenv("TESTFILE");
	const char *pool = getenv("TESTPOOL");
	const char *uid_str = getenv("TARGET_UID");
	const char *gid_str = getenv("TARGET_GID");
	const char *mode_str = getenv("TARGET_MODE");

	if (!dir || !fname || !pool) {
		fprintf(stderr,
		    "Missing environment: TESTDIR, TESTFILE, or TESTPOOL\n");
		return (10);
	}

	uid_t uid = getuid();
	gid_t gid = getgid();
	mode_t mode = 0600;
	if (mode_str) {
		char *end = NULL;
		long m = strtol(mode_str, &end, 0);
		if (*end == '\0') {
			mode = (mode_t)m;
		} else {
			fprintf(stderr, "Invalid TARGET_MODE '%s'\n",
			    mode_str);
			return (11);
		}
	}
	if (uid_str) uid = (uid_t)atol(uid_str);
	if (gid_str) gid = (gid_t)atol(gid_str);

	size_t buf_size = 256 * 1024;
	char *buf = malloc(buf_size);
	if (!buf) {
		perror("malloc");
		return (12);
	}
	memset(buf, 'A', buf_size);

	/* xattrs */
	const char *xattr1 = "user.tmpxattr1";
	const char *xattr2 = "user.tmpxattr2";
	const char *xval1 = "XATTRVAL1";
	const char *xval2 = "XATTRVAL2";
	size_t xlen1 = strlen(xval1);
	size_t xlen2 = strlen(xval2);

	/* create an O_TMPFILE in the test directory */
	int fd = open(dir, O_TMPFILE | O_RDWR, 0666);
	if (fd < 0) {
		perror("open(O_TMPFILE)");
		free(buf);
		return (1);
	}

	/* write 256KB */
	ssize_t written = 0;
	while (written < buf_size) {
		ssize_t ret = write(fd, buf + written,
		    buf_size - written);
		if (ret < 0) {
			perror("write");
			close(fd);
			free(buf);
			return (2);
		}
		written += ret;
	}

	/* set xattrs */
	if (fsetxattr(fd, xattr1, xval1, xlen1, 0) < 0) {
		perror("fsetxattr user.tmpxattr1");
		close(fd);
		free(buf);
		return (3);
	}
	if (fsetxattr(fd, xattr2, xval2, xlen2, 0) < 0) {
		perror("fsetxattr user.tmpxattr2");
		close(fd);
		free(buf);
		return (4);
	}

	/* ownership metadata change */
	if (fchmod(fd, mode) < 0) {
		perror("fchmod");
		close(fd);
		free(buf);
		return (5);
	}
	if (fchown(fd, uid, gid) < 0) {
		if (!(errno == EPERM && geteuid() != 0 && uid == geteuid() &&
		gid == getegid())) {
			/*
			 * If EPERM occurred for any case other than non-root
			 * no-op, treat as error
			 */
			perror("fchown");
			close(fd);
			free(buf);
			return (6);
		}
		/*
		 * EPERM for non-root setting to same UID/GID:
		 * ignore (no change needed)
		 */
	}

	/* link the O_TMPFILE into the namespace */
	char spath[64], dpath[1024];
	snprintf(spath, sizeof (spath), "/proc/self/fd/%d", fd);
	snprintf(dpath, sizeof (dpath), "%s/%s", dir, fname);
	if (linkat(AT_FDCWD, spath, AT_FDCWD, dpath, AT_SYMLINK_FOLLOW) < 0) {
		perror("linkat");
		close(fd);
		free(buf);
		return (7);
	}

	// simulate crash: freeze and export the pool, then import it
	int sysret;
	if ((sysret = zpool_cmd("freeze", pool)) != 0) {
		fprintf(stderr, "ERROR: 'zpool freeze %s' failed (exit %d)\n",
		    pool, sysret);
		close(fd);
		(void) unlink(dpath);
		free(buf);
		return (8);
	}
	close(fd);  // close before export (umount)

	if ((sysret = zpool_cmd("export", pool)) != 0) {
		fprintf(stderr, "ERROR: 'zpool export %s' failed (exit %d)\n",
		    pool, sysret);
		(void) unlink(dpath);
		free(buf);
		return (9);
	}
	if ((sysret = zpool_cmd("import", pool)) != 0) {
		fprintf(stderr, "ERROR: 'zpool import %s' failed (exit %d)\n",
		    pool, sysret);
		free(buf);
		return (10);
	}

	/*
	 * After import, verify the file survived with correct data and
	 * attributes
	 */

	int fd_check = open(dpath, O_RDONLY);
	if (fd_check < 0) {
		perror("open(replayed_file)");
		(void) unlink(dpath);
		free(buf);
		return (11);
	}
	struct stat sb;
	if (fstat(fd_check, &sb) < 0) {
		perror("fstat(replayed_file)");
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (12);
	}
	if (sb.st_nlink != 1) {
		fprintf(stderr, "Link count mismatch: expected 1, got %lu\n",
		(unsigned long)sb.st_nlink);
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (13);
	}
	if ((sb.st_mode & 07777) != (mode & 07777)) {
		fprintf(stderr, "Mode mismatch: expected %04o, got %04o\n",
			mode & 07777, sb.st_mode & 07777);
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (14);
	}
	if (sb.st_uid != uid || sb.st_gid != gid) {
		fprintf(stderr,
		    "UID/GID mismatch: expected "
		    "UID=%d GID=%d, got UID=%d GID=%d\n",
		    (int)uid, (int)gid, sb.st_uid, sb.st_gid);
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (15);
	}
	/* read entire file content and verify */
	char *read_buf = malloc(buf_size);
	if (!read_buf) {
		perror("malloc");
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (16);
	}
	size_t total_read = 0;
	while (total_read < buf_size) {
		ssize_t r = read(fd_check, read_buf + total_read,
		buf_size - total_read);
		if (r < 0) {
			perror("read(file)");
			close(fd_check);
			(void) unlink(dpath);
			free(read_buf);
			free(buf);
			return (17);
		}
		if (r == 0) break;
		total_read += r;
	}
	if (total_read != buf_size) {
		fprintf(stderr,
		"File size mismatch: expected %zu bytes, read %zu bytes\n",
		buf_size, total_read);
		close(fd_check);
		(void) unlink(dpath);
		free(read_buf);
		free(buf);
		return (18);
	}
	if (memcmp(buf, read_buf, buf_size) != 0) {
		fprintf(stderr, "File content mismatch after replay\n");
		close(fd_check);
		(void) unlink(dpath);
		free(read_buf);
		free(buf);
		return (19);
	}
	free(read_buf);

	/* verify xattrs */
	char xbuf[256];
	ssize_t got = fgetxattr(fd_check, xattr1, xbuf, sizeof (xbuf));
	if (got < 0) {
		perror("fgetxattr(user.tmpxattr1)");
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (20);
	}
	if (got != (ssize_t)xlen1 || memcmp(xbuf, xval1, xlen1) != 0) {
		fprintf(stderr, "Extended attribute %s mismatch after replay\n",
		xattr1);
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (21);
	}
	got = fgetxattr(fd_check, xattr2, xbuf, sizeof (xbuf));
	if (got < 0) {
		perror("fgetxattr(user.tmpxattr2)");
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (22);
	}
	if (got != (ssize_t)xlen2 || memcmp(xbuf, xval2, xlen2) != 0) {
		fprintf(stderr, "Extended attribute %s mismatch after replay\n",
		xattr2);
		close(fd_check);
		(void) unlink(dpath);
		free(buf);
		return (23);
	}

	close(fd_check);
	if (unlink(dpath) != 0) {
		perror("unlink(replayed_file)");
	}
	free(buf);
	return (0);
}
