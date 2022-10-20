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
 * Copyright (c) 2022 Andrew Walker <awalker@ixsystems.com>
 * All rights reserved.
 */

#ifndef	__ZFSACL_H__
#define	__ZFSACL_H__

#include <zfs_config.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>

/*
 * BRAND_ACCESS and BRAND_DEFAULT
 * values chosen so can convert easily
 * to FreeBSD brand POSIX with
 * zfsacl_brand_t & ACL_BRAND_POSIX
 */
typedef enum {
	ZFSACL_BRAND_UNKNOWN	= 0,
	ZFSACL_BRAND_NFSV4	= 2,
	ZFSACL_BRAND_ACCESS	= 3,
	ZFSACL_BRAND_DEFAULT	= 5,
} zfsacl_brand_t;

typedef enum {
	ZFSACL_UNDEFINED_TAG	= 0,
	ZFSACL_USER_OBJ		= 1, // owner@ in NFSv4
	ZFSACL_GROUP_OBJ	= 2, // group@ in NFSv4
	ZFSACL_EVERYONE		= 3, // everyone@ -- NFSv4 only
	ZFSACL_USER		= 11, // named user
	ZFSACL_GROUP		= 12, // named group
	ZFSACL_OTHER		= 13, // POSIX1e only
	ZFSACL_MASK		= 14, // POSIX1e only
} zfsace_who_t;

typedef enum {
	ZFSACL_ENTRY_TYPE_ALLOW	= 0,
	ZFSACL_ENTRY_TYPE_DENY	= 1,
	ZFSACL_ENTRY_TYPE_AUDIT	= 2,
	ZFSACL_ENTRY_TYPE_ALARM	= 3,
} zfsace_entry_type_t;

struct native_acl {
	void *data;
	size_t datalen;
	zfsacl_brand_t brand;
};

#ifdef __linux__
struct zfsacl_entry { uint_t netlong[5]; };
struct zfsacl {
	size_t aclbuf_size;
	zfsacl_brand_t brand;
	uint_t *aclbuf;
};
#else
#define	_ACL_PRIVATE
#define	zfsacl_entry acl_entry
#define	zfsacl acl_t_struct
#endif

typedef struct zfsacl_entry *zfsacl_entry_t;
typedef struct zfsacl *zfsacl_t;

typedef unsigned int zfsace_flagset_t;
typedef unsigned int zfsace_permset_t;
typedef uid_t zfsace_id_t;
typedef unsigned int zfsacl_aclflags_t;

#define	ZFSACL_UNDEFINED_ID	((uid_t)-1)
#define	ZFSACL_APPEND_ENTRY	-1
#define	ZFSACL_MAX_ENTRIES	1024

#ifndef	ARRAY_SIZE
#define	ARRAY_SIZE(a) ((int)(sizeof (a)/sizeof (a[0])))
#endif

boolean_t zfsacl_set_fd(int _fd, zfsacl_t _acl);
boolean_t zfsacl_set_file(const char *_path_p, zfsacl_t _acl);
boolean_t zfsacl_set_link(const char *_path_p, zfsacl_t _acl);

zfsacl_t zfsacl_get_fd(int fd, zfsacl_brand_t _brand);
zfsacl_t zfsacl_get_file(const char *_path_p, zfsacl_brand_t _brand);
zfsacl_t zfsacl_get_link(const char *_path_p, zfsacl_brand_t _brand);

boolean_t zfsacl_is_trivial(zfsacl_t _acl, boolean_t *trivialp);

/*
 * @brief initialize a new ZFS ACL (for setting on file)
 *        allocates memory that must be freed
 *
 * @param[in] _acecnt count of ACEs for new ACL
 * @param[in] _brand brand of ACL to allocate
 * @return new ACL on succcess, NULL on failure
 */
zfsacl_t zfsacl_init(int _acecnt, zfsacl_brand_t _brand);

/*
 * @brief free an ACL
 *
 * @param[in] *_acl free an ACL
 * @return always succeeds
 */
void zfsacl_free(zfsacl_t *_acl);

