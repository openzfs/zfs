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
 * Copyright (c) 2017 Jorgen Lundman <lundman@lundman.net>
 */

#ifndef _SPL_GRP_H
#define	_SPL_GRP_H

struct  group { /* see getgrent(3C) */
	char    *gr_name;
	char    *gr_passwd;
	gid_t   gr_gid;
	char    **gr_mem;
};

extern struct group *getgrnam(const char *);    /* MT-unsafe */

int getgrnam_r(const char *name, struct group *grp,
    char *buf, size_t buflen, struct group **result);

#endif
