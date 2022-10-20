/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2008, 2009 Edward Tomasz Napierała <trasz@FreeBSD.org>
 * Copyright (c) 2022 Andrew Walker <awalker@ixsystems.com>
 * All rights reserved.
 */

#include <arpa/inet.h>
#include <assert.h>
#include <zfsacl.h>
#include <sunacl.h>
#include <string.h>


#define	ACE_GETACL	4
#define	ACE_SETACL	5
#define	ACE_GETACLCNT	6

static int
acl_from_aces(zfsacl_t aclp, const ace_t *aces, int nentries)
{
	int i;
	const ace_t *ace = NULL;
	boolean_t ok;

	if (nentries > ZFSACL_MAX_ENTRIES) {
		/*
		 * I believe it may happen only when moving a pool
		 * from SunOS to FreeBSD.
		 */
		printf("acl_from_aces: ZFS ACL too big to fit "
		    "into 'struct acl'; returning EINVAL.\n");
		return (EINVAL);
	}

	for (i = 0; i < nentries; i++) {
		zfsace_permset_t permset = 0;
		zfsace_flagset_t flagset = 0;
		zfsace_who_t whotype = 0;
		zfsace_id_t whoid = ZFSACL_UNDEFINED_ID;
		zfsace_entry_type_t entry_type = 0;
		zfsacl_entry_t entry = NULL;

		ok = zfsacl_get_aclentry(aclp, i,  &entry);
		if (!ok) {
			return (errno);
		}

		ace = &(aces[i]);

		permset = ace->a_access_mask;
		flagset = ace->a_flags;

		if (ace->a_flags & ACE_OWNER) {
			whotype = ZFSACL_USER_OBJ;
		} else if (ace->a_flags & ACE_GROUP) {
			whotype = ZFSACL_GROUP_OBJ;
			flagset |= ZFSACE_IDENTIFIER_GROUP;
		} else if (ace->a_flags & ACE_EVERYONE) {
			whotype = ZFSACL_EVERYONE;
		} else if (ace->a_flags & ACE_IDENTIFIER_GROUP) {
			whotype = ZFSACL_GROUP;
			flagset |= ZFSACE_IDENTIFIER_GROUP;
		} else {
			whotype = ZFSACL_USER;
		}

		if (whotype == ZFSACL_USER || whotype == ZFSACL_GROUP)
			whoid = ace->a_who;

		switch (ace->a_type) {
		case ACE_ACCESS_ALLOWED_ACE_TYPE:
			entry_type = ZFSACL_ENTRY_TYPE_ALLOW;
			break;
		case ACE_ACCESS_DENIED_ACE_TYPE:
			entry_type = ZFSACL_ENTRY_TYPE_DENY;
			break;
		case ACE_SYSTEM_AUDIT_ACE_TYPE:
			entry_type = ZFSACL_ENTRY_TYPE_AUDIT;
			break;
		case ACE_SYSTEM_ALARM_ACE_TYPE:
			entry_type = ZFSACL_ENTRY_TYPE_ALARM;
			break;
		default:
			abort();
		}

		ok = zfsace_set_permset(entry, permset);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_set_flagset(entry, flagset);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_set_who(entry, whotype, whoid);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_set_entry_type(entry, entry_type);
		if (!ok) {
			return (errno);
		}
	}

	return (0);
}

