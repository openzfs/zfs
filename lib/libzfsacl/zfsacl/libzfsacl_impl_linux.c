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

#include <arpa/inet.h>
#include <sys/xattr.h>
#include <assert.h>
#include <zfsacl.h>


#define	ACL4_MAX_ENTRIES	ZFSACL_MAX_ENTRIES
#define	ACL4_XATTR	"system.nfs4_acl_xdr"

/* Non-ACL metadata */
#define	ACL_GET_SZ(aclp) ((size_t)*(aclp))

/* NFSv4 ACL metadata */
#define	ACL4_GET_FL(aclp) (aclp)
#define	ACL4_GET_CNT(aclp) (ACL4_GET_FL(aclp) + 1)

/* NFSv4 ACL ENTRY */
#define	ACE4_SZ (sizeof (uint_t) * 5)
#define	ACL4_METADATA (sizeof (uint_t) * 2)
#define	ACL4SZ_FROM_ACECNT(cnt) (ACL4_METADATA + (cnt * ACE4_SZ))
#define	ACL4_GETENTRY(aclp, idx) \
	(zfsacl_entry_t)((char *)aclp + ACL4SZ_FROM_ACECNT(idx))
#define	ACLBUF_TO_ACES(aclp) (

#define	zfsace4	zfsacl_entry
#define	ACL4BUF_TO_ACES(aclp)	((struct zfsace4 *)(aclp + 2))

static boolean_t
acl_check_brand(zfsacl_t _acl, zfsacl_brand_t expected)
{
	if (_acl->brand != expected) {
#if ZFS_DEBUG
		(void) fprintf(stderr, "Incorrect ACL brand");
#endif
		errno = ENOSYS;
		return (B_FALSE);
	}
	return (B_TRUE);
}

zfsacl_t
zfsacl_init(int _acecnt, zfsacl_brand_t _brand)
{
	size_t naclsz;
	zfsacl_t out = NULL;
	if (_brand != ZFSACL_BRAND_NFSV4) {
		errno = EINVAL;
		return (NULL);
	}

	out = calloc(1, sizeof (struct zfsacl));
	if (out == NULL) {
		return (NULL);
	}

	naclsz = ACL4SZ_FROM_ACECNT(_acecnt);
	out->aclbuf = calloc(naclsz, sizeof (char));
	if (out->aclbuf == NULL) {
		free(out);
		return (NULL);
	}
	out->brand = _brand;
	out->aclbuf_size = naclsz;
	return (out);
}

void
zfsacl_free(zfsacl_t *_pacl)
{
	zfsacl_t to_free = *_pacl;
	free(to_free->aclbuf);
	free(to_free);
	*_pacl = NULL;
}

boolean_t
zfsacl_get_brand(zfsacl_t _acl, zfsacl_brand_t *_brandp)
{
	*_brandp = _acl->brand;
	return (B_TRUE);
}

boolean_t
zfsacl_get_aclflags(zfsacl_t _acl, zfsacl_aclflags_t *_paclflags)
{
	zfsacl_aclflags_t flags;

	if (!acl_check_brand(_acl, ZFSACL_BRAND_NFSV4)) {
		return (B_FALSE);
	}

	flags = ntohl(*ACL4_GET_FL(_acl->aclbuf));
	*_paclflags = flags;
	return (B_TRUE);
}

boolean_t
zfsacl_set_aclflags(zfsacl_t _acl, zfsacl_aclflags_t _aclflags)
{
	zfsacl_aclflags_t *flags;

	if (!acl_check_brand(_acl, ZFSACL_BRAND_NFSV4)) {
		return (B_FALSE);
	}

	if (ZFSACL_FLAGS_INVALID(_aclflags)) {
#if ZFS_DEBUG
		(void) fprintf(stderr, "Incorrect ACL brand");
#endif
		errno = EINVAL;
		return (B_FALSE);
	}

	flags = ACL4_GET_FL(_acl->aclbuf);
	*flags = htonl(_aclflags);

	return (B_TRUE);
}

