/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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
 * Copyright 2010 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 *
 * Portions Copyright 2012 Martin Matuska <martin@matuska.org>
 */

/*
 * Copyright (c) 2020 by Datto Inc. All rights reserved.
 */

#include <ctype.h>
#include <libnvpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stddef.h>

#include <libzfs.h>
#include <libzfs_core.h>

#include <sys/dmu.h>
#include <sys/zfs_ioctl.h>
#include "zstream.h"

int
zstream_do_token(int argc, char *argv[])
{
	char *resume_token = NULL;

	if (argc < 2) {
		(void) fprintf(stderr, "Need to pass the resume token\n");
		zstream_usage();
	}

	resume_token = argv[1];

	libzfs_handle_t *hdl = libzfs_init();

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
