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
 *  Solaris Porting LAyer Tests (SPLAT) Credential Tests.
\*****************************************************************************/

#include <sys/cred.h>
#include "splat-internal.h"

#define SPLAT_CRED_NAME			"cred"
#define SPLAT_CRED_DESC			"Kernel Cred Tests"

#define SPLAT_CRED_TEST1_ID		0x0e01
#define SPLAT_CRED_TEST1_NAME		"cred"
#define SPLAT_CRED_TEST1_DESC		"Task Credential Test"

#define SPLAT_CRED_TEST2_ID		0x0e02
#define SPLAT_CRED_TEST2_NAME		"kcred"
#define SPLAT_CRED_TEST2_DESC		"Kernel Credential Test"

#define SPLAT_CRED_TEST3_ID		0x0e03
#define SPLAT_CRED_TEST3_NAME		"groupmember"
#define SPLAT_CRED_TEST3_DESC		"Group Member Test"

#define GROUP_STR_SIZE			128
#define GROUP_STR_REDZONE		16

static int
splat_cred_test1(struct file *file, void *arg)
{
	char str[GROUP_STR_SIZE];
	uid_t uid, ruid, suid;
	gid_t gid, rgid, sgid, *groups;
	int ngroups, i, count = 0;

	uid  = crgetuid(CRED());
	ruid = crgetruid(CRED());
	suid = crgetsuid(CRED());

	gid  = crgetgid(CRED());
	rgid = crgetrgid(CRED());
	sgid = crgetsgid(CRED());

	crhold(CRED());
	ngroups = crgetngroups(CRED());
	groups  = crgetgroups(CRED());

	memset(str, 0, GROUP_STR_SIZE);
	for (i = 0; i < ngroups; i++) {
		count += sprintf(str + count, "%d ", groups[i]);

		if (count > (GROUP_STR_SIZE - GROUP_STR_REDZONE)) {
			splat_vprint(file, SPLAT_CRED_TEST1_NAME,
				     "Failed too many group entries for temp "
				     "buffer: %d, %s\n", ngroups, str);
			return -ENOSPC;
		}
	}

	crfree(CRED());

	splat_vprint(file, SPLAT_CRED_TEST1_NAME,
		     "uid: %d ruid: %d suid: %d "
		     "gid: %d rgid: %d sgid: %d\n",
		     uid, ruid, suid, gid, rgid, sgid);
	splat_vprint(file, SPLAT_CRED_TEST1_NAME,
		     "ngroups: %d groups: %s\n", ngroups, str);

	if (uid || ruid || suid || gid || rgid || sgid) {
		splat_vprint(file, SPLAT_CRED_TEST1_NAME,
			     "Failed expected all uids+gids to be %d\n", 0);
		return -EIDRM;
	}

	if (ngroups > NGROUPS_MAX) {
		splat_vprint(file, SPLAT_CRED_TEST1_NAME,
			     "Failed ngroups must not exceed NGROUPS_MAX: "
			     "%d > %d\n", ngroups, NGROUPS_MAX);
		return -EIDRM;
	}

	splat_vprint(file, SPLAT_CRED_TEST1_NAME,
		     "Success sane CRED(): %d\n", 0);

        return 0;
} /* splat_cred_test1() */