boolean_t
zfsacl_get_acecnt(zfsacl_t _acl, uint_t *pcnt)
{
	uint_t acecnt;
	if (!acl_check_brand(_acl, ZFSACL_BRAND_NFSV4)) {
		return (B_FALSE);
	}

	acecnt = ntohl(*ACL4_GET_CNT(_acl->aclbuf));
	*pcnt = acecnt;
	return (B_TRUE);
}


static boolean_t
validate_entry_idx(zfsacl_t _acl, int _idx)
{
	uint_t acecnt;
	boolean_t ok;

	ok = zfsacl_get_acecnt(_acl, &acecnt);
	if (!ok) {
		return (B_FALSE);
	}

	if ((((uint_t)_idx) + 1) > acecnt) {
		errno = E2BIG;
		return (B_FALSE);
	}

	return (B_TRUE);
}

/* out will be set to new required size if realloc required */
static boolean_t
acl_get_new_size(zfsacl_t _acl, uint_t new_count, size_t *out)
{
	size_t current_sz, required_sz;

	if (new_count > ACL4_MAX_ENTRIES) {
		errno = E2BIG;
		return (B_FALSE);
	}
	current_sz = _acl->aclbuf_size;
	required_sz = ACL4SZ_FROM_ACECNT(new_count);

	if (current_sz >= required_sz) {
		*out = 0;
	} else {
		*out = required_sz;
	}

	return (B_TRUE);
}

boolean_t
zfsacl_create_aclentry(zfsacl_t _acl, int _idx, zfsacl_entry_t *_pentry)
{
	uint_t acecnt;
	uint_t *pacecnt;
	zfsacl_entry_t entry;
	size_t new_size, new_offset, acl_size;
	boolean_t ok;
	struct zfsace4 *z = ACL4BUF_TO_ACES(_acl->aclbuf);

	ok = zfsacl_get_acecnt(_acl, &acecnt);
	if (!ok) {
		return (B_FALSE);
	}

	if ((_idx != ZFSACL_APPEND_ENTRY) && (((uint_t)_idx) + 1 > acecnt)) {
		errno = ERANGE;
		return (B_FALSE);
	}

	ok = acl_get_new_size(_acl, acecnt + 1, &new_size);
	if (!ok) {
		return (B_FALSE);
	}

	acl_size = _acl->aclbuf_size;

	if (new_size != 0) {
		zfsacl_t _tmp = realloc(_acl->aclbuf, new_size);
		if (_tmp == NULL) {
			errno = ENOMEM;
			return (B_FALSE);
		}
		_acl->aclbuf = (uint_t *)_tmp;
		_acl->aclbuf_size = new_size;
		assert(new_size == (acl_size + ACE4_SZ));
		memset(_acl->aclbuf + (new_size - ACE4_SZ), 0, ACE4_SZ);
	}

	if (_idx == ZFSACL_APPEND_ENTRY) {
		*_pentry = &z[acecnt];
		goto done;
	}

	new_offset = ACL4SZ_FROM_ACECNT(_idx);

	/*
	 * shift back one ace from offset
	 * to make room for new entry
	 */
	entry = &z[_idx];
	memmove(entry + 1, entry, acl_size - new_offset - ACE4_SZ);

	/* zero-out new ACE */
	memset(entry, 0, ACE4_SZ);
	*_pentry = entry;

done:
	pacecnt = ACL4_GET_CNT(_acl->aclbuf);
	*pacecnt = htonl(acecnt + 1);
	return (B_TRUE);
}

#if ZFS_DEBUG
static void
dump_entry(struct zfsace4 *z)
{
	fprintf(stderr,
	    "0x%08X  %p "
	    "0x%08X  %p "
	    "0x%08X  %p "
	    "0x%08X  %p "
	    "0x%08X  %p \n",
	    z->netlong[0],
	    &z->netlong[0],
	    z->netlong[1],
	    &z->netlong[1],
	    z->netlong[2],
	    &z->netlong[2],
	    z->netlong[3],
	    &z->netlong[3],
	    z->netlong[4],
	    &z->netlong[4]);
}
#endif

