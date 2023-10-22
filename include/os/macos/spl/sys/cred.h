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
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_CRED_H
#define	_SPL_CRED_H

#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/kauth.h>

typedef struct ucred cred_t;

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