/*
 * @brief get branding for specified ACL
 *
 * @param[in] _acl the ACL from which to get branding info
 * @param[out] _brandp the brand (ACCESS, DEFAULT, NFSV4)
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_get_brand(zfsacl_t _acl, zfsacl_brand_t *_brandp);

/*
 * API to get / set ACL-wide flags
 * these are NFSv41-only
 */

/*
 * @brief get ACL-wide flags
 *
 * @param[in] _acl the ZFS ACL
 * @param[out] _paclflags ACL-wide flags
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_get_aclflags(zfsacl_t _acl, zfsacl_aclflags_t *_paclflags);

/*
 * @brief set ACL-wide flags
 *
 * @param[in] _acl ZFS ACL to modify
 * @param[in] _aclflags flags to set on ACL
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_set_aclflags(zfsacl_t _acl, zfsacl_aclflags_t _aclflags);

/*
 * @brief get number of ACL entries in ACL
 *
 * @param[in] _acl the ZFS ACL
 * @param[out] _acecnt number of ACEs in ACL.
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_get_acecnt(zfsacl_t _acl, uint_t *_acecnt);

/*
 * API to get, create, modify, and delete ACL entries
 */

/*
 * @brief create ACL entry at specified index
 *      special value ZFSACL_APPEND_ENTRY will create new entry
 *	at end of list.
 *
 * @param[in] _acl the ZFS ACL to modify
 * @param[in] _idx index of where to create new ACL entry
 * @param[out] _pentry new ACL entry created
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_create_aclentry(zfsacl_t _acl, int _idx,
    zfsacl_entry_t *_pentry);

/*
 * @brief get ACL entry at specified index
 *
 * @param[in] _acl ZFS ACL from which to get entry
 * @param[in] _idx index of ACL entry to retrieve
 * @param[out] _pentry ACL entry retrieved
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_get_aclentry(zfsacl_t _acl, int _idx,
    zfsacl_entry_t *_pentry);

/*
 * @brief remove ACL entry by index
 *
 * @param[in] _acl ZFS ACL from which to remove entry
 * @param[in] _idx index of ACL entry to remove
 * @return B_TRUE on success, B_FALSE on failure
 */
boolean_t zfsacl_delete_aclentry(zfsacl_t _acl, int _idx);


/*
 * @brief convert an ACL to text. Returns malloced string.
 *
 * @param[in] _acl ZFS ACL
 * @return pointer to text form the of specified ACLe
 */
char *zfsacl_to_text(zfsacl_t _acl);

boolean_t zfsacl_to_native(zfsacl_t _acl, struct native_acl *pnative);

/*
 * ACL entry specific functions
 */
boolean_t zfsace_get_permset(zfsacl_entry_t _entry,
    zfsace_permset_t *_pperm);
boolean_t zfsace_get_flagset(zfsacl_entry_t _entry,
    zfsace_flagset_t *_pflags);
boolean_t zfsace_get_who(zfsacl_entry_t _entry, zfsace_who_t *pwho,
    zfsace_id_t *_paeid);
boolean_t zfsace_get_entry_type(zfsacl_entry_t _entry,
    zfsace_entry_type_t *_tp);

boolean_t zfsace_set_permset(zfsacl_entry_t _entry, zfsace_permset_t _perm);
boolean_t zfsace_set_flagset(zfsacl_entry_t _entry, zfsace_flagset_t _flags);
boolean_t zfsace_set_who(zfsacl_entry_t _entry, zfsace_who_t _who,
    zfsace_id_t _aeid);
boolean_t zfsace_set_entry_type(zfsacl_entry_t _entry, zfsace_entry_type_t _tp);

zfsacl_t zfsacl_calculate_inherited_acl(zfsacl_t p, zfsacl_t t, boolean_t dir);


/*
 * NFSv4 ACL-wide flags
 * used in zfsacl_get_aclflags() and zfsacl_set_aclflags()
 */

/*
 * ACL flags
 */
#define	ZFSACL_AUTO_INHERIT			0x0001
#define	ZFSACL_PROTECTED			0x0002
#define	ZFSACL_DEFAULTED			0x0004
#define	ZFSACL_FLAGS_ALL   \
	(ZFSACL_AUTO_INHERIT|ZFSACL_PROTECTED|ZFSACL_DEFAULTED)

