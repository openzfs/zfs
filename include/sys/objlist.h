/*
 * CDDL HEADER START
 *
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * http://www.illumos.org/license/CDDL.
 *
 * CDDL HEADER END
 */
/*
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#ifndef	_OBJLIST_H
#define	_OBJLIST_H

#ifdef	__cplusplus
extern "C" {
#endif

#include	<sys/zfs_context.h>

typedef struct objlist_node {
	list_node_t	on_node;
	uint64_t	on_object;
} objlist_node_t;

typedef struct objlist {
	list_t		ol_list; /* List of struct objnode. */
	/*
	 * Last object looked up. Used to assert that objects are being looked
	 * up in ascending order.
	 */
	uint64_t	ol_last_lookup;
} objlist_t;

objlist_t *objlist_create(void);
void objlist_destroy(objlist_t *);
boolean_t objlist_exists(objlist_t *, uint64_t);
void objlist_insert(objlist_t *, uint64_t);

#ifdef	__cplusplus
}
#endif

#endif	/* _OBJLIST_H */