boolean_t
zfsacl_get_aclentry(zfsacl_t _acl, int _idx, zfsacl_entry_t *_pentry)
{
	zfsacl_entry_t entry;

	if (!validate_entry_idx(_acl, _idx)) {
		return (B_FALSE);
	}

	entry = ACL4_GETENTRY(_acl->aclbuf, _idx);
	*_pentry = entry;
#if ZFS_DEBUG
	dump_entry(entry);
#endif
	return (B_TRUE);
}

boolean_t
zfsacl_delete_aclentry(zfsacl_t _acl, int _idx)
{
	uint_t acecnt;
	uint_t *aclacecnt = NULL;
	boolean_t ok;
	struct zfsace4 *z = ACL4BUF_TO_ACES(_acl->aclbuf);
	size_t orig_sz, after_offset;

	if (!validate_entry_idx(_acl, _idx)) {
		return (B_FALSE);
	}

	ok = zfsacl_get_acecnt(_acl, &acecnt);
	if (!ok) {
		return (B_FALSE);
	}

	if (acecnt == 1) {
		/* ACL without entries is not permitted */
		errno = ERANGE;
		return (B_FALSE);
	}

	if (((uint_t)_idx) + 1 == acecnt) {
		memset(&z[_idx], 0, ACE4_SZ);
	} else {
		orig_sz = _acl->aclbuf_size;
		after_offset = orig_sz - ACL4SZ_FROM_ACECNT(_idx) - ACE4_SZ;
		memmove(&z[_idx], &z[_idx + 1], after_offset);
	}

	aclacecnt = ACL4_GET_CNT(_acl->aclbuf);
	*aclacecnt = htonl(acecnt -1);
	return (B_TRUE);
}

#define	ZFSACE_TYPE_OFFSET	0
#define	ZFSACE_FLAGSET_OFFSET	1
#define	ZFSACE_WHOTYPE_OFFSET	2
#define	ZFSACE_PERMSET_OFFSET	3
#define	ZFSACE_WHOID_OFFSET	4
#define	ZFSACE_SPECIAL_ID	0x00000001
#define	HAS_SPECIAL_ID(who) ((who == ZFSACE_SPECIAL_ID) ? B_TRUE : B_FALSE)

boolean_t
zfsace_get_permset(zfsacl_entry_t _entry, zfsace_permset_t *_pperm)
{
	uint_t *entry = (uint_t *)_entry;
	zfsace_permset_t perm;

	perm = ntohl(*(entry + ZFSACE_PERMSET_OFFSET));
	*_pperm = perm;
	return (B_TRUE);
}

boolean_t
zfsace_get_flagset(zfsacl_entry_t _entry, zfsace_flagset_t *_pflags)
{
	uint_t *entry = (uint_t *)_entry;
	zfsace_flagset_t flags;

	flags = ntohl(*(entry + ZFSACE_FLAGSET_OFFSET));
	*_pflags = flags;
	return (B_TRUE);
}

boolean_t
zfsace_get_who(zfsacl_entry_t _entry, zfsace_who_t *pwho, zfsace_id_t *_paeid)
{
	struct zfsace4 *entry = (struct zfsace4 *)_entry;
	zfsace_who_t whotype;
	zfsace_id_t whoid;
	zfsace_flagset_t flags;
	boolean_t is_special;

	is_special =
	    HAS_SPECIAL_ID(ntohl(entry->netlong[ZFSACE_WHOTYPE_OFFSET]));

	if (is_special) {
		whotype =  ntohl(entry->netlong[ZFSACE_WHOID_OFFSET]);
		whoid = ZFSACL_UNDEFINED_ID;
	} else {
		flags = ntohl(entry->netlong[ZFSACE_FLAGSET_OFFSET]);
		if (ZFSACE_IS_GROUP(flags)) {
			whotype = ZFSACL_GROUP;
		} else {
			whotype = ZFSACL_USER;
		}
		whoid =  ntohl(entry->netlong[ZFSACE_WHOID_OFFSET]);
	}

	*pwho = whotype;
	*_paeid = whoid;
	return (B_TRUE);
}

