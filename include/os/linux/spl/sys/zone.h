// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _SPL_ZONE_H
#define	_SPL_ZONE_H

#include <sys/byteorder.h>
#include <sys/cred.h>

#include <linux/cred.h>
#include <linux/user_namespace.h>

/*
 * Attach the given dataset to the given user namespace.
 */
extern int zone_dataset_attach(cred_t *, const char *, int);

/*
 * Detach the given dataset from the given user namespace.
 */
extern int zone_dataset_detach(cred_t *, const char *, int);

/*
 * Attach the given dataset to all user namespaces owned by the given UID.
 */
extern int zone_dataset_attach_uid(cred_t *, const char *, uid_t);

/*
 * Detach the given dataset from UID-based zoning.
 */
extern int zone_dataset_detach_uid(cred_t *, const char *, uid_t);

/*
 * Returns true if the named pool/dataset is visible in the current zone.
 */
extern int zone_dataset_visible(const char *dataset, int *write);

/*
 * Operations that can be authorized via zoned_uid delegation.
 * Used by zone_dataset_admin_check() to apply operation-specific constraints.
 */
typedef enum zone_uid_op {
	ZONE_OP_CREATE,		/* Create child dataset */
	ZONE_OP_SNAPSHOT,	/* Create snapshot */
	ZONE_OP_CLONE,		/* Clone from snapshot */
	ZONE_OP_DESTROY,	/* Destroy dataset/snapshot */
	ZONE_OP_RENAME,		/* Rename (both src and dst checked) */
	ZONE_OP_SETPROP		/* Set properties */
} zone_uid_op_t;

/*
 * Result of admin authorization check for zoned_uid delegation.
 */
typedef enum zone_admin_result {
	ZONE_ADMIN_NOT_APPLICABLE,	/* In global zone, use normal checks */
	ZONE_ADMIN_ALLOWED,		/* Authorized via zoned_uid */
	ZONE_ADMIN_DENIED		/* In user ns but not authorized */
} zone_admin_result_t;

/*
 * Check if a dataset operation is authorized via zoned_uid delegation.
 * For ZONE_OP_RENAME and ZONE_OP_CLONE, aux_dataset provides the
 * second dataset (destination for rename, origin for clone).
 * Returns ZONE_ADMIN_ALLOWED if authorized, ZONE_ADMIN_DENIED if in a
 * user namespace but not authorized, or ZONE_ADMIN_NOT_APPLICABLE if
 * in the global zone (caller should use normal permission checks).
 */
extern zone_admin_result_t zone_dataset_admin_check(const char *dataset,
    zone_uid_op_t op, const char *aux_dataset);

/*
 * Callback type for looking up zoned_uid property.
 * Returns the zoned_uid value if found, 0 if not set or on error.
 * If root_out is non-NULL, copies the delegation root dataset name.
 */
typedef uid_t (*zone_get_zoned_uid_fn_t)(const char *dataset,
    char *root_out, size_t root_size);

/*
 * Register/unregister the zoned_uid property lookup callback.
 * Called by ZFS module during init/fini.
 */
extern void zone_register_zoned_uid_callback(zone_get_zoned_uid_fn_t fn);
extern void zone_unregister_zoned_uid_callback(void);

int spl_zone_init(void);
void spl_zone_fini(void);

extern unsigned int crgetzoneid(const cred_t *);
extern unsigned int global_zoneid(void);
extern boolean_t inglobalzone(proc_t *);

#define	INGLOBALZONE(x) inglobalzone(x)
#define	GLOBAL_ZONEID	global_zoneid()

#endif /* SPL_ZONE_H */