static int
splat_cred_test2(struct file *file, void *arg)
{
	char str[GROUP_STR_SIZE];
	uid_t uid, ruid, suid;
	gid_t gid, rgid, sgid, *groups;
	int ngroups, i, count = 0;

	uid  = crgetuid(kcred);
	ruid = crgetruid(kcred);
	suid = crgetsuid(kcred);

	gid  = crgetgid(kcred);
	rgid = crgetrgid(kcred);
	sgid = crgetsgid(kcred);

	crhold(kcred);
	ngroups = crgetngroups(kcred);
	groups  = crgetgroups(kcred);

	memset(str, 0, GROUP_STR_SIZE);
	for (i = 0; i < ngroups; i++) {
		count += sprintf(str + count, "%d ", groups[i]);

		if (count > (GROUP_STR_SIZE - GROUP_STR_REDZONE)) {
			splat_vprint(file, SPLAT_CRED_TEST2_NAME,
				     "Failed too many group entries for temp "
				     "buffer: %d, %s\n", ngroups, str);
			return -ENOSPC;
		}
	}

	crfree(kcred);

	splat_vprint(file, SPLAT_CRED_TEST2_NAME,
		     "uid: %d ruid: %d suid: %d "
		     "gid: %d rgid: %d sgid: %d\n",
		     uid, ruid, suid, gid, rgid, sgid);
	splat_vprint(file, SPLAT_CRED_TEST2_NAME,
		     "ngroups: %d groups: %s\n", ngroups, str);

	if (uid || ruid || suid || gid || rgid || sgid) {
		splat_vprint(file, SPLAT_CRED_TEST2_NAME,
			     "Failed expected all uids+gids to be %d\n", 0);
		return -EIDRM;
	}

	if (ngroups > NGROUPS_MAX) {
		splat_vprint(file, SPLAT_CRED_TEST2_NAME,
			     "Failed ngroups must not exceed NGROUPS_MAX: "
			     "%d > %d\n", ngroups, NGROUPS_MAX);
		return -EIDRM;
	}

	splat_vprint(file, SPLAT_CRED_TEST2_NAME,
		     "Success sane kcred: %d\n", 0);

        return 0;
} /* splat_cred_test2() */

/*
 * On most/all systems it can be expected that a task with root
 * permissions also is a member of the root group,  Since the
 * test suite is always run as root we check first that CRED() is
 * a member of the root group, and secondly that it is not a member
 * of our fake group.  This test will break is someone happens to
 * create group number NGROUPS_MAX-1 and then added root to it.
 */
static int
splat_cred_test3(struct file *file, void *arg)
{
	gid_t root_gid, fake_gid;
	int rc;

	root_gid = 0;
	fake_gid = NGROUPS_MAX-1;

	rc = groupmember(root_gid, CRED());
	if (!rc) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME,
			     "Failed root git %d expected to be member "
			     "of CRED() groups: %d\n", root_gid, rc);
		return -EIDRM;
	}

	rc = groupmember(fake_gid, CRED());
	if (rc) {
		splat_vprint(file, SPLAT_CRED_TEST3_NAME,
			     "Failed fake git %d expected not to be member "
			     "of CRED() groups: %d\n", fake_gid, rc);
		return -EIDRM;
	}

	splat_vprint(file, SPLAT_CRED_TEST3_NAME, "Success root gid "
		     "is a member of the expected groups: %d\n", rc);

	return rc;
} /* splat_cred_test3() */

splat_subsystem_t *
splat_cred_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_CRED_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_CRED_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_CRED;

        SPLAT_TEST_INIT(sub, SPLAT_CRED_TEST1_NAME, SPLAT_CRED_TEST1_DESC,
	              SPLAT_CRED_TEST1_ID, splat_cred_test1);
        SPLAT_TEST_INIT(sub, SPLAT_CRED_TEST2_NAME, SPLAT_CRED_TEST2_DESC,
	              SPLAT_CRED_TEST2_ID, splat_cred_test2);
        SPLAT_TEST_INIT(sub, SPLAT_CRED_TEST3_NAME, SPLAT_CRED_TEST3_DESC,
	              SPLAT_CRED_TEST3_ID, splat_cred_test3);

        return sub;
} /* splat_cred_init() */

void
splat_cred_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);

        SPLAT_TEST_FINI(sub, SPLAT_CRED_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CRED_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_CRED_TEST1_ID);

        kfree(sub);
} /* splat_cred_fini() */

int
splat_cred_id(void)
{
        return SPLAT_SUBSYSTEM_CRED;
} /* splat_cred_id() */
