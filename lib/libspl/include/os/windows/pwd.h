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

#ifndef _SPL_PWD_H
#define	_SPL_PWD_H

struct passwd {
	char    *pw_name;
	char    *pw_passwd;
	uid_t   pw_uid;
	gid_t   pw_gid;
	char    *pw_age;
	char    *pw_comment;
	char    *pw_gecos;
	char    *pw_dir;
	char    *pw_shell;
};


extern struct passwd *getpwnam(const char *);   /* MT-unsafe */

int getpwnam_r(const char *name, struct passwd *pwd,
    char *buf, size_t buflen, struct passwd **result);

#endif
