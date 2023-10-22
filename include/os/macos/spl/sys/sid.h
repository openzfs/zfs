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

/*
 * Copyright 2010 Sun Microsystems, Inc. All rights reserved.
 * Use is subject to license terms.
 */


#ifndef _SPL_SID_H
#define	_SPL_SID_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <kern/debug.h>

#define	crgetzoneid(x)	(GLOBAL_ZONEID)

typedef struct ksiddomain {
	char		*kd_name;
} ksiddomain_t;

typedef enum ksid_index {
	KSID_USER,
	KSID_GROUP,
	KSID_OWNER,
	KSID_COUNT
} ksid_index_t;

typedef int ksid_t;

/* Should be in kidmap.h */
typedef int32_t idmap_stat;

static inline ksiddomain_t *
ksid_lookupdomain(const char *dom)
{
	ksiddomain_t *kd;
	int len = strlen(dom);

	kd = (ksiddomain_t *)kmem_zalloc(sizeof (ksiddomain_t), KM_SLEEP);
	kd->kd_name = (char *)kmem_zalloc(len + 1, KM_SLEEP);
	memcpy(kd->kd_name, dom, len);

	return (kd);
}

static inline void
ksiddomain_rele(ksiddomain_t *ksid)
{
	kmem_free(ksid->kd_name, strlen(ksid->kd_name) + 1);
	kmem_free(ksid, sizeof (ksiddomain_t));
}

#define	UID_NOBODY	65534
#define	GID_NOBODY	65534

static __inline uint_t
ksid_getid(ksid_t *ks)
{
	panic("%s has been unexpectedly called", __func__);
	return (0);
}

static __inline const char *
ksid_getdomain(ksid_t *ks)
{
	panic("%s has been unexpectedly called", __func__);
	return (0);
}

static __inline uint_t
ksid_getrid(ksid_t *ks)
{
	panic("%s has been unexpectedly called", __func__);
	return (0);
}

#define	kidmap_getsidbyuid(zone, uid, sid_prefix, rid)  (1)
#define	kidmap_getsidbygid(zone, gid, sid_prefix, rid)  (1)

#ifdef	__cplusplus
}
#endif

#endif /* _SPL_SID_H */
