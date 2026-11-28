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
 * Get or set a file's extent size through FS_IOC_FSGETXATTR and
 * FS_IOC_FSSETXATTR, which on ZFS fixes the block size of a file or sets
 * the block size new files in a directory inherit.
 *
 *   extsize PATH            print "FLAG SIZE BLKSIZE": FLAG is extsize,
 *                           extszinherit or none; BLKSIZE is st_blksize
 *   extsize -s SIZE PATH    set: FS_XFLAG_EXTSIZE on a file,
 *                           FS_XFLAG_EXTSZINHERIT on a directory
 *   extsize -c PATH         clear
 *   extsize -f FLAGS -s SIZE PATH
 *                           set exactly these extent size flags (hex)
 *   extsize -p PROJID ...   also set fsx_projid
 *   extsize -x CMD PATH     open PATH, run CMD, then get through that fd
 *
 * On failure, prints the errno name (EINVAL, EOPNOTSUPP, ...) on stdout
 * and exits 1.
 */

#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <linux/fs.h>
typedef struct fsxattr zfsxattr_t;
#else
typedef struct {
	uint32_t	fsx_xflags;
	uint32_t	fsx_extsize;
	uint32_t	fsx_nextents;
	uint32_t	fsx_projid;
	uint32_t	fsx_cowextsize;
	unsigned char	fsx_pad[8];
} zfsxattr_t;
#define	FS_IOC_FSGETXATTR	_IOR('X', 31, zfsxattr_t)
#define	FS_IOC_FSSETXATTR	_IOW('X', 32, zfsxattr_t)
#endif
#ifndef FS_XFLAG_EXTSIZE
#define	FS_XFLAG_EXTSIZE	0x00000800
#endif
#ifndef FS_XFLAG_EXTSZINHERIT
#define	FS_XFLAG_EXTSZINHERIT	0x00001000
#endif
#define	EXT_FLAGS	(FS_XFLAG_EXTSIZE | FS_XFLAG_EXTSZINHERIT)

static const char *
errname(int e)
{
	switch (e) {
	case EINVAL: return ("EINVAL");
	case EIO: return ("EIO");
	case EDQUOT: return ("EDQUOT");
	case EOPNOTSUPP: return ("EOPNOTSUPP");
	case EROFS: return ("EROFS");
	case EPERM: return ("EPERM");
	case EACCES: return ("EACCES");
	case ENOTTY: return ("ENOTTY");
	default: return (strerror(e));
	}
}

static void
usage(void)
{
	(void) fprintf(stderr, "usage: extsize [-p projid] "
	    "[-c | [-f flags] -s size] path\n"
	    "       extsize -x cmd path\n"
	    "       extsize -W count -p projid -s size dir\n");
	exit(2);
}

/*
 * FS_IOC_FSSETXATTR (project ID and extent size) on count new empty files
 * in dir, cycling through three cases: no write (it must succeed), a
 * child's one-byte write first (it must fail with EINVAL and change
 * nothing), and the write racing the call (either of the two).
 */
