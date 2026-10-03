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
 * Copyright (C) 2008 MacZFS
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#include <sys/cred.h>
#include <sys/kmem.h>
#include <sys/kauth.h>

/* Return the effective user id */
uid_t
crgetuid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (kauth_cred_getuid((kauth_cred_t)cr));
}

/* Return the real user id */
uid_t
crgetruid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (kauth_cred_getruid((kauth_cred_t)cr));
}

/* Return the saved user id */
uid_t
crgetsuid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (kauth_cred_getsvuid((kauth_cred_t)cr));
}

/* Return the filesystem user id */
uid_t
crgetfsuid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (-1);
}

/* Return the effective group id */
gid_t
crgetgid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (kauth_cred_getgid((kauth_cred_t)cr));
}

/* Return the real group id */
gid_t
crgetrgid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (kauth_cred_getrgid((kauth_cred_t)cr));
}

/* Return the saved group id */
gid_t
crgetsgid(const cred_t *cr)
{
	if (!cr)
		return (0);
	return (kauth_cred_getsvgid((kauth_cred_t)cr));
}

/* Return the filesystem group id */
gid_t
crgetfsgid(const cred_t *cr)
{
	return (-1);
}


extern int kauth_cred_getgroups(kauth_cred_t _cred, gid_t *_groups,
    int *_groupcount);
/*
 * Unfortunately, to get the count of groups, we have to call XNU which
 * memcpy's them over. No real clean way to get around that, but at least
 * these calls are done sparingly.
 * dsl_deleg.c: dsl_check_user_access() loops the gid the user is in
 * to call dsl_check_access(gid) to see if "zfs allow" matches.
 * If we can iterate the gids saved in mos, and test with
 * kauth_cred_ismember_gid() the equivalent can be achieved.
 * However, "zfs allow" does not yet work of macOS.
 */
int
crgetngroups(const cred_t *cr)
{
	return (0);
}


/*
 * We always allocate NGROUPs here, since we don't know how many there will
 * be until after the call. Unlike IllumOS, the ptr returned is allocated
 * and must be returned by a call to crgetgroupsfree().
 */
gid_t *
crgetgroups(const cred_t *cr)
{
	return (NULL);
}

void
crgetgroupsfree(gid_t *gids)
{
	if (!gids)
		return;
	kmem_free(gids, sizeof (gid_t) * NGROUPS);
}

/*
 * Return true if "cr" belongs in group "gid".
 */
int
spl_cred_ismember_gid(cred_t *cr, gid_t gid)
{
	int ret = 0; // Is not member.
	kauth_cred_ismember_gid((kauth_cred_t)cr, gid, &ret);
	if (ret == 1)
		return (TRUE);
	return (FALSE);
}

void
crhold(struct ucred *cr)
{
	if (cr != NULL)
		(void) kauth_cred_ref((kauth_cred_t)cr);
}

void
crfree(struct ucred *cr)
{
	if (cr != NULL) {
		kauth_cred_t kcr = (kauth_cred_t)cr;
		kauth_cred_unref(&kcr);
	}
}
