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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <pthread.h>
#include <errno.h>
#include <err.h>

/*
 * --------------------------------------------------------------------
 * Bug Issue Id: #7512
 * The bug time sequence:
 * 1. context #1, zfs_write assign a txg "n".
 * 2. In the same process, context #2, mmap page fault (which means the mm_sem
 *    is hold) occurred, zfs_dirty_inode open a txg failed, and wait previous
 *    txg "n" completed.
 * 3. context #1 call zfs_uiomove to write, however page fault is occurred in
 *    zfs_uiomove, which means it needs mm_sem, but mm_sem is hold by
 *    context #2, so it stuck and can't complete, then txg "n" will not
 *    complete.
 *
 * So context #1 and context #2 trap into the "dead lock".
 * --------------------------------------------------------------------
 */

#define	NORMAL_WRITE_TH_NUM	2
#define	MAX_WRITE_BYTES	262144000

static void *
normal_writer(void *filename)
{
	char *file_path = filename;
	int fd;
	ssize_t write_num = 0;
	int page_size = getpagesize();

	fd = open(file_path, O_RDWR | O_CREAT, 0777);
	if (fd == -1) {
		err(1, "failed to open %s", file_path);
	}

	char buf = 'z';
	off_t bytes_written = 0;

	while (bytes_written < MAX_WRITE_BYTES) {
		write_num = write(fd, &buf, 1);
		if (write_num == 0) {
			err(1, "write failed!");
			break;
		}
		if ((bytes_written = lseek(fd, page_size, SEEK_CUR)) == -1) {
			err(1, "lseek failed on %s: %s", file_path,
			    strerror(errno));
			break;
		}
	}

	if (close(fd) != 0)
		err(1, "failed to close file");

	return (NULL);
}

static void *
map_writer(void *filename)
{
	int fd;
	int ret = 0;
	char *buf = NULL;
	int page_size = getpagesize();
	char *file_path = filename;

	while (1) {
		fd = open(file_path, O_RDWR);
		if (fd == -1) {
			if (errno == ENOENT) {
				fd = open(file_path, O_RDWR | O_CREAT, 0777);
				if (fd == -1) {
					err(1, "open file failed");
				}
				ret = ftruncate(fd, page_size);
				if (ret == -1) {
					err(1, "truncate file failed");
				}
			} else {
				err(1, "open file failed");
			}
		}

		if ((buf = mmap(NULL, page_size, PROT_READ | PROT_WRITE,
		    MAP_SHARED, fd, 0)) == MAP_FAILED) {
			err(1, "map file failed");
		}

		if (fd != -1)
			close(fd);

		char s[10] = {0, };
		memcpy(buf, s, 10);
		ret = munmap(buf, page_size);
		if (ret != 0) {
			err(1, "unmap file failed");
		}
	}
}

int
main(int argc, char **argv)
{
	pthread_t map_write_tid;
	pthread_t normal_write_tid[NORMAL_WRITE_TH_NUM];
	int i = 0;

	if (argc != 3) {
		(void) printf("usage: %s <normal write file name> "
		    "<map write file name>\n", argv[0]);
		exit(1);
	}

	for (i = 0; i < NORMAL_WRITE_TH_NUM; i++) {
		if (pthread_create(&normal_write_tid[i], NULL, normal_writer,
		    argv[1])) {
			err(1, "pthread_create normal_writer failed.");
		}
	}

	if (pthread_create(&map_write_tid, NULL, map_writer, argv[2])) {
		err(1, "pthread_create map_writer failed.");
	}

	pthread_join(map_write_tid, NULL);
	return (0);
}
