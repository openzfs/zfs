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

#include	<sys/objlist.h>
#include	<sys/zfs_context.h>

objlist_t *
objlist_create(void)
{
	objlist_t *list = kmem_alloc(sizeof (*list), KM_SLEEP);
	list_create(&list->ol_list, sizeof (objlist_node_t),
	    offsetof(objlist_node_t, on_node));
	list->ol_last_lookup = 0;
	return (list);
}

void
objlist_destroy(objlist_t *list)
{
	for (objlist_node_t *n = list_remove_head(&list->ol_list);
	    n != NULL; n = list_remove_head(&list->ol_list)) {
		kmem_free(n, sizeof (*n));
	}
	list_destroy(&list->ol_list);
	kmem_free(list, sizeof (*list));
}

/*
 * This function looks through the objlist to see if the specified object number
 * is contained in the objlist.  In the process, it will remove all object
 * numbers in the list that are smaller than the specified object number.  Thus,
 * any lookup of an object number smaller than a previously looked up object
 * number will always return false; therefore, all lookups should be done in
 * ascending order.
 */
boolean_t
objlist_exists(objlist_t *list, uint64_t object)
{
	objlist_node_t *node = list_head(&list->ol_list);
	ASSERT3U(object, >=, list->ol_last_lookup);
	list->ol_last_lookup = object;
	while (node != NULL && node->on_object < object) {
		VERIFY3P(node, ==, list_remove_head(&list->ol_list));
		kmem_free(node, sizeof (*node));
		node = list_head(&list->ol_list);
	}
	return (node != NULL && node->on_object == object);
}

/*
 * The objlist is a list of object numbers stored in ascending order.  However,
 * the insertion of new object numbers does not seek out the correct location to
 * store a new object number; instead, it appends it to the list for simplicity.
 * Thus, any users must take care to only insert new object numbers in ascending
 * order.
 */
void
objlist_insert(objlist_t *list, uint64_t object)
{
	objlist_node_t *node = kmem_zalloc(sizeof (*node), KM_SLEEP);
	node->on_object = object;
#ifdef ZFS_DEBUG
	objlist_node_t *last_object = list_tail(&list->ol_list);
	uint64_t last_objnum = (last_object != NULL ? last_object->on_object :
	    0);
	ASSERT3U(node->on_object, >, last_objnum);
#endif
	list_insert_tail(&list->ol_list, node);
}
