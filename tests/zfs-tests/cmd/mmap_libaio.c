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
 * Copyright 2018 Canonical.  All rights reserved.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <libaio.h>
#include <err.h>

static io_context_t io_ctx;

static void
do_sync_io(struct iocb *iocb)
{
	struct io_event event;
	struct iocb *iocbs[] = { iocb };
	struct timespec ts = { 30, 0 };

	if (io_submit(io_ctx, 1, iocbs) != 1)
		err(1, "io_submit failed");

	if (io_getevents(io_ctx, 0, 1, &event, &ts) != 1)
		err(1, "io_getevents failed");
}

int
main(int argc, char **argv)
{
	(void) argc;
	char *buf;
	int page_size = getpagesize();
	int buf_size = strtol(argv[2], NULL, 0);
	int rwfd;
	struct iocb iocb;

	if (io_queue_init(1024, &io_ctx))
		err(1, "io_queue_init failed");

	rwfd = open(argv[1], O_RDWR | O_CREAT, S_IRUSR | S_IWUSR);
	if (rwfd < 0)
		err(1, "open failed");

	if (ftruncate(rwfd, buf_size) < 0)
		err(1, "ftruncate failed");

	buf = mmap(0, page_size, PROT_READ | PROT_WRITE, MAP_SHARED, rwfd, 0);
	if (buf == MAP_FAILED)
		err(1, "mmap failed");

	(void) io_prep_pwrite(&iocb, rwfd, buf, buf_size, 0);
	do_sync_io(&iocb);

	(void) io_prep_pread(&iocb, rwfd, buf, buf_size, 0);
	do_sync_io(&iocb);

	if (close(rwfd))
		err(1, "close failed");

	if (io_queue_release(io_ctx) != 0)
		err(1, "io_queue_release failed");

	return (0);
}
