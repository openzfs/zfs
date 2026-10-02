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
 *
 * Copyright (C) 2017 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_CRED_H
#define	_SPL_CRED_H

#include <sys/types.h>
#include <sys/vfs.h>

typedef struct cred {
	uid_t	cr_uid;		/* effective user id */
	gid_t	cr_gid;		/* effective group id */
} cred_t;

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

/*
 * SID-to-POSIX uid/gid mapping and caller-identity helpers.
 * Implemented in spl-cred.c; callers that need the full SID definition
 * must include <Ntifs.h> before this header (or use the opaque pointer).
 */
extern uid_t spl_sid_to_uid(struct _SID *sid);
extern gid_t spl_sid_to_gid(struct _SID *sid);
extern uid_t spl_get_caller_uid(void);
extern gid_t spl_get_caller_gid(void);

#endif  /* _SPL_CRED_H */