#define	ZFSACL_FLAGS_INVALID(flags) (flags & ~ZFSACL_FLAGS_ALL)
/*
 * ZFS pflags exposed via ACL call as ACL flags
 * valid on get, but not set
 */
#define	ZFSACL_IS_TRIVIAL			0x10000
#define	ZFSACL_IS_DIR				0x20000

#define	ZFSACE_TYPE_INVALID(ae_type) (ae_type > ZFSACL_ENTRY_TYPE_DENY)

/*
 * NFSv4 ACL inheritance flags
 * These are not valid if ACL is branded POSIX ACCESS or DEFAULT
 */
#define	ZFSACE_FILE_INHERIT			0x00000001
#define	ZFSACE_DIRECTORY_INHERIT		0x00000002
#define	ZFSACE_NO_PROPAGATE_INHERIT		0x00000004
#define	ZFSACE_INHERIT_ONLY			0x00000008
#define	ZFSACE_SUCCESSFUL_ACCESS_ACE_FLAG	0x00000010
#define	ZFSACE_FAILED_ACCESS_ACE_FLAG		0x00000020
#define	ZFSACE_IDENTIFIER_GROUP			0x00000040
#define	ZFSACE_INHERITED_ACE			0x00000080

#define	ZFSACE_IS_GROUP(flags) (flags & ZFSACE_IDENTIFIER_GROUP)

#define	ZFSACE_FLAG_INVALID(flags) ((flags & 0xFFFFFF30) || ( \
	(flags & ZFSACE_INHERIT_ONLY) && \
	(flags & !(ZFSACE_FILE_INHERIT | ZFSACE_DIRECTORY_INHERIT))))

/*
 * NFSv4 ACL permissions
 */
#define	ZFSACE_READ_DATA			0x00000001
#define	ZFSACE_LIST_DIRECTORY			0x00000001
#define	ZFSACE_WRITE_DATA			0x00000002
#define	ZFSACE_ADD_FILE				0x00000002
#define	ZFSACE_APPEND_DATA			0x00000004
#define	ZFSACE_ADD_SUBDIRECTORY			0x00000004
#define	ZFSACE_READ_NAMED_ATTRS			0x00000008
#define	ZFSACE_WRITE_NAMED_ATTRS		0x00000010
#define	ZFSACE_EXECUTE				0x00000020
#define	ZFSACE_DELETE_CHILD			0x00000040
#define	ZFSACE_READ_ATTRIBUTES			0x00000080
#define	ZFSACE_WRITE_ATTRIBUTES			0x00000100
#define	ZFSACE_DELETE				0x00010000
#define	ZFSACE_READ_ACL				0x00020000
#define	ZFSACE_WRITE_ACL			0x00040000
#define	ZFSACE_WRITE_OWNER			0x00080000
#define	ZFSACE_SYNCHRONIZE			0x00100000

#define	ZFSACE_FULL_SET 	(ZFSACE_READ_DATA | ZFSACE_WRITE_DATA | \
    ZFSACE_APPEND_DATA | ZFSACE_READ_NAMED_ATTRS | ZFSACE_WRITE_NAMED_ATTRS | \
    ZFSACE_EXECUTE | ZFSACE_DELETE_CHILD | ZFSACE_READ_ATTRIBUTES | \
    ZFSACE_WRITE_ATTRIBUTES | ZFSACE_DELETE | ZFSACE_READ_ACL | \
    ZFSACE_WRITE_ACL | ZFSACE_WRITE_OWNER | ZFSACE_SYNCHRONIZE)

#define	ZFSACE_MODIFY_SET	(ZFSACE_FULL_SET & \
	~(ZFSACE_WRITE_ACL | ZFSACE_WRITE_OWNER))

#define	ZFSACE_READ_SET		(ZFSACE_READ_DATA | ZFSACE_READ_NAMED_ATTRS | \
    ZFSACE_READ_ATTRIBUTES | ZFSACE_READ_ACL)

#define	ZFSACE_WRITE_SET	(ZFSACE_WRITE_DATA | ZFSACE_APPEND_DATA | \
    ZFSACE_WRITE_NAMED_ATTRS | ZFSACE_WRITE_ATTRIBUTES)

