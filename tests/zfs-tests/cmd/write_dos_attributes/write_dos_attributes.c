/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 */

/*
 * Copyright 2022 iXsystems, Inc.
 */

/*
 * FreeBSD allows to update and retreive additional file level attributes.
 * For Linux, two IOCTLs have been added to update and retrieve additional
 * level attributes.
 *
 * This application updates additional file level attributes on a given
 * file. FreeBSD keywords can be used to specify the flag.
 *
 * Usage: 'write_dos_attributes flag filepath'
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <err.h>
#include <sys/fs/zfs.h>
#include <string.h>
#include <ctype.h>

#define	SU_ARCH_SHORT				"arch"
#define	SU_ARCH_FULL				"archived"
#define	SU_NODUMP				"nodump"
#define	SU_APPEND_SHORT				"sappnd"
#define	SU_APPEND_FULL				"sappend"
#define	SU_IMMUTABLE				"schg"
#define	SU_IMMUTABLE_SHORT			"schange"
#define	SU_IMMUTABLE_FULL			"simmutable"
#define	SU_UNLINK_SHORT				"sunlnk"
#define	SU_UNLINK_FULL				"sunlink"
#define	U_APPEND_SHORT				"uappnd"
#define	U_APPEND_FULL				"uappend"
#define	U_ARCH_SHORT				"uarch"
#define	U_ARCH_FULL				"uarchive"
#define	U_IMMUTABLE				"uchg"
#define	U_IMMUTABLE_SHORT			"uchange"
#define	U_IMMUTABLE_FULL			"uimmutable"
#define	U_HIDDEN_SHORT				"hidden"
#define	U_HIDDEN_FULL				"uhidden"
#define	U_OFFLINE_SHORT				"offline"
#define	U_OFFLINE_FULL				"uoffline"
#define	U_RDONLY				"rdonly"
#define	U_RDONLY_SHORT				"urdonly"
#define	U_RDONLY_FULL				"readonly"
#define	U_SPARSE_SHORT				"sparse"
#define	U_SPARSE_FULL				"usparse"
#define	U_SYSTEM_SHORT				"system"
#define	U_SYSTEM_FULL				"usystem"
#define	U_REPARSE_SHORT				"reparse"
#define	U_REPARSE_FULL				"ureparse"
#define	U_UNLINK_SHORT				"uunlnk"
#define	U_UNLINK_FULL				"uunlink"
#define	UNSET_NODUMP				"dump"

#define	IS_NO(s)	(s[0] == 'n' && s[1] == 'o')

uint64_t str_to_attribute(char *str);

uint64_t
str_to_attribute(char *str)
{
	if ((strcmp(str, SU_ARCH_SHORT) == 0) ||
	    (strcmp(str, SU_ARCH_FULL) == 0) ||
	    (strcmp(str, U_ARCH_SHORT) == 0) ||
	    (strcmp(str, U_ARCH_FULL) == 0))
		return (ZFS_ARCHIVE);

	else if ((strcmp(str, SU_APPEND_SHORT) == 0) ||
	    (strcmp(str, SU_APPEND_FULL) == 0) ||
	    (strcmp(str, U_APPEND_SHORT) == 0) ||
	    (strcmp(str, U_APPEND_FULL) == 0))
		return (ZFS_APPENDONLY);

	else if ((strcmp(str, SU_IMMUTABLE) == 0) ||
	    (strcmp(str, SU_IMMUTABLE_SHORT) == 0) ||
	    (strcmp(str, SU_IMMUTABLE_FULL) == 0))
		return (ZFS_IMMUTABLE);

	else if ((strcmp(str, SU_UNLINK_SHORT) == 0) ||
	    (strcmp(str, SU_UNLINK_FULL) == 0) ||
	    (strcmp(str, U_UNLINK_SHORT) == 0) ||
	    (strcmp(str, SU_UNLINK_FULL) == 0))
		return (ZFS_NOUNLINK);

	else if ((strcmp(str, U_HIDDEN_SHORT) == 0) ||
	    (strcmp(str, U_HIDDEN_FULL) == 0))
		return (ZFS_HIDDEN);

	else if ((strcmp(str, U_OFFLINE_SHORT) == 0) ||
	    (strcmp(str, U_OFFLINE_FULL) == 0))
		return (ZFS_OFFLINE);

	else if ((strcmp(str, U_RDONLY) == 0) ||
	    (strcmp(str, U_RDONLY_SHORT) == 0) ||
	    (strcmp(str, U_RDONLY_FULL) == 0))
		return (ZFS_READONLY);

	else if ((strcmp(str, U_SPARSE_SHORT) == 0) ||
	    (strcmp(str, U_SPARSE_FULL) == 0))
		return (ZFS_SPARSE);

	else if ((strcmp(str, U_SYSTEM_SHORT) == 0) ||
	    (strcmp(str, U_SYSTEM_FULL) == 0))
		return (ZFS_SYSTEM);

	else if ((strcmp(str, U_REPARSE_SHORT) == 0) ||
	    (strcmp(str, U_REPARSE_FULL) == 0))
		return (ZFS_REPARSE);

	return (-1);
}

int
main(int argc, const char * const argv[])
{
	if (argc != 3)
		errx(EXIT_FAILURE, "Usage: %s flag filepath", argv[0]);

	uint8_t unset, unset_all;
	uint64_t attribute, dosflags;
	char *flag = strdup(argv[1]);
	unset = unset_all = 0;
	attribute = dosflags = 0;

	// convert the flag to lower case
	for (int i = 0; i < strlen(argv[1]); ++i)
		flag[i] = tolower((unsigned char) flag[i]);

	// check if flag starts with 'no'
	if (IS_NO(flag)) {
		if (strcmp(flag, SU_NODUMP) == 0) {
			attribute = ZFS_NODUMP;
		} else {
			attribute = str_to_attribute(flag + 2);
			unset = 1;
		}
	}
	// check if '0' was passed
	else if (strcmp(flag, "0") == 0) {
		unset_all = 1;
	}
	// check if the flag is 'dump'
	else if (strcmp(flag, UNSET_NODUMP) == 0) {
		attribute = ZFS_NODUMP;
		unset = 1;
	} else {
		attribute = str_to_attribute(flag);
	}

	if (attribute == -1)
		errx(EXIT_FAILURE, "Invalid Flag %s", argv[1]);

	int fd = open(argv[2], O_RDWR | O_APPEND);
	if (fd < 0)
		err(EXIT_FAILURE, "Failed to open %s", argv[2]);

	if (ioctl(fd, ZFS_IOC_GETDOSFLAGS, &dosflags) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_GETDOSFLAGS failed");

	if (unset == 0 && attribute != 0)
		attribute |= dosflags;
	else if (unset == 1 && attribute != 0)
		attribute = dosflags & (~attribute);
	else if (unset_all == 1)
		attribute = 0;

	// set the attribute/s
	if (ioctl(fd, ZFS_IOC_SETDOSFLAGS, &attribute) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_SETDOSFLAGS failed");

	// get the attributes to confirm
	dosflags = -1;
	if (ioctl(fd, ZFS_IOC_GETDOSFLAGS, &dosflags) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_GETDOSFLAGS failed");

	(void) close(fd);

	if (dosflags != attribute)
		errx(EXIT_FAILURE, "Could not set %s attribute", argv[1]);

	(void) printf("New Dos Flags: 0x%llx\n", (u_longlong_t)dosflags);

	return (EXIT_SUCCESS);
}
