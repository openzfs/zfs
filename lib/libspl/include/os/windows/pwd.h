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


#endif
