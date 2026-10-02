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
 * Copyright (C) 2013 Jorgen Lundman <lundman@lundman.net>
 *
 */

#ifndef _SPL_ZONE_H
#define	_SPL_ZONE_H

#include <sys/byteorder.h>

#define	GLOBAL_ZONEID			0

#define	zone_dataset_visible(x, y)	(1)
#define	INGLOBALZONE(z)			(1)

/*
 * Operations that can be authorized via zoned_uid delegation.
 * Shared with Linux; on FreeBSD these are defined but the check
 * always returns NOT_APPLICABLE (no user namespace support).
 */
typedef enum zone_uid_op {
	ZONE_OP_CREATE,
	ZONE_OP_SNAPSHOT,
	ZONE_OP_CLONE,
	ZONE_OP_DESTROY,
	ZONE_OP_RENAME,
	ZONE_OP_SETPROP
} zone_uid_op_t;

typedef enum zone_admin_result {
	ZONE_ADMIN_NOT_APPLICABLE,
	ZONE_ADMIN_ALLOWED,
	ZONE_ADMIN_DENIED
} zone_admin_result_t;

/*
 * FreeBSD stub: zoned_uid delegation is not applicable (no user namespaces).
 * Always returns NOT_APPLICABLE so callers fall through to existing
 * jail-based permission checks.
 */
static inline zone_admin_result_t
zone_dataset_admin_check(const char *dataset, zone_uid_op_t op,
    const char *aux_dataset)
{
	(void) dataset, (void) op, (void) aux_dataset;
	return (ZONE_ADMIN_NOT_APPLICABLE);
}

/*
 * Callback type for looking up zoned_uid property.
 */
typedef uid_t (*zone_get_zoned_uid_fn_t)(const char *dataset,
    char *root_out, size_t root_size);

/*
 * FreeBSD stubs: zoned_uid attach/detach require user namespaces
 * which FreeBSD does not have.  Return ENXIO (consistent with the
 * Linux fallback when CONFIG_USER_NS is not defined).
 */
static inline int
zone_dataset_attach_uid(struct ucred *cred, const char *dataset, uid_t uid)
{
	(void) cred, (void) dataset, (void) uid;
	return (ENXIO);
}

static inline int
zone_dataset_detach_uid(struct ucred *cred, const char *dataset, uid_t uid)
{
	(void) cred, (void) dataset, (void) uid;
	return (ENXIO);
}

/*
 * FreeBSD stubs: no-op since zoned_uid delegation requires user namespaces.
 */
static inline void
zone_register_zoned_uid_callback(zone_get_zoned_uid_fn_t fn)
{
	(void) fn;
}

static inline void
zone_unregister_zoned_uid_callback(void)
{
}

#endif /* SPL_ZONE_H */