#define	ZFSACE_TRAVERSE_SET	(ZFSACE_EXECUTE | ZFSACE_READ_NAMED_ATTRS | \
    ZFSACE_READ_ATTRIBUTES | ZFSACE_READ_ACL)

#define	ZFSACE_ACCESS_MASK_INVALID(mask) (mask & ~ZFSACE_FULL_SET)

#define	SPECIAL_WHO_INVALID(who) ( \
	(who != ZFSACL_USER_OBJ) && (who != ZFSACL_USER) && \
	(who != ZFSACL_GROUP_OBJ) && (who != ZFSACL_GROUP) && \
	(who != ZFSACL_EVERYONE))

static const struct {
	zfsacl_aclflags_t flag;
	const char *name;
} aclflag2name[] = {
	{ ZFSACL_AUTO_INHERIT, "AUTO_INHERIT" },
	{ ZFSACL_PROTECTED, "PROTECTED" },
	{ ZFSACL_DEFAULTED, "DEFAULTED" },
	{ ZFSACL_IS_TRIVIAL, "ACL_IS_TRIVIAL" },
	{ ZFSACL_IS_DIR, "IS_DIRECTORY" },
};

static const struct {
	zfsace_permset_t perm;
	const char *name;
	char letter;
} aceperm2name[] = {
	{ ZFSACE_READ_DATA, "READ_DATA", 'r' },
	{ ZFSACE_LIST_DIRECTORY, "LIST_DIRECTORY", '\0' },
	{ ZFSACE_WRITE_DATA, "WRITE_DATA", 'w' },
	{ ZFSACE_ADD_FILE, "ADD_FILE", '\0' },
	{ ZFSACE_APPEND_DATA, "APPEND_DATA", 'p' },
	{ ZFSACE_DELETE, "DELETE", 'd' },
	{ ZFSACE_DELETE_CHILD, "DELETE_CHILD", 'D' },
	{ ZFSACE_ADD_SUBDIRECTORY, "ADD_SUBDIRECTORY", '\0' },
	{ ZFSACE_READ_ATTRIBUTES, "READ_ATTRIBUTES", 'a' },
	{ ZFSACE_WRITE_ATTRIBUTES, "WRITE_ATTRIBUTES", 'A' },
	{ ZFSACE_READ_NAMED_ATTRS, "READ_NAMED_ATTRS", 'R' },
	{ ZFSACE_WRITE_NAMED_ATTRS, "WRITE_NAMED_ATTRS", 'W' },
	{ ZFSACE_READ_ACL, "READ_ACL", 'c' },
	{ ZFSACE_WRITE_ACL, "WRITE_ACL", 'C' },
	{ ZFSACE_WRITE_OWNER, "WRITE_OWNER", 'o' },
	{ ZFSACE_SYNCHRONIZE, "SYNCHRONIZE", 's' },
};

static const struct {
	zfsace_flagset_t flag;
	const char *name;
	char letter;
} aceflag2name[] = {
	{ ZFSACE_FILE_INHERIT, "FILE_INHERIT", 'f' },
	{ ZFSACE_DIRECTORY_INHERIT, "DIRECTORY_INHERIT", 'd' },
	{ ZFSACE_INHERIT_ONLY, "INHERIT_ONLY", 'i' },
	{ ZFSACE_NO_PROPAGATE_INHERIT, "NO_PROPAGATE_INHERIT", 'n' },
	{ ZFSACE_INHERITED_ACE, "INHERITED", 'I' },
};

static const struct {
	zfsace_who_t who;
	const char *name;
} acewho2name[] = {
	{ ZFSACL_UNDEFINED_TAG, "UNDEFINED" },
	{ ZFSACL_USER_OBJ, "USER_OBJ" },
	{ ZFSACL_GROUP_OBJ, "GROUP_OBJ" },
	{ ZFSACL_EVERYONE, "EVERYONE" },
	{ ZFSACL_USER, "USER" },
	{ ZFSACL_GROUP, "GROUP" },
	{ ZFSACL_OTHER, "OTHER" },
	{ ZFSACL_MASK, "MASK" },
};

#endif /* __ZFSACL_H__ */