boolean_t
zfsace_get_entry_type(zfsacl_entry_t _entry, zfsace_entry_type_t *_tp)
{
	uint_t *entry = (uint_t *)_entry;
	zfsace_entry_type_t entry_type;

	entry_type = ntohl(*(entry + ZFSACE_TYPE_OFFSET));
	*_tp = entry_type;
	return (B_TRUE);
}

boolean_t
zfsace_set_permset(zfsacl_entry_t _entry, zfsace_permset_t _perm)
{
	uint_t *pperm = (uint_t *)_entry + ZFSACE_PERMSET_OFFSET;

	if (ZFSACE_ACCESS_MASK_INVALID(_perm)) {
		errno = EINVAL;
		return (B_FALSE);
	}

	*pperm = htonl(_perm);
	return (B_TRUE);
}

boolean_t
zfsace_set_flagset(zfsacl_entry_t _entry, zfsace_flagset_t _flags)
{
	uint_t *pflags = (uint_t *)_entry + ZFSACE_FLAGSET_OFFSET;

	if (ZFSACE_FLAG_INVALID(_flags)) {
		errno = EINVAL;
		return (B_FALSE);
	}

	*pflags = htonl(_flags);
	return (B_TRUE);
}

boolean_t
zfsace_set_who(zfsacl_entry_t _entry, zfsace_who_t _whotype, zfsace_id_t _whoid)
{
	struct zfsace4 *entry = (struct zfsace4 *)_entry;
	uint_t *pspecial = &entry->netlong[ZFSACE_WHOTYPE_OFFSET];
	uint_t *pwhoid = &entry->netlong[ZFSACE_WHOID_OFFSET];
	uint_t special_flag, whoid;
	zfsace_flagset_t flags;

	flags = ntohl(entry->netlong[ZFSACE_FLAGSET_OFFSET]);

	switch (_whotype) {
	case ZFSACL_USER_OBJ:
	case ZFSACL_EVERYONE:
		whoid = _whotype;
		special_flag = ZFSACE_SPECIAL_ID;
		if (ZFSACE_IS_GROUP(flags)) {
			zfsace_set_flagset(_entry,
			    flags & ~ZFSACE_IDENTIFIER_GROUP);
		}
		break;
	case ZFSACL_GROUP_OBJ:
		whoid = _whotype;
		special_flag = ZFSACE_SPECIAL_ID;
		if (!ZFSACE_IS_GROUP(flags)) {
			zfsace_set_flagset(_entry,
			    flags | ZFSACE_IDENTIFIER_GROUP);
		}
		break;
	case ZFSACL_USER:
		if (_whoid == ZFSACL_UNDEFINED_ID) {
			errno = EINVAL;
			return (B_FALSE);
		}
		whoid = _whoid;
		special_flag = 0;
		if (ZFSACE_IS_GROUP(flags)) {
			zfsace_set_flagset(_entry,
			    flags & ~ZFSACE_IDENTIFIER_GROUP);
		}
		break;
	case ZFSACL_GROUP:
		if (_whoid == ZFSACL_UNDEFINED_ID) {
			errno = EINVAL;
			return (B_FALSE);
		}
		whoid = _whoid;
		special_flag = 0;
		if (!ZFSACE_IS_GROUP(flags)) {
			zfsace_set_flagset(_entry,
			    flags | ZFSACE_IDENTIFIER_GROUP);
		}
		break;
	default:
		errno = EINVAL;
		return (B_FALSE);
	}

	*pspecial = htonl(special_flag);
	*pwhoid = htonl(whoid);
	return (B_TRUE);
}

boolean_t
zfsace_set_entry_type(zfsacl_entry_t _entry, zfsace_entry_type_t _tp)
{
	uint_t *ptype = (uint_t *)_entry + ZFSACE_TYPE_OFFSET;

	if (ZFSACE_TYPE_INVALID(_tp)) {
		errno = EINVAL;
		return (B_FALSE);
	}

	*ptype = htonl(_tp);
	return (B_TRUE);
}