static int
aces_from_acl(ace_t *aces, int *nentries, zfsacl_t aclp)
{
	int i;
	uint_t acecnt;
	ace_t *ace;
	boolean_t ok;

	ok = zfsacl_get_acecnt(aclp, &acecnt);
	if (!ok) {
		return (errno);
	}

	memset(aces, 0, sizeof (*aces) * acecnt);
	*nentries = (int)acecnt;

	for (i = 0; i < (int)acecnt; i++) {
		zfsace_permset_t permset = 0;
		zfsace_flagset_t flagset = 0;
		zfsace_who_t whotype = 0;
		zfsace_id_t whoid = ZFSACL_UNDEFINED_ID;
		zfsace_entry_type_t entry_type = 0;
		zfsacl_entry_t entry = NULL;

		ok = zfsacl_get_aclentry(aclp, i, &entry);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_get_permset(entry, &permset);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_get_flagset(entry, &flagset);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_get_who(entry, &whotype, &whoid);
		if (!ok) {
			return (errno);
		}
		ok = zfsace_get_entry_type(entry, &entry_type);
		if (!ok) {
			return (errno);
		}

		ace = &(aces[i]);

		ace->a_who = whoid;
		ace->a_access_mask = permset;
		ace->a_flags = flagset;

		if (whotype == ZFSACL_USER_OBJ)
			ace->a_flags |= ACE_OWNER;
		else if (whotype == ZFSACL_GROUP_OBJ)
			ace->a_flags |= (ACE_GROUP | ACE_IDENTIFIER_GROUP);
		else if (whotype == ZFSACL_GROUP)
			ace->a_flags |= ACE_IDENTIFIER_GROUP;
		else if (whotype == ZFSACL_EVERYONE)
			ace->a_flags |= ACE_EVERYONE;

		switch (entry_type) {
		case ZFSACL_ENTRY_TYPE_ALLOW:
			ace->a_type = ACE_ACCESS_ALLOWED_ACE_TYPE;
			break;
		case ZFSACL_ENTRY_TYPE_DENY:
			ace->a_type = ACE_ACCESS_DENIED_ACE_TYPE;
			break;
		case ZFSACL_ENTRY_TYPE_ALARM:
			ace->a_type = ACE_SYSTEM_ALARM_ACE_TYPE;
			break;
		case ZFSACL_ENTRY_TYPE_AUDIT:
			ace->a_type = ACE_SYSTEM_AUDIT_ACE_TYPE;
			break;
		default:
			abort();
		}
	}

	return (0);
}

static int
xacl(const char *path, int fd, int cmd, int cnt, void *buf)
{
	int error, nentries = 0;
	zfsacl_t aclp = NULL;
	uint_t acecnt;
	boolean_t ok;

	switch (cmd) {
	case ACE_SETACL:
		if (buf == NULL || cnt <= 0) {
			errno = EINVAL;
			return (-1);
		}

		if (cnt >= ZFSACL_MAX_ENTRIES) {
			errno = ENOSPC;
			return (-1);
		}

		aclp = zfsacl_init(cnt, ZFSACL_BRAND_NFSV4);
		if (aclp == NULL) {
			return (-1);
		}

		error = acl_from_aces(aclp, buf, cnt);
		if (error) {
			zfsacl_free(&aclp);
			errno = EIO;
			return (-1);
		}

		/*
		 * Ugly hack to make sure we don't trip sanity check at
		 * lib/libc/posix1e/acl_branding.c:
		 *     _acl_type_not_valid_for_acl().
		 */
		if (path != NULL)
			ok = zfsacl_set_file(path, aclp);
		else
			ok = zfsacl_set_fd(fd, aclp);
		if (error) {
			if (errno == EOPNOTSUPP || errno == EINVAL)
				errno = ENOSYS;
			zfsacl_free(&aclp);
			return (-1);
		}

		zfsacl_free(&aclp);
		return (0);

	case ACE_GETACL:
		if (buf == NULL) {
			errno = EINVAL;
			return (-1);
		}

		if (path != NULL)
			aclp = zfsacl_get_file(path, ZFSACL_BRAND_NFSV4);
		else
			aclp = zfsacl_get_fd(fd, ZFSACL_BRAND_NFSV4);
		if (aclp == NULL) {
			if (errno == EOPNOTSUPP || errno == EINVAL)
				errno = ENOSYS;
			return (-1);
		}

		ok = zfsacl_get_acecnt(aclp, &acecnt);
		if (!ok || acecnt > (uint_t)cnt) {
			zfsacl_free(&aclp);
			errno = ENOSPC;
			return (-1);
		}

		error = aces_from_acl(buf, &nentries, aclp);
		zfsacl_free(&aclp);
		if (error) {
			errno = EIO;
			return (-1);
		}

		return (nentries);

	case ACE_GETACLCNT:
		if (path != NULL)
			aclp = zfsacl_get_file(path, ZFSACL_BRAND_NFSV4);
		else
			aclp = zfsacl_get_fd(fd, ZFSACL_BRAND_NFSV4);
		if (aclp == NULL) {
			if (errno == EOPNOTSUPP || errno == EINVAL)
				errno = ENOSYS;
			return (-1);
		}

		ok = zfsacl_get_acecnt(aclp, &acecnt);
		if (!ok) {
			return (-1);
		}
		nentries = acecnt;
		zfsacl_free(&aclp);
		return (nentries);

	default:
		errno = EINVAL;
		return (-1);
	}
}

int
acl(const char *path, int cmd, int cnt, void *buf)
{
	if (path == NULL) {
		errno = EINVAL;
		return (-1);
	}

	return (xacl(path, -1, cmd, cnt, buf));
}

int
facl(int fd, int cmd, int cnt, void *buf)
{
	return (xacl(NULL, fd, cmd, cnt, buf));
}
