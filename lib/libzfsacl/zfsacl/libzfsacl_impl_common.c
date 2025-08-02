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

#include <zfsacl.h>

#define	INHERITANCE_FLAGS ZFSACE_FILE_INHERIT | \
	ZFSACE_DIRECTORY_INHERIT | \
	ZFSACE_NO_PROPAGATE_INHERIT

static boolean_t
copy_ace(zfsacl_entry_t target, zfsacl_entry_t source,
    zfsace_flagset_t new_flags)
{
	zfsace_permset_t aeperms;
	zfsace_who_t aewho;
	zfsace_id_t aeid;
	zfsace_entry_type_t aetp;

	if (!zfsace_get_permset(source, &aeperms)) {
		return (B_FALSE);
	}

	if (!zfsace_get_who(source, &aewho, &aeid)) {
		return (B_FALSE);
	}

	if (!zfsace_get_entry_type(source, &aetp)) {
		return (B_FALSE);
	}

	if (!zfsace_set_permset(target, aeperms)) {
		return (B_FALSE);
	}

	if (!zfsace_set_who(target, aewho, aeid)) {
		return (B_FALSE);
	}

	if (!zfsace_set_entry_type(target, aetp)) {
		return (B_FALSE);
	}

	if (!zfsace_set_flagset(target, new_flags)) {
		return (B_FALSE);
	}

	return (B_TRUE);
}

static boolean_t
add_non_inherited_entries(zfsacl_t target, zfsacl_t source)
{
	uint_t i, cnt;

	if (!zfsacl_get_acecnt(source, &cnt)) {
		return (B_FALSE);
	}

	for (i = 0; i < cnt; i++) {
		zfsacl_entry_t ae = NULL;
		zfsacl_entry_t new = NULL;
		zfsace_flagset_t flags = 0;

		if (!zfsacl_get_aclentry(source, i, &ae)) {
			return (B_FALSE);
		}

		if (!zfsace_get_flagset(ae, &flags)) {
			return (B_FALSE);
		}

		if (flags & ZFSACE_INHERITED_ACE) {
			continue;
		}

		if (!zfsacl_create_aclentry(target, ZFSACL_APPEND_ENTRY,
		    &new)) {
			return (B_FALSE);
		}

		if (!copy_ace(new, ae, flags)) {
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

static boolean_t
add_inherited_ace(zfsacl_t target, zfsacl_entry_t ae,
    zfsace_flagset_t flags, boolean_t isdir)
{
	zfsacl_entry_t new = NULL;

	if (!zfsacl_create_aclentry(target, ZFSACL_APPEND_ENTRY,
	    &new)) {
		return (B_FALSE);
	}

	if (isdir) {
		if (flags & ZFSACE_INHERIT_ONLY) {
			flags &= ~ZFSACE_INHERIT_ONLY;
		} else if (flags & ZFSACE_NO_PROPAGATE_INHERIT) {
			flags &= ~INHERITANCE_FLAGS;
		}
	} else {
		flags &= ~(ZFSACE_INHERIT_ONLY | INHERITANCE_FLAGS);
	}

	flags |= ZFSACE_INHERITED_ACE;

	return (copy_ace(new, ae, flags));
}

static boolean_t
add_inherited_entries(zfsacl_t target, zfsacl_t source, boolean_t isdir)
{
	uint_t i, cnt;

	if (!zfsacl_get_acecnt(source, &cnt)) {
		return (B_FALSE);
	}

	for (i = 0; i < cnt; i++) {
		zfsacl_entry_t ae = NULL;
		zfsace_flagset_t flags = 0;

		if (!zfsacl_get_aclentry(source, i, &ae)) {
			return (B_FALSE);
		}

		if (!zfsace_get_flagset(ae, &flags)) {
			return (B_FALSE);
		}

		if ((flags & (ZFSACE_FILE_INHERIT |
		    ZFSACE_DIRECTORY_INHERIT)) == 0) {
			// Not inheritable, skip
			continue;
		}

		if (((flags & ZFSACE_DIRECTORY_INHERIT) == 0) &&
		    isdir) {
			/*
			 * Inheritable only on files and this ACL is for
			 * a directory.
			 */
			continue;
		}

		if (((flags & ZFSACE_FILE_INHERIT) == 0) && !isdir) {
			/*
			 * Inheritable only on directories and this ACL
			 * is for a file.
			 */
			continue;
		}

		if (!add_inherited_ace(target, ae, flags, isdir)) {
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

/*
 * Permissions auto-inheritance is only a NFSv4 ACL feature
 */
static boolean_t
acl_may_inherit(zfsacl_t parent, zfsacl_t target)
{
	zfsacl_brand_t brand;

	if (parent == NULL) {
		errno = EINVAL;
		return (B_FALSE);
	}

	if (!zfsacl_get_brand(parent, &brand)) {
		return (B_FALSE);
	}

	if (brand != ZFSACL_BRAND_NFSV4) {
		errno = EOPNOTSUPP;
		return (B_FALSE);
	}

	if (target) {
		if (!zfsacl_get_brand(target, &brand)) {
			return (B_FALSE);
		}


		if (brand != ZFSACL_BRAND_NFSV4) {
			errno = EOPNOTSUPP;
			return (B_FALSE);
		}
	}

	return (B_TRUE);
}

zfsacl_t
zfsacl_calculate_inherited_acl(zfsacl_t parent, zfsacl_t target,
    boolean_t is_dir)
{
	zfsacl_t out = NULL;

	if (!acl_may_inherit(parent, target)) {
		return (NULL);
	}

	out = zfsacl_init(ZFSACL_MAX_ENTRIES, ZFSACL_BRAND_NFSV4);

	if (target) {
		if (!add_non_inherited_entries(out, target)) {
			zfsacl_free(&out);
			return (NULL);
		}
	}

	if (!add_inherited_entries(out, parent, is_dir)) {
		zfsacl_free(&out);
		return (NULL);
	}

	return (out);
}