#if ZFS_DEBUG
static void
dump_xattr(uint_t *buf, size_t len)
{
	size_t i;

	fprintf(stderr, "off: 0, 0x%08x, ptr: %p | ", ntohl(buf[0]), &buf[0]);
	fprintf(stderr, "off: 1, 0x%08x, ptr: %p | ", ntohl(buf[1]), &buf[0]);

	for (i = 2; i < (len / sizeof (uint_t)); i++) {
		if (((i -2) % 5) == 0) {
			fprintf(stderr, "\n");
		}
		fprintf(stderr, "off: %ld, 0x%08x, ptr: %p\n",
		    i, ntohl(buf[i]), &buf[i]);
	}
}
#endif

zfsacl_t
zfsacl_get_fd(int fd, zfsacl_brand_t _brand)
{
	zfsacl_t out = NULL;
	ssize_t res;

	if (_brand != ZFSACL_BRAND_NFSV4) {
		errno = EINVAL;
		return (NULL);
	}

	out = zfsacl_init(ACL4_MAX_ENTRIES, _brand);
	if (out == NULL) {
		return (NULL);
	}

	res = fgetxattr(fd, ACL4_XATTR, out->aclbuf, out->aclbuf_size);
	if (res == -1) {
		zfsacl_free(&out);
		return (NULL);
	}
#if ZFS_DEBUG
	dump_xattr(out->aclbuf, out->aclbuf_size);
#endif

	return (out);
}

zfsacl_t
zfsacl_get_file(const char *_path_p, zfsacl_brand_t _brand)
{
	zfsacl_t out = NULL;
	ssize_t res;

	if (_brand != ZFSACL_BRAND_NFSV4) {
		errno = EINVAL;
		return (NULL);
	}

	out = zfsacl_init(ACL4_MAX_ENTRIES, _brand);
	if (out == NULL) {
		return (NULL);
	}

	res = getxattr(_path_p, ACL4_XATTR, out->aclbuf, out->aclbuf_size);
	if (res == -1) {
		zfsacl_free(&out);
		return (NULL);
	}
#if ZFS_DEBUG
	dump_xattr(out->aclbuf, out->aclbuf_size);
#endif

	return (out);
}

zfsacl_t
zfsacl_get_link(const char *_path_p, zfsacl_brand_t _brand)
{
	zfsacl_t out = NULL;
	ssize_t res;

	if (_brand != ZFSACL_BRAND_NFSV4) {
		errno = EINVAL;
		return (NULL);
	}

	out = zfsacl_init(ACL4_MAX_ENTRIES, _brand);
	if (out == NULL) {
		return (NULL);
	}

	res = lgetxattr(_path_p, ACL4_XATTR, out->aclbuf, out->aclbuf_size);
	if (res == -1) {
		zfsacl_free(&out);
		return (NULL);
	}

#if ZFS_DEBUG
	dump_xattr(out->aclbuf, out->aclbuf_size);
#endif
	return (out);
}

static boolean_t
xatbuf_from_acl(zfsacl_t acl, char **pbuf, size_t *bufsz)
{
	uint_t acecnt;
	size_t calculated_acl_sz;
	boolean_t ok;

	ok = zfsacl_get_acecnt(acl, &acecnt);
	if (!ok) {
		return (B_FALSE);
	}

	if (acecnt == 0) {
		errno = ENODATA;
	} else if (acecnt > ACL4_MAX_ENTRIES) {
		errno = ERANGE;
		return (B_FALSE);
	}

	calculated_acl_sz = ACL4SZ_FROM_ACECNT(acecnt);
	assert(calculated_acl_sz <= acl->aclbuf_size);

	*pbuf = (char *)acl->aclbuf;

	*bufsz = calculated_acl_sz;
	return (B_TRUE);
}

