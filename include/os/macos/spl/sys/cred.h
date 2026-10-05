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
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_CRED_H
#define	_SPL_CRED_H

struct ucred;
typedef struct ucred *kauth_cred_t;
typedef struct ucred cred_t;

#include <sys/ucred.h>

#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/kauth.h>

#define	kcred	spl_kcred()
#define	CRED()	(cred_t *)kauth_cred_get()
#define	KUID_TO_SUID(x)		(x)
#define	KGID_TO_SGID(x)		(x)

#include <TargetConditionals.h>
#include <AvailabilityMacros.h>

// Older OSX API
#if !(MAC_OS_X_VERSION_MIN_REQUIRED >= 1070)
#define	kauth_cred_getruid(x) (x)->cr_ruid
#define	kauth_cred_getrgid(x) (x)->cr_rgid
#define	kauth_cred_getsvuid(x) (x)->cr_svuid
#define	kauth_cred_getsvgid(x) (x)->cr_svgid
#endif


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
extern cred_t *spl_kcred(void);

#define	crgetsid(cred, i)	(NULL)

#endif  /* _SPL_CRED_H */
