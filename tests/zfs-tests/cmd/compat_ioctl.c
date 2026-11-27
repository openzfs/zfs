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
 * Issue file ioctls the way a 32-bit program does on a 64-bit Linux
 * kernel, so that they reach the filesystem's compat_ioctl handler.
 *
 * On x86_64 a 64-bit program can make a 32-bit system call with
 * "int $0x80": the kernel then takes the ia32 entry and runs the compat
 * ioctl path, as it would for a real 32-bit binary.  The argument is
 * placed below 4 GiB (MAP_32BIT) so that a 32-bit pointer can reach it.
 * This needs no 32-bit toolchain or libraries.
 *
 *   compat_ioctl getflags FILE
 *	FS_IOC32_GETFLAGS; prints the flags in hex.
 *   compat_ioctl setflags FILE FLAGS
 *	FS_IOC32_SETFLAGS with FLAGS (a number, in hex with 0x).
 *   compat_ioctl fsgetxattr FILE
 *	FS_IOC_FSGETXATTR; prints "xflags projid" (xflags in hex).
 *   compat_ioctl fssetxattr FILE PROJID
 *	FS_IOC_FSGETXATTR, then FS_IOC_FSSETXATTR with the project ID set
 *	to PROJID and everything else as read.
 *
 * Exit status: 0 on success; 1 when an ioctl fails (the error is
 * printed as "ioctl NAME: errno N (TEXT)"); 2 when 32-bit system calls
 * are not available (not x86_64, or ia32 emulation disabled); 3 on a
 * usage or setup error.
 */

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/fs.h>

#define	EXIT_IOCTL	1
#define	EXIT_UNSUPPORTED	2
#define	EXIT_USAGE	3

#if defined(__x86_64__)

/* The i386 system call number of ioctl(2). */
#define	NR32_IOCTL	54

/* Not every header defines the 32-bit flag ioctls. */
#ifndef FS_IOC32_GETFLAGS
#define	FS_IOC32_GETFLAGS	_IOR('f', 1, int)
#endif
#ifndef FS_IOC32_SETFLAGS
#define	FS_IOC32_SETFLAGS	_IOW('f', 2, int)
#endif

static long
ioctl32(int fd, uint32_t cmd, void *arg)
{
	long ret;

	__asm__ volatile("int $0x80"
	    : "=a" (ret)
	    : "a" ((long)NR32_IOCTL), "b" ((long)fd), "c" ((long)cmd),
	    "d" ((long)(uintptr_t)arg)
	    : "memory", "r8", "r9", "r10", "r11");
	return (ret);
}

static void *
low_alloc(size_t size)
{
	void *p = mmap(NULL, size, PROT_READ | PROT_WRITE,
	    MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);

	if (p == MAP_FAILED || (uintptr_t)p + size > UINT32_MAX) {
		perror("mmap(MAP_32BIT)");
		exit(EXIT_USAGE);
	}
	memset(p, 0, size);
	return (p);
}

static uint32_t
parse_u32(const char *str)
{
	char *end;
	unsigned long val;

	errno = 0;
	val = strtoul(str, &end, 0);
	if (*str == '\0' || *end != '\0' || errno != 0 || val > UINT32_MAX) {
		(void) fprintf(stderr, "bad number: %s\n", str);
		exit(EXIT_USAGE);
	}
	return ((uint32_t)val);
}

static int
check(const char *name, long ret)
{
	if (ret >= 0)
		return (0);
	if (ret == -ENOSYS) {
		(void) fprintf(stderr, "32-bit system calls are not "
		    "available\n");
		exit(EXIT_UNSUPPORTED);
	}
	(void) fprintf(stderr, "ioctl %s: errno %ld (%s)\n", name, -ret,
	    strerror((int)-ret));
	return (EXIT_IOCTL);
}

int
main(int argc, char **argv)
{
	if (argc < 3) {
		(void) fprintf(stderr, "usage: %s getflags|fsgetxattr FILE\n"
		    "       %s setflags FILE FLAGS\n"
		    "       %s fssetxattr FILE PROJID\n",
		    argv[0], argv[0], argv[0]);
		return (EXIT_USAGE);
	}

	const char *op = argv[1];
	int fd = open(argv[2], O_RDONLY);
	if (fd < 0) {
		perror(argv[2]);
		return (EXIT_USAGE);
	}

	if (strcmp(op, "getflags") == 0) {
		uint32_t *flags = low_alloc(sizeof (*flags));
		if (check("FS_IOC32_GETFLAGS",
		    ioctl32(fd, FS_IOC32_GETFLAGS, flags)) != 0)
			return (EXIT_IOCTL);
		(void) printf("%#x\n", *flags);
	} else if (strcmp(op, "setflags") == 0 && argc == 4) {
		uint32_t *flags = low_alloc(sizeof (*flags));
		*flags = parse_u32(argv[3]);
		if (check("FS_IOC32_SETFLAGS",
		    ioctl32(fd, FS_IOC32_SETFLAGS, flags)) != 0)
			return (EXIT_IOCTL);
	} else if (strcmp(op, "fsgetxattr") == 0) {
		struct fsxattr *fsx = low_alloc(sizeof (*fsx));
		if (check("FS_IOC_FSGETXATTR",
		    ioctl32(fd, FS_IOC_FSGETXATTR, fsx)) != 0)
			return (EXIT_IOCTL);
		(void) printf("%#x %u\n", fsx->fsx_xflags, fsx->fsx_projid);
	} else if (strcmp(op, "fssetxattr") == 0 && argc == 4) {
		struct fsxattr *fsx = low_alloc(sizeof (*fsx));
		uint32_t projid = parse_u32(argv[3]);
		if (check("FS_IOC_FSGETXATTR",
		    ioctl32(fd, FS_IOC_FSGETXATTR, fsx)) != 0)
			return (EXIT_IOCTL);
		fsx->fsx_projid = projid;
		if (check("FS_IOC_FSSETXATTR",
		    ioctl32(fd, FS_IOC_FSSETXATTR, fsx)) != 0)
			return (EXIT_IOCTL);
	} else {
		(void) fprintf(stderr, "unknown operation: %s\n", op);
		return (EXIT_USAGE);
	}

	(void) close(fd);
	return (0);
}

#else /* !__x86_64__ */

int
main(void)
{
	(void) fprintf(stderr, "32-bit system calls are only issued on "
	    "x86_64\n");
	return (EXIT_UNSUPPORTED);
}

#endif /* __x86_64__ */
