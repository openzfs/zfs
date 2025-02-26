// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright 2014 Garrett D'Amore <garrett@damore.org>
 *
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2017 RackTop Systems.
 */

#ifndef _SYS_ACL_H
#define	_SYS_ACL_H

#include <sys/types.h>
#include <sys/acl_impl.h>

/*
 * When compiling OpenSolaris kernel code, this file is included instead of the
 * FreeBSD one.  Include the original sys/acl.h as well.
 */
#undef _SYS_ACL_H
#include_next <sys/acl.h>
#define	_SYS_ACL_H

#ifdef	__cplusplus
extern "C" {
#endif

#define	MAX_ACL_ENTRIES		(1024)	/* max entries of each type */
typedef struct {
	int		a_type;		/* the type of ACL entry */
	uid_t		a_id;		/* the entry in -uid or gid */
	o_mode_t	a_perm;		/* the permission field */
} aclent_t;

typedef struct ace {
	uid_t		a_who;		/* uid or gid */
	uint32_t	a_access_mask;	/* read,write,... */
	uint16_t	a_flags;	/* see below */
	uint16_t	a_type;		/* allow or deny */
} ace_t;

/*
 * The following are Defined types for an aclent_t.
 */
#define	USER_OBJ	(0x01)		/* object owner */
#define	USER		(0x02)		/* additional users */
#define	GROUP_OBJ	(0x04)		/* owning group of the object */
#define	GROUP		(0x08)		/* additional groups */
#define	CLASS_OBJ	(0x10)		/* file group class and mask entry */
#define	OTHER_OBJ	(0x20)		/* other entry for the object */
#define	ACL_DEFAULT	(0x1000)	/* default flag */
/* default object owner */
#define	DEF_USER_OBJ	(ACL_DEFAULT | USER_OBJ)
/* default additional users */
#define	DEF_USER	(ACL_DEFAULT | USER)
/* default owning group */
#define	DEF_GROUP_OBJ	(ACL_DEFAULT | GROUP_OBJ)
/* default additional groups */
#define	DEF_GROUP	(ACL_DEFAULT | GROUP)
/* default mask entry */
#define	DEF_CLASS_OBJ	(ACL_DEFAULT | CLASS_OBJ)
/* default other entry */
#define	DEF_OTHER_OBJ	(ACL_DEFAULT | OTHER_OBJ)

/*
 * The following are defined for ace_t.
 */
#define	ACE_READ_DATA		0x00000001
#define	ACE_LIST_DIRECTORY	0x00000001
#define	ACE_WRITE_DATA		0x00000002
#define	ACE_ADD_FILE		0x00000002
#define	ACE_APPEND_DATA		0x00000004
#define	ACE_ADD_SUBDIRECTORY	0x00000004
#define	ACE_READ_NAMED_ATTRS	0x00000008
#define	ACE_WRITE_NAMED_ATTRS	0x00000010
#define	ACE_EXECUTE		0x00000020
#define	ACE_DELETE_CHILD	0x00000040
#define	ACE_READ_ATTRIBUTES	0x00000080
#define	ACE_WRITE_ATTRIBUTES	0x00000100
#define	ACE_DELETE		0x00010000
#define	ACE_READ_ACL		0x00020000
#define	ACE_WRITE_ACL		0x00040000
#define	ACE_WRITE_OWNER		0x00080000
#define	ACE_SYNCHRONIZE		0x00100000

#define	ACE_FILE_INHERIT_ACE		0x0001
#define	ACE_DIRECTORY_INHERIT_ACE	0x0002
#define	ACE_NO_PROPAGATE_INHERIT_ACE	0x0004
#define	ACE_INHERIT_ONLY_ACE		0x0008
#define	ACE_SUCCESSFUL_ACCESS_ACE_FLAG	0x0010
#define	ACE_FAILED_ACCESS_ACE_FLAG	0x0020
#define	ACE_IDENTIFIER_GROUP		0x0040
#define	ACE_INHERITED_ACE		0x0080
#define	ACE_OWNER			0x1000
#define	ACE_GROUP			0x2000
#define	ACE_EVERYONE			0x4000

#define	ACE_ACCESS_ALLOWED_ACE_TYPE	0x0000
#define	ACE_ACCESS_DENIED_ACE_TYPE	0x0001
#define	ACE_SYSTEM_AUDIT_ACE_TYPE	0x0002
#define	ACE_SYSTEM_ALARM_ACE_TYPE	0x0003

#define	ACL_AUTO_INHERIT		0x0001
#define	ACL_PROTECTED			0x0002
#define	ACL_DEFAULTED			0x0004
#define	ACL_FLAGS_ALL			(ACL_AUTO_INHERIT|ACL_PROTECTED| \
    ACL_DEFAULTED)

/*
 * These are only applicable in a CIFS context.
 */
