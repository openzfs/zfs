/*
 * SPDX-License-Identifier: BSD-2-Clause-FreeBSD
 *
 * Copyright (c) 2021 iXsystems, Inc.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * Test for correct behavior of DOS mode READONLY flag on a file.
 * We should be able to open a file RW, set READONLY, and still write to the fd.
 */

#include <sys/stat.h>
#include <err.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <stdint.h>
#include <sys/fs/zfs.h>
#endif

int
main(int argc, const char *argv[])
{
	const char *buf = "We should be allowed to write this to the fd.\n";
	const char *path;
	int fd;

	if (argc != 2) {
		fprintf(stderr, "usage: %s PATH\n", argv[0]);
		return (EXIT_FAILURE);
	}
	path = argv[1];
	fd = open(path, O_CREAT|O_RDWR, 0777);
	if (fd == -1)
		err(EXIT_FAILURE, "%s: open failed", path);
#ifdef __linux__
	uint64_t dosflags = ZFS_READONLY;
	if (ioctl(fd, ZFS_IOC_SETDOSFLAGS, &dosflags) == -1)
		err(EXIT_FAILURE, "%s: ZFS_IOC_SETDOSFLAGS failed", path);
#else
	if (chflags(path, UF_READONLY) == -1)
		err(EXIT_FAILURE, "%s: chflags failed", path);
#endif
	if (write(fd, buf, strlen(buf)) == -1)
		err(EXIT_FAILURE, "%s: write failed", path);
	if (close(fd) == -1)
		err(EXIT_FAILURE, "%s: close failed", path);
	return (EXIT_SUCCESS);
}
