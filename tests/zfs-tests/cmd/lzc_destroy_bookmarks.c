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
#include <libzfs_core.h>
#include <stdlib.h>
#include <string.h>
#include <sysexits.h>
#include <sys/nvpair.h>

int
main(int argc, char **argv)
{
	int error;
	int first_bookmark = 1;
	nvlist_t *bookmarks;
	boolean_t use_v2 = B_FALSE;

	if (argc > 1 && strcmp(argv[1], "-2") == 0) {
		use_v2 = B_TRUE;
		first_bookmark++;
	}
	if (argc <= first_bookmark)
		errx(EX_USAGE, "usage: %s [-2] bookmark ...", argv[0]);

	error = libzfs_core_init();
	if (error != 0)
		errx(EX_OSERR, "libzfs_core_init: %s", strerror(error));

	bookmarks = fnvlist_alloc();
	for (int i = first_bookmark; i < argc; i++)
		fnvlist_add_boolean(bookmarks, argv[i]);

	error = use_v2 ? lzc_destroy_bookmarks2(bookmarks, NULL) :
	    lzc_destroy_bookmarks(bookmarks, NULL);
	fnvlist_free(bookmarks);
	libzfs_core_fini();

	if (error != 0)
		errx(EX_OSERR, "lzc_destroy_bookmarks%s: %s", use_v2 ? "2" : "",
		    strerror(error));

	return (EXIT_SUCCESS);
}
