/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://zfsonlinux.org/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting Layer (SPL) Credential Implementation.
\*****************************************************************************/

#include <sys/cred.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_CRED

#ifdef HAVE_GROUPS_SEARCH
/* Symbol may be exported by custom kernel patch */
#define cr_groups_search(gi, grp)	groups_search(gi, grp)
#else
/* Implementation from 2.6.30 kernel */
static int
cr_groups_search(const struct group_info *group_info, gid_t grp)
{
	unsigned int left, right;

	if (!group_info)
		return 0;

	left = 0;
	right = group_info->ngroups;
	while (left < right) {
		unsigned int mid = (left+right)/2;
		int cmp = grp - GROUP_AT(group_info, mid);
		if (cmp > 0)
			left = mid + 1;
		else if (cmp < 0)
			right = mid;
		else
			return 1;
	}
	return 0;
}
#endif

#ifdef HAVE_CRED_STRUCT

/*
 * As of 2.6.29 a clean credential API appears in the linux kernel.
 * We attempt to layer the Solaris API on top of the linux API.
 */

/* Hold a reference on the credential and group info */
void
crhold(cred_t *cr)
{
	(void)get_cred((const cred_t *)cr);
	(void)get_group_info(cr->group_info);
}

/* Free a reference on the credential and group info */
void
crfree(cred_t *cr)
{
	put_group_info(cr->group_info);
	put_cred((const cred_t *)cr);
}

/* Return the number of supplemental groups */
int
crgetngroups(const cred_t *cr)
{
	struct group_info *gi;
	int rc;

	gi = get_group_info(cr->group_info);
	rc = gi->ngroups;
	put_group_info(gi);

	return rc;
}

/*
 * Return an array of supplemental gids.  The returned address is safe
 * to use as long as the caller has taken a reference with crhold().
 * The caller is responsible for releasing the reference with crfree().
 */
gid_t *
crgetgroups(const cred_t *cr)
{
	struct group_info *gi;
	gid_t *gids;

	gi = get_group_info(cr->group_info);
	gids = gi->blocks[0];
	put_group_info(gi);

	return gids;
}

/* Check if the passed gid is available is in supplied credential. */
int
groupmember(gid_t gid, const cred_t *cr)
{
	struct group_info *gi;
	int rc;

	gi = get_group_info(cr->group_info);
	rc = cr_groups_search(cr->group_info, gid);
	put_group_info(gi);

	return rc;
}

#else /* HAVE_CRED_STRUCT */

/*
 * Until very recently all credential information was embedded in
 * the linux task struct.  For this reason to simulate a Solaris
 * cred_t we need to pass the entire task structure around.
 */

/* Hold a reference on the credential and group info */
void crhold(cred_t *cr) { }

/* Free a reference on the credential and group info */
void crfree(cred_t *cr) { }

/* Return the number of supplemental groups */
int
crgetngroups(const cred_t *cr)
{
	int lock, rc;

	lock = (cr != current);
	if (lock)
		task_lock((struct task_struct *)cr);

	get_group_info(cr->group_info);
	rc = cr->group_info->ngroups;
	put_group_info(cr->group_info);

	if (lock)
		task_unlock((struct task_struct *)cr);

	return rc;
}

/*
 * Return an array of supplemental gids.  The returned address is safe
 * to use as long as the caller has taken a reference with crhold().
 * The caller is responsible for releasing the reference with crfree().
 */
gid_t *
crgetgroups(const cred_t *cr)
{
	gid_t *gids;
	int lock;

	lock = (cr != current);
	if (lock)
		task_lock((struct task_struct *)cr);

	get_group_info(cr->group_info);
	gids = cr->group_info->blocks[0];
	put_group_info(cr->group_info);

	if (lock)
		task_unlock((struct task_struct *)cr);

	return gids;
}

/* Check if the passed gid is available is in supplied credential. */
int
groupmember(gid_t gid, const cred_t *cr)
{
	int lock, rc;

	lock = (cr != current);
	if (lock)
		task_lock((struct task_struct *)cr);

	get_group_info(cr->group_info);
	rc = cr_groups_search(cr->group_info, gid);
	put_group_info(cr->group_info);

	if (lock)
		task_unlock((struct task_struct *)cr);

	return rc;
}

#endif /* HAVE_CRED_STRUCT */

/* Return the effective user id */
uid_t
crgetuid(const cred_t *cr)
{
	return cr->euid;
}

/* Return the real user id */
uid_t
crgetruid(const cred_t *cr)
{
	return cr->uid;
}

/* Return the saved user id */
uid_t
crgetsuid(const cred_t *cr)
{
	return cr->suid;
}

/* Return the filesystem user id */
uid_t
crgetfsuid(const cred_t *cr)
{
	return cr->fsuid;
}

/* Return the effective group id */
gid_t
crgetgid(const cred_t *cr)
{
	return cr->egid;
}

/* Return the real group id */
gid_t
crgetrgid(const cred_t *cr)
{
	return cr->gid;
}

/* Return the saved group id */
gid_t
crgetsgid(const cred_t *cr)
{
	return cr->sgid;
}

/* Return the filesystem group id */
gid_t
crgetfsgid(const cred_t *cr)
{
	return cr->fsgid;
}

EXPORT_SYMBOL(crhold);
EXPORT_SYMBOL(crfree);
EXPORT_SYMBOL(crgetuid);
EXPORT_SYMBOL(crgetruid);
EXPORT_SYMBOL(crgetsuid);
EXPORT_SYMBOL(crgetfsuid);
EXPORT_SYMBOL(crgetgid);
EXPORT_SYMBOL(crgetrgid);
EXPORT_SYMBOL(crgetsgid);
EXPORT_SYMBOL(crgetfsgid);
EXPORT_SYMBOL(crgetngroups);
EXPORT_SYMBOL(crgetgroups);
EXPORT_SYMBOL(groupmember);
