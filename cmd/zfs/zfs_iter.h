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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 * Copyright 2013 Nexenta Systems, Inc. All rights reserved.
 */

#ifndef	ZFS_ITER_H
#define	ZFS_ITER_H

#ifdef	__cplusplus
extern "C" {
#endif

typedef struct zfs_sort_column {
	struct zfs_sort_column	*sc_next;
	struct zfs_sort_column	*sc_last;
	zfs_prop_t		sc_prop;
	char			*sc_user_prop;
	boolean_t		sc_reverse;
} zfs_sort_column_t;

int zfs_for_each(int, char **, int options, zfs_type_t,
    zfs_sort_column_t *, zprop_list_t **, int, zfs_iter_f, void *);
int zfs_add_sort_column(zfs_sort_column_t **, const char *, boolean_t);
void zfs_free_sort_columns(zfs_sort_column_t *);
boolean_t zfs_sort_only_by_fast(const zfs_sort_column_t *);
boolean_t zfs_list_only_by_fast(const zprop_list_t *);

#ifdef	__cplusplus
}
#endif

#endif	/* ZFS_ITER_H */
