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

#include <zfsacl.h>
#include "/usr/include/sys/acl.h"

#define	BSDACE(zfsace) ((acl_entry_t)zfsace)
#define	BSDACL(zfsacl) ((acl_t)zfsacl)
#define	ZFSACL(bsdacl) ((zfsacl_t)bsdacl)

static const struct {
	acl_flag_t bsdflag;
	zfsace_flagset_t nfs4flag;
} bsdflag2nfs4flag[] = {
	{ ACL_ENTRY_FILE_INHERIT, ZFSACE_FILE_INHERIT },
	{ ACL_ENTRY_DIRECTORY_INHERIT, ZFSACE_DIRECTORY_INHERIT },
	{ ACL_ENTRY_NO_PROPAGATE_INHERIT, ZFSACE_NO_PROPAGATE_INHERIT },
	{ ACL_ENTRY_INHERIT_ONLY, ZFSACE_INHERIT_ONLY },
	{ ACL_ENTRY_INHERITED, ZFSACE_INHERITED_ACE },
};

static const struct {
	acl_perm_t bsdperm;
	zfsace_permset_t nfs4perm;
} bsdperm2nfs4perm[] = {
	{ ACL_READ_DATA, ZFSACE_READ_DATA },
	{ ACL_WRITE_DATA, ZFSACE_WRITE_DATA },
	{ ACL_APPEND_DATA, ZFSACE_APPEND_DATA },
	{ ACL_READ_NAMED_ATTRS, ZFSACE_READ_NAMED_ATTRS },
	{ ACL_WRITE_NAMED_ATTRS, ZFSACE_WRITE_NAMED_ATTRS },
	{ ACL_EXECUTE, ZFSACE_EXECUTE },
	{ ACL_DELETE_CHILD, ZFSACE_DELETE_CHILD },
	{ ACL_READ_ATTRIBUTES, ZFSACE_READ_ATTRIBUTES },
	{ ACL_WRITE_ATTRIBUTES, ZFSACE_WRITE_ATTRIBUTES },
	{ ACL_DELETE, ZFSACE_DELETE },
	{ ACL_READ_ACL, ZFSACE_READ_ACL },
	{ ACL_WRITE_ACL, ZFSACE_WRITE_ACL },
	{ ACL_WRITE_OWNER, ZFSACE_WRITE_OWNER },
	{ ACL_SYNCHRONIZE, ZFSACE_SYNCHRONIZE },
};

static inline int
CONV_BRAND(zfsacl_brand_t brand_in)
{
	return (brand_in & ACL_BRAND_POSIX) ^ (brand_in & ACL_BRAND_NFS4);
}

static inline void
BSD_BRAND(acl_t _acl)
{
	_acl->ats_brand = CONV_BRAND(_acl->ats_brand);
}

static inline acl_type_t
brand_to_type(zfsacl_brand_t _brand)
{
	acl_type_t out = 0;

	switch (_brand) {
	case ZFSACL_BRAND_NFSV4:
		out = ACL_TYPE_NFS4;
		break;
	case ZFSACL_BRAND_ACCESS:
		out = ACL_TYPE_ACCESS;
		break;
	case ZFSACL_BRAND_DEFAULT:
		out = ACL_TYPE_DEFAULT;
		break;
	default:
		fprintf(stderr, "0x%08x: invalid ACL brand\n", _brand);
		break;
	};

	return (out);
}

zfsacl_t
zfsacl_init(int _acecnt, zfsacl_brand_t _brand)
{
	acl_t out = NULL;

	out = acl_init(_acecnt);
	if (out == NULL) {
		return (NULL);
	}
	out->ats_brand = _brand;
	return (ZFSACL(out));
}

void
zfsacl_free(zfsacl_t *_acl)
{
	acl_t acl = BSDACL(*_acl);
	acl_free(acl);
	*_acl = NULL;
}

zfsacl_t
zfsacl_get_fd(int _fd, zfsacl_brand_t _brand)
{
	acl_t out = NULL;

	out = acl_get_fd_np(_fd, brand_to_type(_brand));
	if (out == NULL) {
		return (NULL);
	}
	out->ats_brand = _brand;
	return (ZFSACL(out));
}

