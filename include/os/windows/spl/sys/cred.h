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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_CRED_H
#define	_SPL_CRED_H

#include <sys/types.h>
#include <sys/vfs.h>

struct ucred; // fixme
typedef struct ucred cred_t;

#define	kcred	(cred_t *)NULL
#define	CRED()	(cred_t *)NULL
#define	KUID_TO_SUID(x)	(x)
#define	KGID_TO_SGID(x)	(x)

extern void crhold(cred_t *cr);
extern void crfree(cred_t *cr);
extern uid_t crgetuid(const cred_t *cr);
extern uid_t crgetruid(const cred_t *cr);
extern uid_t crgetsuid(const cred_t *cr);
extern uid_t crgetfsuid(const cred_t *cr);
extern gid_t crgetgid(const cred_t *cr);
extern gid_t crgetrgid(const cred_t *cr);
extern gid_t crgetsgid(const cred_t *cr);
extern gid_t crgetfsgid(const cred_t *cr);
extern int crgetngroups(const cred_t *cr);
extern gid_t *crgetgroups(const cred_t *cr);
extern void crgetgroupsfree(gid_t *gids);
extern int spl_cred_ismember_gid(cred_t *cr, gid_t gid);

#define	crgetsid(cred, i)	(NULL)

#endif  /* _SPL_CRED_H */