static int
race_write(const char *dir, long count, uint32_t projid, uint32_t size)
{
	char path[4096];
	zfsxattr_t fsx;
	long pinned = 0, refused = 0, bad = 0;
	char c = 'x';

	for (long i = 0; i < count; i++) {
		int fd, wfd, p[2], status, err = 0;
		int mode = i % 3;	/* 0 no write, 1 write first, 2 race */
		pid_t pid = -1;

		if (snprintf(path, sizeof (path), "%s/race.%ld", dir, i) >=
		    (int)sizeof (path) ||
		    (fd = open(path, O_RDWR | O_CREAT | O_EXCL, 0644)) < 0 ||
		    (wfd = open(path, O_WRONLY)) < 0 || pipe(p) != 0 ||
		    ioctl(fd, FS_IOC_FSGETXATTR, &fsx) != 0 ||
		    (mode != 0 && (pid = fork()) < 0)) {
			perror(path);
			return (2);
		}
		if (pid == 0)
			_exit(read(p[0], &c, 1) == 1 &&
			    write(wfd, &c, 1) == 1 ? 0 : 1);
		if (pid > 0 && write(p[1], &c, 1) != 1) {
			perror("pipe");
			return (2);
		}
		if (mode == 1 && (waitpid(pid, &status, 0) != pid ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
			(void) fprintf(stderr, "%s: write failed\n", path);
			return (2);
		}
		fsx.fsx_projid = projid;
		fsx.fsx_xflags |= FS_XFLAG_EXTSIZE;
		fsx.fsx_extsize = size;
		if (ioctl(fd, FS_IOC_FSSETXATTR, &fsx) != 0)
			err = errno;
		if (mode == 2 && (waitpid(pid, &status, 0) != pid ||
		    !WIFEXITED(status) || WEXITSTATUS(status) != 0)) {
			(void) fprintf(stderr, "%s: write failed\n", path);
			return (2);
		}
		if (ioctl(fd, FS_IOC_FSGETXATTR, &fsx) != 0) {
			perror(path);
			return (2);
		}
		if (err == 0 && mode != 1) {
			pinned++;
			if (fsx.fsx_projid == projid &&
			    (fsx.fsx_xflags & FS_XFLAG_EXTSIZE) &&
			    fsx.fsx_extsize == size)
				goto next;
		} else if (err == EINVAL && mode != 0) {
			refused++;
			if (fsx.fsx_projid != projid &&
			    !(fsx.fsx_xflags & FS_XFLAG_EXTSIZE))
				goto next;
		}
		(void) printf("%s: case %d: %s, project ID %u, extsize %u\n",
		    path, mode, err == 0 ? "set" : errname(err),
		    fsx.fsx_projid, fsx.fsx_extsize);
		bad++;
next:
		(void) close(p[0]);
		(void) close(p[1]);
		(void) close(wfd);
		(void) close(fd);
		(void) unlink(path);
	}
	(void) printf("%ld set, %ld refused, %ld wrong\n", pinned, refused,
	    bad);
	return (bad != 0);
}

int
main(int argc, char **argv)
{
	zfsxattr_t fsx;
	struct stat st;
	long long size = -1;
	long flags = -1;
	long long projid = -1;
	const char *cmd = NULL;
	long race = 0;
	int clear = 0;
	int c, fd;

	while ((c = getopt(argc, argv, "cf:p:s:W:x:")) != -1) {
		switch (c) {
		case 'c':
			clear = 1;
			break;
		case 'f':
			flags = strtol(optarg, NULL, 16);
			break;
		case 'p':
			projid = strtoll(optarg, NULL, 0);
			break;
		case 's':
			size = strtoll(optarg, NULL, 0);
			break;
		case 'W':
			race = strtol(optarg, NULL, 0);
			break;
		case 'x':
			cmd = optarg;
			break;
		default:
			usage();
		}
	}
	if (optind != argc - 1 || (clear && size >= 0) ||
	    (flags >= 0 && size < 0) ||
	    (cmd != NULL && (clear || size >= 0 || projid >= 0)) ||
	    (race > 0 && (clear || flags >= 0 || cmd != NULL || size < 0 ||
	    projid < 0)))
		usage();
	if (race > 0)
		return (race_write(argv[optind], race, (uint32_t)projid,
		    (uint32_t)size));

	if ((fd = open(argv[optind], O_RDONLY)) < 0) {
		perror(argv[optind]);
		return (1);
	}
	if (cmd != NULL) {
		if (system(cmd) != 0) {
			(void) fprintf(stderr, "%s failed\n", cmd);
			return (2);
		}
		/* No fstat(): it may fail first. */
		if (ioctl(fd, FS_IOC_FSGETXATTR, &fsx) != 0) {
			(void) printf("%s\n", errname(errno));
			return (1);
		}
		(void) printf("%s %u\n",
		    (fsx.fsx_xflags & FS_XFLAG_EXTSIZE) ? "extsize" :
		    (fsx.fsx_xflags & FS_XFLAG_EXTSZINHERIT) ? "extszinherit" :
		    "none", fsx.fsx_extsize);
		return (0);
	}
	if (fstat(fd, &st) != 0 || ioctl(fd, FS_IOC_FSGETXATTR, &fsx) != 0) {
		(void) printf("%s\n", errname(errno));
		return (1);
	}

	if (!clear && size < 0) {
		(void) printf("%s %u %ld\n",
		    (fsx.fsx_xflags & FS_XFLAG_EXTSIZE) ? "extsize" :
		    (fsx.fsx_xflags & FS_XFLAG_EXTSZINHERIT) ? "extszinherit" :
		    "none", fsx.fsx_extsize, (long)st.st_blksize);
		return (0);
	}

	fsx.fsx_xflags &= ~EXT_FLAGS;
	fsx.fsx_extsize = 0;
	if (projid >= 0)
		fsx.fsx_projid = (uint32_t)projid;
	if (flags >= 0) {
		fsx.fsx_xflags |= (uint32_t)flags;
		fsx.fsx_extsize = (uint32_t)size;
	} else if (!clear) {
		fsx.fsx_xflags |= S_ISDIR(st.st_mode) ? FS_XFLAG_EXTSZINHERIT :
		    FS_XFLAG_EXTSIZE;
		fsx.fsx_extsize = (uint32_t)size;
	}
	if (ioctl(fd, FS_IOC_FSSETXATTR, &fsx) != 0) {
		(void) printf("%s\n", errname(errno));
		return (1);
	}
	(void) close(fd);
	return (0);
}
