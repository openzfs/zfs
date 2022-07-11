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

io_context_t io_ctx;

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
