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
 * Portions Copyright 2020 iXsystems, Inc.
 */

/*
 * Test some invalid send operations with libzfs/libzfs_core.
 *
 * Specifying the to and from snaps in the wrong order should return EXDEV.
 * We are checking that the early return doesn't accidentally leave any
 * references held, so this test is designed to trigger a panic when asserts
 * are verified with the bug present.
 */

#include <libzfs.h>
#include <libzfs_core.h>

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sysexits.h>
#include <err.h>

static void
usage(const char *name)
{
	fprintf(stderr, "usage: %s snap0 snap1\n", name);
	exit(EX_USAGE);
}

int
main(int argc, char const * const argv[])
{
	sendflags_t flags = { 0 };
	libzfs_handle_t *zhdl;
	zfs_handle_t *zhp;
	const char *fromfull, *tofull, *fsname, *fromsnap, *tosnap, *p;
	uint64_t size;
	int fd, error;

	if (argc != 3)
		usage(argv[0]);

	fromfull = argv[1];
	tofull = argv[2];

	p = strchr(fromfull, '@');
	if (p == NULL)
		usage(argv[0]);
	fromsnap = p + 1;

	p = strchr(tofull, '@');
	if (p == NULL)
		usage(argv[0]);
	tosnap = p + 1;

	fsname = strndup(tofull, p - tofull);
	if (strncmp(fsname, fromfull, p - tofull) != 0)
		usage(argv[0]);

	fd = open("/dev/null", O_WRONLY);
	if (fd == -1)
		err(EX_OSERR, "open(\"/dev/null\", O_WRONLY)");

	zhdl = libzfs_init();
	if (zhdl == NULL)
		errx(EX_OSERR, "libzfs_init(): %s", libzfs_error_init(errno));

	zhp = zfs_open(zhdl, fsname, ZFS_TYPE_FILESYSTEM);
	if (zhp == NULL)
		err(EX_OSERR, "zfs_open(\"%s\")", fsname);

	/*
	 * Exercise EXDEV in dmu_send_obj.  The error gets translated to
	 * EZFS_CROSSTARGET in libzfs.
	 */
	error = zfs_send(zhp, tosnap, fromsnap, &flags, fd, NULL, NULL, NULL);
	if (error == 0 || libzfs_errno(zhdl) != EZFS_CROSSTARGET)
		errx(EX_OSERR, "zfs_send(\"%s\", \"%s\") should have failed "
		    "with EZFS_CROSSTARGET, not %d",
		    tofull, fromfull, libzfs_errno(zhdl));
	printf("zfs_send(\"%s\", \"%s\"): %s\n",
	    tofull, fromfull, libzfs_error_description(zhdl));

	zfs_close(zhp);

	/*
	 * Exercise EXDEV in dmu_send.
	 */
	error = lzc_send_resume_redacted(fromfull, tofull, fd, 0, 0, 0, NULL);
	if (error != EXDEV)
		errx(EX_OSERR, "lzc_send_resume_redacted(\"%s\", \"%s\")"
		    " should have failed with EXDEV, not %d",
		    fromfull, tofull, error);
	printf("lzc_send_resume_redacted(\"%s\", \"%s\"): %s\n",
	    fromfull, tofull, strerror(error));

	/*
	 * Exercise EXDEV in dmu_send_estimate_fast.
	 */
	error = lzc_send_space_resume_redacted(fromfull, tofull, 0, 0, 0, 0,
	    NULL, fd, &size);
	if (error != EXDEV)
		errx(EX_OSERR, "lzc_send_space_resume_redacted(\"%s\", \"%s\")"
		    " should have failed with EXDEV, not %d",
		    fromfull, tofull, error);
	printf("lzc_send_space_resume_redacted(\"%s\", \"%s\"): %s\n",
	    fromfull, tofull, strerror(error));

	close(fd);
	libzfs_fini(zhdl);
	free((void *)fsname);

	return (EXIT_SUCCESS);
}
