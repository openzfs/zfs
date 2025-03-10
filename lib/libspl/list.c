// SPDX-License-Identifier: CDDL-1.0
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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*
 * Generic doubly-linked list implementation
 */

#include <sys/list.h>
#include <sys/list_impl.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/debug.h>

#define	list_d2l(a, obj) ((list_node_t *)(((char *)obj) + (a)->list_offset))
#define	list_object(a, node) ((void *)(((char *)node) - (a)->list_offset))
#define	list_empty(a) ((a)->list_head.next == &(a)->list_head)

#define	list_insert_after_node(list, node, object) {	\
	list_node_t *lnew = list_d2l(list, object);	\
	lnew->prev = (node);			\
	lnew->next = (node)->next;		\
	(node)->next->prev = lnew;		\
	(node)->next = lnew;			\
}

#define	list_insert_before_node(list, node, object) {	\
	list_node_t *lnew = list_d2l(list, object);	\
	lnew->next = (node);			\
	lnew->prev = (node)->prev;		\
	(node)->prev->next = lnew;		\
	(node)->prev = lnew;			\
}

#define	list_remove_node(node)					\
	(node)->prev->next = (node)->next;	\
	(node)->next->prev = (node)->prev;	\
	(node)->next = (node)->prev = NULL

void
list_create(list_t *list, size_t size, size_t offset)
{
	ASSERT(list);
	ASSERT(size > 0);
	ASSERT(size >= offset + sizeof (list_node_t));

	(void) size;

	list->list_offset = offset;
	list->list_head.next = list->list_head.prev = &list->list_head;
}

void
list_destroy(list_t *list)
{
	list_node_t *node = &list->list_head;

	ASSERT(list);
	ASSERT(list->list_head.next == node);
	ASSERT(list->list_head.prev == node);

	node->next = node->prev = NULL;
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
	ASSERT(lold->next != NULL);
	list_remove_node(lold);
}

void *
list_remove_head(list_t *list)
{
	list_node_t *head = list->list_head.next;
	if (head == &list->list_head)
		return (NULL);
	list_remove_node(head);
	return (list_object(list, head));
}

void *
list_remove_tail(list_t *list)
{
	list_node_t *tail = list->list_head.prev;
	if (tail == &list->list_head)
		return (NULL);
	list_remove_node(tail);
	return (list_object(list, tail));
}

void *
list_head(list_t *list)
{
	if (list_empty(list))
		return (NULL);
	return (list_object(list, list->list_head.next));
}

void *
list_tail(list_t *list)
{
	if (list_empty(list))
		return (NULL);
	return (list_object(list, list->list_head.prev));
}

void *
list_next(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->next != &list->list_head)
		return (list_object(list, node->next));

	return (NULL);
}

void *
list_prev(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->prev != &list->list_head)
		return (list_object(list, node->prev));

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

	ASSERT(dst->list_offset == src->list_offset);

	if (list_empty(src))
		return;

	dstnode->prev->next = srcnode->next;
	srcnode->next->prev = dstnode->prev;
	dstnode->prev = srcnode->prev;
	srcnode->prev->next = dstnode;

	/* empty src list */
	srcnode->next = srcnode->prev = srcnode;
}

void
list_link_replace(list_node_t *lold, list_node_t *lnew)
{
	ASSERT(list_link_active(lold));
	ASSERT(!list_link_active(lnew));

	lnew->next = lold->next;
	lnew->prev = lold->prev;
	lold->prev->next = lnew;
	lold->next->prev = lnew;
	lold->next = lold->prev = NULL;
}

void
list_link_init(list_node_t *ln)
{
	ln->next = NULL;
	ln->prev = NULL;
}

int
list_link_active(list_node_t *ln)
{
	EQUIV(ln->next == NULL, ln->prev == NULL);
	return (ln->next != NULL);
}

int
list_is_empty(list_t *list)
{
	return (list_empty(list));
}
