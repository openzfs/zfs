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

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/quota.h>
#include <sys/syscall.h>

#ifndef SYS_quotactl_fd
#ifdef __NR_quotactl_fd
#define	SYS_quotactl_fd __NR_quotactl_fd
#elif defined(__x86_64__) || defined(__i386__) || defined(__arm__) || \
    defined(__aarch64__) || defined(__powerpc__) || defined(__s390__) || \
    defined(__riscv)
#define	SYS_quotactl_fd 443
#else
#error "SYS_quotactl_fd not known for this architecture."
#endif
#endif

#ifndef PRJQUOTA
#define	PRJQUOTA 2
#endif

static void
usage(const char *prog)
{
	(void) fprintf(stderr,
	    "Usage: %s get  <path> <usr|grp|prj> <id>\n"
	    "       %s set  <path> <usr|grp|prj> <id> <bytes> [inodelim]\n"
	    "       %s sync <path>\n",
	    prog, prog, prog);
	exit(1);
}

static int
open_path(const char *path)
{
	int fd;

#ifdef O_PATH
	fd = open(path, O_PATH | O_NOFOLLOW | O_CLOEXEC);
	if (fd >= 0)
		return (fd);
#endif
	fd = open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
	if (fd < 0 && (errno == ENOTDIR || errno == EISDIR))
		fd = open(path, O_RDONLY | O_CLOEXEC);
	return (fd);
}

static int
parse_qtype(const char *str)
{
	if (strcmp(str, "usr") == 0 || strcmp(str, "user") == 0)
		return (USRQUOTA);
	if (strcmp(str, "grp") == 0 || strcmp(str, "group") == 0)
		return (GRPQUOTA);
	if (strcmp(str, "prj") == 0 || strcmp(str, "project") == 0)
		return (PRJQUOTA);
	return (-1);
}

int
main(int argc, char **argv)
{
	int fd;
	int ret;
	int qtype;
	unsigned int id;
	struct dqblk dq;

	if (argc < 3)
		usage(argv[0]);

	fd = open_path(argv[2]);
	if (fd < 0) {
		(void) fprintf(stderr, "Failed to open %s: %s\n",
		    argv[2], strerror(errno));
		return (1);
	}

	if (strcmp(argv[1], "sync") == 0) {
		ret = syscall(SYS_quotactl_fd, (unsigned int)fd,
		    QCMD(Q_SYNC, 0), 0, NULL);
		(void) close(fd);
		if (ret != 0) {
			(void) fprintf(stderr, "quotactl_fd sync failed: %s\n",
			    strerror(errno));
			return (1);
		}
		return (0);
	}

	if (strcmp(argv[1], "get") == 0) {
		if (argc < 5)
			usage(argv[0]);
		qtype = parse_qtype(argv[3]);
		if (qtype < 0)
			usage(argv[0]);
		id = (unsigned int)strtoul(argv[4], NULL, 0);

		memset(&dq, 0, sizeof (dq));
		ret = syscall(SYS_quotactl_fd, (unsigned int)fd,
		    QCMD(Q_GETQUOTA, qtype), id, &dq);
		(void) close(fd);
		if (ret != 0) {
			(void) fprintf(stderr, "quotactl_fd get failed: %s\n",
			    strerror(errno));
			return (1);
		}

		(void) printf("bhardlimit=%llu\n",
		    (unsigned long long)dq.dqb_bhardlimit);
		(void) printf("bsoftlimit=%llu\n",
		    (unsigned long long)dq.dqb_bsoftlimit);
		(void) printf("curspace=%llu\n",
		    (unsigned long long)dq.dqb_curspace);
		(void) printf("ihardlimit=%llu\n",
		    (unsigned long long)dq.dqb_ihardlimit);
		(void) printf("isoftlimit=%llu\n",
		    (unsigned long long)dq.dqb_isoftlimit);
		(void) printf("curinodes=%llu\n",
		    (unsigned long long)dq.dqb_curinodes);
		return (0);
	}

	if (strcmp(argv[1], "set") == 0) {
		unsigned long long space_bytes;
		unsigned long long inodes = 0;

		if (argc < 6)
			usage(argv[0]);
		qtype = parse_qtype(argv[3]);
		if (qtype < 0)
			usage(argv[0]);
		id = (unsigned int)strtoul(argv[4], NULL, 0);
		space_bytes = strtoull(argv[5], NULL, 0);
		if (argc >= 7)
			inodes = strtoull(argv[6], NULL, 0);

		memset(&dq, 0, sizeof (dq));
		dq.dqb_bhardlimit = space_bytes / 1024;
		dq.dqb_bsoftlimit = space_bytes / 1024;
		dq.dqb_valid = QIF_BLIMITS;
		if (argc >= 7) {
			dq.dqb_ihardlimit = inodes;
			dq.dqb_isoftlimit = inodes;
			dq.dqb_valid |= QIF_ILIMITS;
		}

		ret = syscall(SYS_quotactl_fd, (unsigned int)fd,
		    QCMD(Q_SETQUOTA, qtype), id, &dq);
		(void) close(fd);
		if (ret != 0) {
			(void) fprintf(stderr, "quotactl_fd set failed: %s\n",
			    strerror(errno));
			return (1);
		}
		return (0);
	}

	(void) close(fd);
	usage(argv[0]);
	return (1);
}
