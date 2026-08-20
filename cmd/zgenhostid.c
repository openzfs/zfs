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
 * Copyright (c) 2020, Georgy Yakovlev.  All rights reserved.
 */

#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/*
 * Fill buf with len bytes from the system CSPRNG.  Returns 0 on success
 * and -1 on failure, with errno set.
 */
static int
get_random_bytes(void *buf, size_t len)
{
	int fd = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return (-1);

	size_t got = 0;
	while (got < len) {
		ssize_t n = read(fd, (char *)buf + got, len - got);
		if (n <= 0) {
			if (n < 0 && errno == EINTR)
				continue;
			if (n == 0)
				errno = EIO;
			int saved = errno;
			(void) close(fd);
			errno = saved;
			return (-1);
		}
		got += (size_t)n;
	}

	(void) close(fd);
	return (0);
}

static __attribute__((noreturn)) void
usage(void)
{
	(void) fprintf(stderr,
	    "usage: zgenhostid [-fh] [-o path] [value]\n\n"
	    "  -f\t\t force hostid file write\n"
	    "  -h\t\t print this usage and exit\n"
	    "  -o <filename>\t write hostid to this file\n\n"
	    "If hostid file is not present, store a hostid in it.\n"
	    "The optional value should be an 8-digit hex number between"
	    " 1 and 2^32-1.\n"
	    "If the value is 0 or no value is provided, a random one"
	    " will be generated.\n"
	    "The value must be unique among your systems.\n");
	exit(EXIT_FAILURE);
}

int
main(int argc, char **argv)
{
	/* default file path, can be optionally set by user */
	const char *path = "/etc/hostid";
	/* holds converted user input or generated value */
	unsigned long input_i = 0;

	int opt;
	int force_fwrite = 0;
	while ((opt = getopt_long(argc, argv, "fo:h?", 0, 0)) != -1) {
		switch (opt) {
		case 'f':
			force_fwrite = 1;
			break;
		case 'o':
			path = optarg;
			break;
		case 'h':
		case '?':
			usage();
		}
	}

	char *in_s = argv[optind];
	if (in_s != NULL) {
		/* increment pointer by 2 if string is 0x prefixed */
		if (strncasecmp("0x", in_s, 2) == 0) {
			in_s += 2;
		}

		/* need to be exactly 8 characters */
		const char *hex = "0123456789abcdefABCDEF";
		if (strlen(in_s) != 8 || strspn(in_s, hex) != 8) {
			fprintf(stderr, "%s\n", strerror(ERANGE));
			usage();
		}

		input_i = strtoul(in_s, NULL, 16);
		if (errno != 0) {
			perror("strtoul");
			exit(EXIT_FAILURE);
		}

		if (input_i > UINT32_MAX) {
			fprintf(stderr, "%s\n", strerror(ERANGE));
			usage();
		}
	}

	struct stat fstat;
	if (force_fwrite == 0 && stat(path, &fstat) == 0 &&
	    S_ISREG(fstat.st_mode)) {
		fprintf(stderr, "%s: %s\n", path, strerror(EEXIST));
		exit(EXIT_FAILURE);
	}

	/*
	 * Generate if not provided by user. The hostid identifies
	 * this machine to ZFS multihost protection, so it must be
	 * unique. Obtain a value from the system CSPRNG to ensure
	 * that even on systems that share a common image and boot
	 * deterministically different hostids will be generated.
	 * The loop handles the unlikely zero return case.
	 */
	while (input_i == 0) {
		uint32_t rnd;

		if (get_random_bytes(&rnd, sizeof (rnd)) != 0) {
			(void) fprintf(stderr,
			    "zgenhostid: failed to read /dev/urandom: %s\n",
			    strerror(errno));
			exit(EXIT_FAILURE);
		}

		input_i = rnd;
	}

	FILE *fp = fopen(path, "wb");
	if (!fp) {
		perror("fopen");
		exit(EXIT_FAILURE);
	}

	/*
	 * we need just 4 bytes in native endianness
	 * not using sethostid() because it may be missing or just a stub
	 */
	uint32_t hostid = input_i;
	int written = fwrite(&hostid, 1, 4, fp);
	if (written != 4) {
		perror("fwrite");
		exit(EXIT_FAILURE);
	}

	fclose(fp);
	exit(EXIT_SUCCESS);
}
