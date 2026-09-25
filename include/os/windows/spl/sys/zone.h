/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
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

#ifndef _SPL_ZONE_H
#define	_SPL_ZONE_H

#include <sys/byteorder.h>
#include <sys/errno.h>

typedef struct cred cred_t;

#define	GLOBAL_ZONEID 0

#define	zone_dataset_visible(x, y) (1)
#define	INGLOBALZONE(z) (1)

#define	crgetzoneid(x) (GLOBAL_ZONEID)

/*
 * Operations that can be authorized via zoned_uid delegation.
 * Shared with Linux; on Windows these are defined but the check
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
 * Windows stub: zoned_uid delegation is not applicable (no user
 * namespaces). Always returns NOT_APPLICABLE so callers fall through to
 * existing permission checks.
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
 * Windows stubs: zoned_uid attach/detach require user namespaces which
 * Windows does not have. Return ENXIO (consistent with the Linux
 * fallback when CONFIG_USER_NS is not defined).
 */
static inline int
zone_dataset_attach_uid(cred_t *cred, const char *dataset, uid_t uid)
{
	(void) cred, (void) dataset, (void) uid;
	return (ENXIO);
}

static inline int
zone_dataset_detach_uid(cred_t *cred, const char *dataset, uid_t uid)
{
	(void) cred, (void) dataset, (void) uid;
	return (ENXIO);
}

/*
 * Windows stubs: no-op since zoned_uid delegation requires user
 * namespaces.
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