boolean_t
zfsacl_set_fd(int _fd, zfsacl_t _acl)
{
	int err;
	boolean_t ok;
	char *buf = NULL;
	size_t bufsz = 0;

	ok = xatbuf_from_acl(_acl, &buf, &bufsz);
	if (!ok) {
		return (B_FALSE);
	}

	err = fsetxattr(_fd, ACL4_XATTR, buf, bufsz, 0);
	if (err) {
		return (B_FALSE);
	}
	return (B_TRUE);
}

boolean_t
zfsacl_set_file(const char *_path_p, zfsacl_t _acl)
{
	int err;
	boolean_t ok;
	char *buf = NULL;
	size_t bufsz = 0;

	ok = xatbuf_from_acl(_acl, &buf, &bufsz);
	if (!ok) {
		return (B_FALSE);
	}

	err = setxattr(_path_p, ACL4_XATTR, buf, bufsz, 0);
	if (err) {
		return (B_FALSE);
	}
	return (B_TRUE);
}

boolean_t
zfsacl_set_link(const char *_path_p, zfsacl_t _acl)
{
	int err;
	boolean_t ok;
	char *buf = NULL;
	size_t bufsz = 0;

	ok = xatbuf_from_acl(_acl, &buf, &bufsz);
	if (!ok) {
		return (B_FALSE);
	}

	err = lsetxattr(_path_p, ACL4_XATTR, buf, bufsz, 0);
	if (err) {
		return (B_FALSE);
	}
	return (B_TRUE);
}

boolean_t
zfsacl_to_native(zfsacl_t _acl, struct native_acl *pnative)
{
	char *to_copy = NULL;
	char *out_buf = NULL;
	size_t bufsz;
	boolean_t ok;

	if (pnative == NULL) {
		errno = ENOMEM;
		return (B_FALSE);
	}

	ok = xatbuf_from_acl(_acl, &to_copy, &bufsz);
	if (!ok) {
		return (B_FALSE);
	}

	out_buf = calloc(bufsz, sizeof (char));
	if (out_buf == NULL) {
		errno = ENOMEM;
		return (B_FALSE);
	}
	memcpy(out_buf, to_copy, bufsz);
	pnative->data = out_buf;
	pnative->datalen = bufsz;
	pnative->brand = _acl->brand;
	return (B_TRUE);
}

boolean_t
zfsacl_is_trivial(zfsacl_t _acl, boolean_t *trivialp)
{
	(void) _acl;
	(void) trivialp;
	errno = EOPNOTSUPP;
	return (B_FALSE);
}

#define	MAX_ENTRY_LENGTH 512

static boolean_t
format_perms(char *str, const zfsacl_entry_t entry, size_t *off)
{
	int i, cnt = 0;
	zfsace_permset_t p;

	if (!zfsace_get_permset(entry, &p)) {
		return (B_FALSE);
	}

	for (i = 0; i < ARRAY_SIZE(aceperm2name); i++) {
		char to_set;

		if (aceperm2name[i].letter == '\0') {
			continue;
		}
		if (p & aceperm2name[i].perm) {
			to_set = aceperm2name[i].letter;
		} else {
			to_set = '-';
		}
		str[cnt] = to_set;
		cnt++;
	}

	*off += cnt;
	return (B_TRUE);
}

static boolean_t
format_flags(char *str, const zfsacl_entry_t entry, size_t *off)
{
	int i, cnt = 0;
	zfsace_flagset_t flag;

	if (!zfsace_get_flagset(entry, &flag)) {
		return (B_FALSE);
	}

	for (i = 0; i < ARRAY_SIZE(aceflag2name); i++) {
		char to_set;

		if (aceflag2name[i].letter == '\0') {
			continue;
		}
		if (flag & aceflag2name[i].flag) {
			to_set = aceflag2name[i].letter;
		} else {
			to_set = '-';
		}
		str[cnt] = to_set;
		cnt++;
	}

	*off += cnt;
	return (B_TRUE);
}

