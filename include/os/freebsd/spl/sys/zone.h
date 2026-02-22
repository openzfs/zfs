// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_ZONE_H_
#define	_OPENSOLARIS_SYS_ZONE_H_

#include <sys/jail.h>

/*
 * Macros to help with zone visibility restrictions.
 */

#define	GLOBAL_ZONEID	0

/*
 * Is proc in the global zone?
 */
#define	INGLOBALZONE(proc)	(!jailed((proc)->p_ucred))

/*
 * Attach the given dataset to the given jail.
 */
extern int zone_dataset_attach(struct ucred *, const char *, int);

/*
 * Detach the given dataset to the given jail.
 */
extern int zone_dataset_detach(struct ucred *, const char *, int);

/*
 * Returns true if the named pool/dataset is visible in the current zone.
 */
extern int zone_dataset_visible(const char *, int *);

/*
 * Safely get the hostid of the specified zone (defaults to machine's hostid
 * if the specified zone doesn't emulate a hostid).  Passing NULL retrieves
 * the global zone's (i.e., physical system's) hostid.
 */
extern uint32_t zone_get_hostid(void *);

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

#endif	/* !_OPENSOLARIS_SYS_ZONE_H_ */
