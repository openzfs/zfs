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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2012 Martin Matuska <martin@matuska.org>
 */

/*
 * Copyright (c) 2020 by Datto Inc. All rights reserved.
 */

#include <errno.h>
#include <libnvpair.h>
#include <libzfs.h>
#include <stdio.h>
#include <sys/nvpair.h>

#include "zstream.h"

int
zstream_do_token(int argc, char *argv[])
{
	char *resume_token = NULL;
	libzfs_handle_t *hdl;

	if (argc < 2) {
		(void) fprintf(stderr, "Need to pass the resume token\n");
		zstream_usage();
	}

	resume_token = argv[1];

	if ((hdl = libzfs_init()) == NULL) {
		(void) fprintf(stderr, "%s\n", libzfs_error_init(errno));
		return (1);
	}

	nvlist_t *resume_nvl =
	    zfs_send_resume_token_to_nvlist(hdl, resume_token);

	if (resume_nvl == NULL) {
		(void) fprintf(stderr,
		    "Unable to parse resume token: %s\n",
		    libzfs_error_description(hdl));
		libzfs_fini(hdl);
		return (1);
	}

	dump_nvlist(resume_nvl, 5);
	nvlist_free(resume_nvl);

	libzfs_fini(hdl);
	return (0);
}