#define	ACE_ACCESS_ALLOWED_COMPOUND_ACE_TYPE		0x04
#define	ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE		0x05
#define	ACE_ACCESS_DENIED_OBJECT_ACE_TYPE		0x06
#define	ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE		0x07
#define	ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE		0x08
#define	ACE_ACCESS_ALLOWED_CALLBACK_ACE_TYPE		0x09
#define	ACE_ACCESS_DENIED_CALLBACK_ACE_TYPE		0x0A
#define	ACE_ACCESS_ALLOWED_CALLBACK_OBJECT_ACE_TYPE	0x0B
#define	ACE_ACCESS_DENIED_CALLBACK_OBJECT_ACE_TYPE	0x0C
#define	ACE_SYSTEM_AUDIT_CALLBACK_ACE_TYPE		0x0D
#define	ACE_SYSTEM_ALARM_CALLBACK_ACE_TYPE		0x0E
#define	ACE_SYSTEM_AUDIT_CALLBACK_OBJECT_ACE_TYPE	0x0F
#define	ACE_SYSTEM_ALARM_CALLBACK_OBJECT_ACE_TYPE	0x10

#define	ACE_ALL_TYPES	0x001F

typedef struct ace_object {
	uid_t		a_who;		/* uid or gid */
	uint32_t	a_access_mask;	/* read,write,... */
	uint16_t	a_flags;	/* see below */
	uint16_t	a_type;		/* allow or deny */
	uint8_t		a_obj_type[16];	/* obj type */
	uint8_t		a_inherit_obj_type[16];  /* inherit obj */
} ace_object_t;

#define	ACE_ALL_PERMS	(ACE_READ_DATA|ACE_LIST_DIRECTORY|ACE_WRITE_DATA| \
    ACE_ADD_FILE|ACE_APPEND_DATA|ACE_ADD_SUBDIRECTORY|ACE_READ_NAMED_ATTRS| \
    ACE_WRITE_NAMED_ATTRS|ACE_EXECUTE|ACE_DELETE_CHILD|ACE_READ_ATTRIBUTES| \
    ACE_WRITE_ATTRIBUTES|ACE_DELETE|ACE_READ_ACL|ACE_WRITE_ACL| \
    ACE_WRITE_OWNER|ACE_SYNCHRONIZE)

#define	ACE_ALL_WRITE_PERMS (ACE_WRITE_DATA|ACE_APPEND_DATA| \
    ACE_WRITE_ATTRIBUTES|ACE_WRITE_NAMED_ATTRS|ACE_WRITE_ACL| \
    ACE_WRITE_OWNER|ACE_DELETE|ACE_DELETE_CHILD)

#define	ACE_READ_PERMS	(ACE_READ_DATA|ACE_READ_ACL|ACE_READ_ATTRIBUTES| \
    ACE_READ_NAMED_ATTRS)

#define	ACE_WRITE_PERMS	(ACE_WRITE_DATA|ACE_APPEND_DATA|ACE_WRITE_ATTRIBUTES| \
    ACE_WRITE_NAMED_ATTRS)

#define	ACE_MODIFY_PERMS (ACE_READ_DATA|ACE_LIST_DIRECTORY|ACE_WRITE_DATA| \
    ACE_ADD_FILE|ACE_APPEND_DATA|ACE_ADD_SUBDIRECTORY|ACE_READ_NAMED_ATTRS| \
    ACE_WRITE_NAMED_ATTRS|ACE_EXECUTE|ACE_DELETE_CHILD|ACE_READ_ATTRIBUTES| \
    ACE_WRITE_ATTRIBUTES|ACE_DELETE|ACE_READ_ACL|ACE_SYNCHRONIZE)
/*
 * The following flags are supported by both NFSv4 ACLs and ace_t.
 */
#define	ACE_NFSV4_SUP_FLAGS (ACE_FILE_INHERIT_ACE | \
    ACE_DIRECTORY_INHERIT_ACE | \
    ACE_NO_PROPAGATE_INHERIT_ACE | \
    ACE_INHERIT_ONLY_ACE | \
    ACE_INHERITED_ACE | \
    ACE_IDENTIFIER_GROUP)

#define	ACE_TYPE_FLAGS		(ACE_OWNER|ACE_GROUP|ACE_EVERYONE| \
    ACE_IDENTIFIER_GROUP)
#define	ACE_INHERIT_FLAGS	(ACE_FILE_INHERIT_ACE| ACL_INHERITED_ACE| \
    ACE_DIRECTORY_INHERIT_ACE|ACE_NO_PROPAGATE_INHERIT_ACE|ACE_INHERIT_ONLY_ACE)

/* cmd args to acl(2) for aclent_t  */
#define	GETACL			1
#define	SETACL			2
#define	GETACLCNT		3

/* cmd's to manipulate ace acls. */
#define	ACE_GETACL		4
#define	ACE_SETACL		5
#define	ACE_GETACLCNT		6

/* minimal acl entries from GETACLCNT */
#define	MIN_ACL_ENTRIES		4

extern void aces_from_acl(ace_t *aces, int *nentries, const struct acl *aclp);
extern int acl_from_aces(struct acl *aclp, const ace_t *aces, int nentries);
extern void ksort(caddr_t, int, int, int (*)(void *, void *));
extern int cmp2acls(void *, void *);

extern int acl(const char *path, int cmd, int cnt, void *buf);
extern int facl(int fd, int cmd, int cnt, void *buf);

#ifdef	__cplusplus
}
#endif

#endif /* _SYS_ACL_H */
