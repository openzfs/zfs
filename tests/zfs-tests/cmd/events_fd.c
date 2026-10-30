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

#include <err.h>
#include <fcntl.h>
#include <inttypes.h>
#include <libzfs.h>
#include <sys/fm/protocol.h>
#include <sys/zfs_ioctl.h>
#include <unistd.h>

static uint64_t
next_eid(libzfs_handle_t *hdl, int fd)
{
	nvlist_t *event;
	uint64_t eid;
	int dropped;

	if (zpool_events_next(hdl, &event, &dropped, ZEVENT_NONBLOCK, fd) != 0)
		errx(1, "read descriptor %d: %s", fd,
		    libzfs_error_description(hdl));
	if (event == NULL)
		errx(1, "no event on descriptor %d", fd);
	if (nvlist_lookup_uint64(event, FM_EREPORT_EID, &eid) != 0)
		errx(1, "event on descriptor %d has no EID", fd);
	nvlist_free(event);
	return (eid);
}

static void
check_eid(libzfs_handle_t *hdl, int fd, uint64_t expected)
{
	uint64_t actual = next_eid(hdl, fd);

	if (actual != expected)
		errx(1, "descriptor %d: EID %" PRIu64 ", expected %" PRIu64,
		    fd, actual, expected);
}

int
main(void)
{
	libzfs_handle_t *hdl = libzfs_init();
	uint64_t first;
	uint64_t second;
	int fd;
	int other;
	int duplicate;

	if (hdl == NULL)
		err(1, "libzfs_init");
	fd = open(ZFS_DEV, O_RDWR);
	if (fd < 0)
		err(1, "open %s", ZFS_DEV);
	other = open(ZFS_DEV, O_RDWR);
	if (other < 0)
		err(1, "open %s", ZFS_DEV);
	duplicate = dup(fd);
	if (duplicate < 0)
		err(1, "dup");

	if (zpool_events_seek(hdl, ZEVENT_SEEK_START, fd) != 0)
		errx(1, "seek start: %s", libzfs_error_description(hdl));
	first = next_eid(hdl, fd);
	second = next_eid(hdl, duplicate);
	if (first == second)
		errx(1, "duplicate descriptor did not advance "
		    "the shared cursor");
	check_eid(hdl, other, first);

	if (zpool_events_seek(hdl, first, fd) != 0)
		errx(1, "seek EID: %s", libzfs_error_description(hdl));
	(void) close(fd);
	check_eid(hdl, duplicate, second);
	check_eid(hdl, other, second);

	(void) close(duplicate);
	if (zpool_events_seek(hdl, first, other) != 0)
		errx(1, "seek independent cursor: %s",
		    libzfs_error_description(hdl));
	check_eid(hdl, other, second);

	(void) close(other);
	libzfs_fini(hdl);
	return (0);
}
