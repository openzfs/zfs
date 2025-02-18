// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */

/*
 * Copyright (c) 2020 by Delphix. All rights reserved.
 */

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <libzfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/nvpair.h>
#include <sys/fm/protocol.h>
#include <sys/fm/fs/zfs.h>

/*
 * Command to output io and checksum ereport values, one per line.
 * Used by zpool_events_duplicates.ksh to check for duplicate events.
 *
 * example output line:
 *
 * checksum "error_pool" 0x856dd01ce52e336 0x000034 0x000400 0x000a402c00
 *  0x000004	0x000000	0x000000	0x000000	0x000001
 */

/*
 * Our ereport duplicate criteria
 *
 * When the class and all of these values match, then an ereport is
 * considered to be a duplicate.
 */
static const char *const criteria_name[] = {
	FM_EREPORT_PAYLOAD_ZFS_POOL,
	FM_EREPORT_PAYLOAD_ZFS_VDEV_GUID,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_ERR,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_SIZE,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_PRIORITY,

	/* logical zio criteriai (optional) */
	FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJSET,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_OBJECT,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_BLKID,
	FM_EREPORT_PAYLOAD_ZFS_ZIO_LEVEL,
};

#define	CRITERIA_NAMES_COUNT	ARRAY_SIZE(criteria_name)

static void
print_ereport_line(nvlist_t *nvl)
{
	const char *class;
	int last = CRITERIA_NAMES_COUNT - 1;

	/*
	 * For the test case context, we only want to see 'io' and
	 * 'checksum' subclass.  We skip 'data' to minimize the output.
	 */
	if (nvlist_lookup_string(nvl, FM_CLASS, &class) != 0 ||
	    strstr(class, "ereport.fs.zfs.") == NULL ||
	    strcmp(class, "ereport.fs.zfs.data") == 0) {
		return;
	}

	(void) printf("%s\t", class + strlen("ereport.fs.zfs."));

	for (int i = 0; i < CRITERIA_NAMES_COUNT; i++) {
		nvpair_t *nvp;
		uint32_t i32 = 0;
		uint64_t i64 = 0;
		const char *str = NULL;

		if (nvlist_lookup_nvpair(nvl, criteria_name[i], &nvp) != 0) {
			/* print a proxy for optional criteria */
			(void) printf("--------");
			(void) printf("%c", i == last ? '\n' : '\t');
			continue;
		}

		switch (nvpair_type(nvp)) {
		case DATA_TYPE_STRING:
			(void) nvpair_value_string(nvp, &str);
			(void) printf("\"%s\"", str ? str : "<NULL>");
			break;

		case DATA_TYPE_INT32:
			(void) nvpair_value_int32(nvp, (void *)&i32);
			(void) printf("0x%06x", i32);
			break;

		case DATA_TYPE_UINT32:
			(void) nvpair_value_uint32(nvp, &i32);
			(void) printf("0x%06x", i32);
			break;

		case DATA_TYPE_INT64:
			(void) nvpair_value_int64(nvp, (void *)&i64);
			(void) printf("0x%06llx", (u_longlong_t)i64);
			break;

		case DATA_TYPE_UINT64:
			(void) nvpair_value_uint64(nvp, &i64);
			if (strcmp(FM_EREPORT_PAYLOAD_ZFS_ZIO_OFFSET,
			    criteria_name[i]) == 0)
				(void) printf("0x%010llx", (u_longlong_t)i64);
			else
				(void) printf("0x%06llx", (u_longlong_t)i64);
			break;
		default:
			(void) printf("<unknown>");
			break;
		}
		(void) printf("%c", i == last ? '\n' : '\t');
	}
}

static void
ereports_dump(libzfs_handle_t *zhdl, int zevent_fd)
{
	nvlist_t *nvl;
	int ret, dropped;

	while (1) {
		ret = zpool_events_next(zhdl, &nvl, &dropped, ZEVENT_NONBLOCK,
		    zevent_fd);
		if (ret || nvl == NULL)
			break;
		if (dropped > 0)
			(void) fprintf(stdout, "dropped %d events\n", dropped);
		print_ereport_line(nvl);
		(void) fflush(stdout);
		nvlist_free(nvl);
	}
}

int
main(void)
{
	libzfs_handle_t *hdl;
	int fd;

	hdl = libzfs_init();
	if (hdl == NULL) {
		(void) fprintf(stderr, "libzfs_init: %s\n", strerror(errno));
		exit(2);
	}
	fd = open(ZFS_DEV, O_RDWR);
	if (fd < 0) {
		(void) fprintf(stderr, "open: %s\n", strerror(errno));
		libzfs_fini(hdl);
		exit(2);
	}

	ereports_dump(hdl, fd);

	(void) close(fd);
	libzfs_fini(hdl);

	return (0);
}