static boolean_t
format_who(char *str, size_t sz, const zfsacl_entry_t _entry, size_t *off)
{
	uid_t id;
	zfsace_who_t who;
	int cnt = 0;

	if (!zfsace_get_who(_entry, &who, &id)) {
		return (B_FALSE);
	}

	switch (who) {
	case ZFSACL_USER_OBJ:
		cnt = snprintf(str, sz, "owner@");
		break;
	case ZFSACL_GROUP_OBJ:
		cnt = snprintf(str, sz, "group@");
		break;
	case ZFSACL_EVERYONE:
		cnt = snprintf(str, sz, "everyone@");
		break;
	case ZFSACL_USER:
		cnt = snprintf(str, sz, "user:%d", id);
		break;
	case ZFSACL_GROUP:
		cnt = snprintf(str, sz, "group:%d", id);
		break;
	default:
		errno = EINVAL;
		return (B_FALSE);
	}

	if (cnt == -1) {
		return (B_FALSE);
	}

	*off += cnt;
	return (B_TRUE);
}

static boolean_t
format_entry_type(char *str, size_t sz, const zfsacl_entry_t _entry,
    size_t *off)
{
	zfsace_entry_type_t entry_type;
	int cnt = 0;

	if (!zfsace_get_entry_type(_entry, &entry_type)) {
		return (B_FALSE);
	}

	switch (entry_type) {
	case ZFSACL_ENTRY_TYPE_ALLOW:
		cnt = snprintf(str, sz, "allow");
		break;
	case ZFSACL_ENTRY_TYPE_DENY:
		cnt = snprintf(str, sz, "deny");
		break;
	case ZFSACL_ENTRY_TYPE_AUDIT:
		cnt = snprintf(str, sz, "audit");
		break;
	case ZFSACL_ENTRY_TYPE_ALARM:
		cnt = snprintf(str, sz, "alarm");
		break;
	default:
		errno = EINVAL;
		return (B_FALSE);
	}

	if (cnt == -1) {
		return (B_FALSE);
	}

	*off += cnt;
	return (B_TRUE);
}

static boolean_t
add_format_separator(char *str, size_t sz, size_t *off)
{
	int cnt;

	cnt = snprintf(str, sz, ":");
	if (cnt == -1)
		return (B_FALSE);

	*off += cnt;
	return (B_TRUE);
}

static size_t
format_entry(char *str, size_t sz, const zfsacl_entry_t _entry)
{
	size_t off = 0;
	size_t slen = 0;
	size_t tocopy = 0;
	char buf[MAX_ENTRY_LENGTH + 1] = { 0 };

	if (!format_who(buf, sizeof (buf), _entry, &off))
		return (-1);

	if (!add_format_separator(buf +off, sizeof (buf) - off, &off))
		return (-1);

	if (!format_perms(buf + off, _entry, &off))
		return (-1);

	if (!add_format_separator(buf +off, sizeof (buf) - off, &off))
		return (-1);

	if (!format_flags(buf + off, _entry, &off))
		return (-1);

	if (!add_format_separator(buf +off, sizeof (buf) - off, &off))
		return (-1);

	if (!format_entry_type(buf + off, sizeof (buf) - off, _entry, &off))
		return (-1);

	buf[off] = '\n';
	slen = strlen(buf);
	if (slen >= sz)
		tocopy = sz - 1;
	else
		tocopy = slen;
	memcpy(str, buf, tocopy);
	str[tocopy] = '\0';
	return (tocopy);
}

char *
zfsacl_to_text(zfsacl_t _acl)
{
	uint_t acecnt, i;
	char *str = NULL;
	size_t off = 0, bufsz;

	if (!zfsacl_get_acecnt(_acl, &acecnt)) {
		return (NULL);
	}

	str = calloc(acecnt, MAX_ENTRY_LENGTH);
	if (str == NULL) {
		return (NULL);
	}

	bufsz = acecnt * MAX_ENTRY_LENGTH;

	for (i = 0; i < acecnt; i++) {
		zfsacl_entry_t entry;
		size_t written;

		if (!zfsacl_get_aclentry(_acl, i, &entry)) {
			free(str);
			return (NULL);
		}

		written = format_entry(str + off, bufsz - off, entry);
		if (written == (size_t)-1) {
			free(str);
			return (NULL);
		}

		off += written;
	}

	return (str);
}
