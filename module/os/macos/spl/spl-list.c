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
 * Copyright 2007 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Generic doubly-linked list implementation
 */

#include <sys/list.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>


#define	list_insert_after_node(list, node, object) {	\
	list_node_t *lnew = list_d2l(list, object);	\
	lnew->list_prev = node;				\
	lnew->list_next = node->list_next;		\
	node->list_next->list_prev = lnew;		\
	node->list_next = lnew;				\
}

#define	list_insert_before_node(list, node, object) {	\
	list_node_t *lnew = list_d2l(list, object);	\
	lnew->list_next = node;				\
	lnew->list_prev = node->list_prev;		\
	node->list_prev->list_next = lnew;		\
	node->list_prev = lnew;				\
}

void
list_create(list_t *list, size_t size, size_t offset)
{
	ASSERT(list);
	ASSERT(size > 0);
	ASSERT(size >= offset + sizeof (list_node_t));

	list->list_size = size;
	list->list_offset = offset;
	list->list_head.list_next = list->list_head.list_prev =
	    &list->list_head;
}

void
list_destroy(list_t *list)
{
	list_node_t *node = &list->list_head;

	ASSERT(list);
	ASSERT(list->list_head.list_next == node);
	ASSERT(list->list_head.list_prev == node);

	node->list_next = node->list_prev = NULL;
}

void
list_insert_after(list_t *list, void *object, void *nobject)
{
	if (object == NULL) {
		list_insert_head(list, nobject);
	} else {
		list_node_t *lold = list_d2l(list, object);
		list_insert_after_node(list, lold, nobject);
	}
}

void
list_insert_before(list_t *list, void *object, void *nobject)
{
	if (object == NULL) {
		list_insert_tail(list, nobject);
	} else {
		list_node_t *lold = list_d2l(list, object);
		list_insert_before_node(list, lold, nobject);
	}
}

void
list_insert_head(list_t *list, void *object)
{
	list_node_t *lold = &list->list_head;
	list_insert_after_node(list, lold, object);
}

void
list_insert_tail(list_t *list, void *object)
{
	list_node_t *lold = &list->list_head;
	list_insert_before_node(list, lold, object);
}

void
list_remove(list_t *list, void *object)
{
	list_node_t *lold = list_d2l(list, object);
	ASSERT(!list_empty(list));
	ASSERT(lold->list_next != NULL);
	lold->list_prev->list_next = lold->list_next;
	lold->list_next->list_prev = lold->list_prev;
	lold->list_next = lold->list_prev = NULL;
}


void *
list_head(list_t *list)
{
	if (list_empty(list))
		return (NULL);
	return (list_object(list, list->list_head.list_next));
}

void *
list_tail(list_t *list)
{
	if (list_empty(list))
		return (NULL);
	return (list_object(list, list->list_head.list_prev));
}

void *
list_next(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->list_next != &list->list_head)
		return (list_object(list, node->list_next));

	return (NULL);
}

void *
list_prev(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->list_prev != &list->list_head)
		return (list_object(list, node->list_prev));

	return (NULL);
}

/*
 *  Insert src list after dst list. Empty src list thereafter.
 */
void
list_move_tail(list_t *dst, list_t *src)
{
	list_node_t *dstnode = &dst->list_head;
	list_node_t *srcnode = &src->list_head;

	ASSERT(dst->list_size == src->list_size);
	ASSERT(dst->list_offset == src->list_offset);

	if (list_empty(src))
		return;

	dstnode->list_prev->list_next = srcnode->list_next;
	srcnode->list_next->list_prev = dstnode->list_prev;
	dstnode->list_prev = srcnode->list_prev;
	srcnode->list_prev->list_next = dstnode;

	/* empty src list */
	srcnode->list_next = srcnode->list_prev = srcnode;
}

int
list_link_active(list_node_t *link)
{
	return (link->list_next != NULL);
}

int
list_is_empty(list_t *list)
{
	return (list_empty(list));
}