zfsacl_t
zfsacl_get_file(const char *_path_p, zfsacl_brand_t _brand)
{
	acl_t out = NULL;

	out = acl_get_file(_path_p, brand_to_type(_brand));
	if (out == NULL) {
		return (NULL);
	}
	out->ats_brand = _brand;
	return (ZFSACL(out));
}

zfsacl_t
zfsacl_get_link(const char *_path_p, zfsacl_brand_t _brand)
{
	acl_t out = NULL;

	out = acl_get_link_np(_path_p, brand_to_type(_brand));
	if (out == NULL) {
		return (NULL);
	}
	out->ats_brand = _brand;
	return (ZFSACL(out));
}

boolean_t
zfsacl_set_fd(int _fd, zfsacl_t _acl)
{
	acl_t acl = BSDACL(_acl);
	zfsacl_brand_t saved = acl->ats_brand;
	int err;

	BSD_BRAND(acl);
	err = acl_set_fd_np(_fd, acl, brand_to_type(saved));
	acl->ats_brand = saved;

	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsacl_set_file(const char *_path_p, zfsacl_t _acl)
{
	acl_t acl = BSDACL(_acl);
	zfsacl_brand_t saved = acl->ats_brand;
	int err;

	BSD_BRAND(acl);
	err = acl_set_file(_path_p, brand_to_type(saved), acl);
	acl->ats_brand = saved;

	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsacl_set_link(const char *_path_p, zfsacl_t _acl)
{
	acl_t acl = BSDACL(_acl);
	zfsacl_brand_t saved = acl->ats_brand;
	int err;

	BSD_BRAND(acl);
	err = acl_set_link_np(_path_p, brand_to_type(saved), acl);
	acl->ats_brand = saved;

	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsacl_get_brand(zfsacl_t _acl, zfsacl_brand_t *brandp)
{
	acl_t acl = BSDACL(_acl);
	*brandp = acl->ats_brand;
	return (B_TRUE);
}

boolean_t
zfsacl_get_aclflags(zfsacl_t _acl, zfsacl_aclflags_t *pflags)
{
	/*
	 * TODO: FreeBSD still needs to expose ACL flags
	 * for now we synthesize the PROTECTED flag so that
	 * Security Descriptor flags can be presented correctly
	 * to clients.
	 */
	acl_t acl = BSDACL(_acl);
	unsigned int cnt;
	zfsace_flagset_t flags_out = 0;
	acl_flag_t flags;

	for (cnt = 0; cnt < acl->ats_acl.acl_cnt; cnt++) {
		flags = acl->ats_acl.acl_entry[cnt].ae_flags;
		if ((flags & ACL_ENTRY_INHERITED) == 0)
			continue;

		flags_out = ZFSACL_PROTECTED;
		break;
	}

	*pflags = flags_out;
	return (B_TRUE);
}


boolean_t
zfsacl_set_aclflags(zfsacl_t _acl, zfsacl_aclflags_t flags)
{
	/*
	 * TODO: FreeBSD still needs to expose ACL flags
	 */
	(void) _acl;
	(void) flags;
	errno = EOPNOTSUPP;
	return (B_FALSE);
}

boolean_t
zfsacl_get_acecnt(zfsacl_t _acl, uint_t *_acecnt)
{
	acl_t acl = BSDACL(_acl);
	*_acecnt = acl->ats_acl.acl_cnt;
	return (B_TRUE);
}

static boolean_t
validate_entry_idx(zfsacl_t _acl, uint_t _idx)
{
	uint_t acecnt;
	boolean_t ok;

	ok = zfsacl_get_acecnt(_acl, &acecnt);
	if (!ok) {
		return (B_FALSE);
	}

	if ((_idx + 1) > acecnt) {
		errno = E2BIG;
		return (B_FALSE);
	}

	return (B_TRUE);
}

boolean_t
zfsacl_create_aclentry(zfsacl_t _acl, int _idx, zfsacl_entry_t *_pentry)
{
	acl_t acl = BSDACL(_acl);
	int err;
	zfsacl_brand_t saved = acl->ats_brand;
	acl_entry_t new_entry = NULL;

	BSD_BRAND(acl);
	if (_idx == ZFSACL_APPEND_ENTRY) {
		err = acl_create_entry(&acl, &new_entry);
	} else {
		err = acl_create_entry_np(&acl, &new_entry, _idx);
	}

	acl->ats_brand = saved;

	if (err) {
		return (B_FALSE);
	}

	*_pentry = new_entry;
	return (B_TRUE);
}

boolean_t
zfsacl_get_aclentry(zfsacl_t _acl, int _idx, zfsacl_entry_t *_pentry)
{
	acl_t acl = BSDACL(_acl);
	acl_entry_t entry = NULL;

	if (!validate_entry_idx(_acl, _idx)) {
		return (B_FALSE);
	}

	entry = &acl->ats_acl.acl_entry[_idx];
	*_pentry = entry;
	return (B_TRUE);
}

boolean_t
zfsacl_delete_aclentry(zfsacl_t _acl, int _idx)
{
	acl_t acl = BSDACL(_acl);
	int err;
	zfsacl_brand_t saved = acl->ats_brand;

	BSD_BRAND(acl);

	err = acl_delete_entry_np(acl, _idx);
	acl->ats_brand = saved;

	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsace_get_permset(zfsacl_entry_t _entry, zfsace_permset_t *_pperm)
{
	acl_entry_t entry = BSDACE(_entry);
	zfsace_permset_t perm = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(bsdperm2nfs4perm); i++) {
		if (entry->ae_perm & bsdperm2nfs4perm[i].bsdperm) {
			perm |= bsdperm2nfs4perm[i].nfs4perm;
		}
	}

	*_pperm = perm;
	return (B_TRUE);
}

boolean_t
zfsace_get_flagset(zfsacl_entry_t _entry, zfsace_flagset_t *_pflags)
{
	acl_entry_t entry = BSDACE(_entry);
	zfsace_flagset_t flags = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(bsdflag2nfs4flag); i++) {
		if (entry->ae_flags & bsdflag2nfs4flag[i].bsdflag) {
			flags |= bsdflag2nfs4flag[i].nfs4flag;
		}
	}

	if (entry->ae_tag & (ACL_GROUP_OBJ | ACL_GROUP)) {
		flags |= ZFSACE_IDENTIFIER_GROUP;
	}

	*_pflags = flags;
	return (B_TRUE);
}

boolean_t
zfsace_get_who(zfsacl_entry_t _entry, zfsace_who_t *pwho, zfsace_id_t *_paeid)
{
	acl_entry_t entry = BSDACE(_entry);
	zfsace_who_t whotype;
	zfsace_id_t whoid = ZFSACL_UNDEFINED_ID;

	switch (entry->ae_tag) {
	case ACL_UNDEFINED_TAG:
		whotype = ZFSACL_UNDEFINED_TAG;
		break;
	case ACL_USER_OBJ:
		whotype = ZFSACL_USER_OBJ;
		break;
	case ACL_GROUP_OBJ:
		whotype = ZFSACL_GROUP_OBJ;
		break;
	case ACL_EVERYONE:
		whotype = ZFSACL_EVERYONE;
		break;
	case ACL_MASK:
		whotype = ZFSACL_MASK;
		break;
	case ACL_OTHER:
		whotype = ZFSACL_MASK;
		break;
	case ACL_USER:
		whotype = ZFSACL_USER;
		whoid = entry->ae_id;
		break;
	case ACL_GROUP:
		whotype = ZFSACL_GROUP;
		whoid = entry->ae_id;
		break;
	default:
		abort();
	};

	*pwho = whotype;
	*_paeid = whoid;
	return (B_TRUE);
}

boolean_t
zfsace_get_entry_type(zfsacl_entry_t _entry, zfsace_entry_type_t *_tp)
{
	acl_entry_t entry = BSDACE(_entry);
	zfsace_entry_type_t etype;

	switch (entry->ae_entry_type) {
	case ACL_ENTRY_TYPE_ALLOW:
		etype = ZFSACL_ENTRY_TYPE_ALLOW;
		break;
	case ACL_ENTRY_TYPE_DENY:
		etype = ZFSACL_ENTRY_TYPE_DENY;
		break;
	case ACL_ENTRY_TYPE_AUDIT:
		etype = ZFSACL_ENTRY_TYPE_AUDIT;
		break;
	case ACL_ENTRY_TYPE_ALARM:
		etype = ZFSACL_ENTRY_TYPE_AUDIT;
		break;
	default:
		abort();
	};

	*_tp = etype;
	return (B_TRUE);
}

boolean_t
zfsace_set_permset(zfsacl_entry_t _entry, zfsace_permset_t _permset)
{
	acl_entry_t entry = BSDACE(_entry);
#if __FreeBSD_version < 1500000
	int permset = 0;
#else
	unsigned int permset = 0;
#endif
	int i, err;

	for (i = 0; i < ARRAY_SIZE(bsdperm2nfs4perm); i++) {
		if (_permset & bsdperm2nfs4perm[i].nfs4perm) {
			permset |= bsdperm2nfs4perm[i].bsdperm;
		}
	}

	err = acl_set_permset(entry, &permset);
	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsace_set_flagset(zfsacl_entry_t _entry, zfsace_flagset_t _flagset)
{
	acl_entry_t entry = BSDACE(_entry);
	acl_flag_t flags = 0;
	int i, err;

	for (i = 0; i < ARRAY_SIZE(bsdflag2nfs4flag); i++) {
		if (_flagset & bsdflag2nfs4flag[i].nfs4flag) {
			flags |= bsdflag2nfs4flag[i].bsdflag;
		}
	}

	err = acl_set_flagset_np(entry, &flags);
	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsace_set_who(zfsacl_entry_t _entry, zfsace_who_t _who, zfsace_id_t _aeid)
{
	acl_entry_t entry = BSDACE(_entry);
	uid_t id = ACL_UNDEFINED_ID;
	acl_tag_t tag;
	int err;

	switch (_who) {
	case ZFSACL_USER_OBJ:
		tag = ACL_USER_OBJ;
		break;
	case ZFSACL_GROUP_OBJ:
		tag = ACL_GROUP_OBJ;
		break;
	case ZFSACL_EVERYONE:
		tag = ACL_EVERYONE;
		break;
	case ZFSACL_OTHER:
		tag = ACL_OTHER;
		break;
	case ZFSACL_MASK:
		tag = ACL_MASK;
		break;
	case ZFSACL_USER:
		tag = ACL_USER;
		id = _aeid;
		break;
	case ZFSACL_GROUP:
		tag = ACL_GROUP;
		id = _aeid;
		break;
	default:
		abort();
	};

	err = acl_set_tag_type(entry, tag);
	if (err)
		return (B_FALSE);

	if (id != ACL_UNDEFINED_ID) {
		err = acl_set_qualifier(entry, &id);
	}

	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsace_set_entry_type(zfsacl_entry_t _entry, zfsace_entry_type_t _tp)
{
	acl_entry_t entry = BSDACE(_entry);
	acl_entry_type_t etype;
	int err;

	switch (_tp) {
	case ZFSACL_ENTRY_TYPE_ALLOW:
		etype = ACL_ENTRY_TYPE_ALLOW;
		break;
	case ZFSACL_ENTRY_TYPE_DENY:
		etype = ACL_ENTRY_TYPE_DENY;
		break;
	case ZFSACL_ENTRY_TYPE_AUDIT:
		etype = ACL_ENTRY_TYPE_AUDIT;
		break;
	case ZFSACL_ENTRY_TYPE_ALARM:
		etype = ACL_ENTRY_TYPE_ALARM;
		break;
	default:
		abort();
	};

	err = acl_set_entry_type_np(entry, etype);

	return (err ? B_FALSE : B_TRUE);
}

boolean_t
zfsacl_to_native(zfsacl_t _acl, struct native_acl *pnative)
{
	(void) _acl;
	(void) pnative;
	errno = EOPNOTSUPP;
	return (B_FALSE);
}

boolean_t
zfsacl_is_trivial(zfsacl_t _acl, boolean_t *_trivialp)
{
	acl_t acl = BSDACL(_acl);
	int err, triv;

	err = acl_is_trivial_np(acl, &triv);
	if (err) {
		return (B_FALSE);
	}

	*_trivialp = (triv == 1) ? B_TRUE : B_FALSE;
	return (B_TRUE);
}

char *
zfsacl_to_text(zfsacl_t _acl)
{
	acl_t acl = BSDACL(_acl);
	return (acl_to_text_np(acl, NULL, ACL_TEXT_NUMERIC_IDS));
}
