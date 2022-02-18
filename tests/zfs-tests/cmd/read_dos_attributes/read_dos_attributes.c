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
 * This application reads additional file level attributes on a given
 * file and prints FreeBSD keywords that map to respective attributes.
 *
 * Usage: 'read_dos_attributes filepath'
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

#define	NO_ATTRIBUTE				"-"

#define	SEPARATOR				","

#define	BUFFER_SIZE				0x200

void attribute_to_str(uint64_t attributes, char *buff);

void
attribute_to_str(uint64_t attributes, char *buff)
{
	if (attributes & ZFS_ARCHIVE) {
		strcat(buff, U_ARCH_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_APPENDONLY) {
		strcat(buff, U_APPEND_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_IMMUTABLE) {
		strcat(buff, U_IMMUTABLE_FULL);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_NOUNLINK) {
		strcat(buff, U_UNLINK_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_NODUMP) {
		strcat(buff, SU_NODUMP);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_HIDDEN) {
		strcat(buff, U_HIDDEN_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_OFFLINE) {
		strcat(buff, U_OFFLINE_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_READONLY) {
		strcat(buff, U_RDONLY);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_SPARSE) {
		strcat(buff, U_SPARSE_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_SYSTEM) {
		strcat(buff, U_SYSTEM_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (attributes & ZFS_REPARSE) {
		strcat(buff, U_REPARSE_SHORT);
		strcat(buff, SEPARATOR);
	}

	if (buff[0] == '\0')
		strcat(buff, NO_ATTRIBUTE);
	else
		buff[strlen(buff) - 1] = '\0';
}

int
main(int argc, const char * const argv[])
{
	if (argc != 2)
		errx(EXIT_FAILURE, "Usage: %s filepath", argv[0]);

	int fd = open(argv[1], O_RDWR | O_APPEND);
	if (fd < 0)
		err(EXIT_FAILURE, "Failed to open %s", argv[1]);

	uint64_t dosflags = 0;
	if (ioctl(fd, ZFS_IOC_GETDOSFLAGS, &dosflags) == -1)
		err(EXIT_FAILURE, "ZFS_IOC_GETDOSFLAGS failed");

	(void) close(fd);

	char buffer[BUFFER_SIZE];
	memset(buffer, 0, BUFFER_SIZE);

	(void) attribute_to_str(dosflags, buffer);

	(void) printf("%s\n", buffer);

	return (EXIT_SUCCESS);
}
