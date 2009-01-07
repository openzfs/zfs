#ifndef _SPL_LIST_H
#define _SPL_LIST_H

#include <sys/types.h>
#include <linux/list.h>

/* NOTE: We have implemented the Solaris list API in terms of the native
 * linux API.  This has certain advantages in terms of leveraging the linux
 * list debugging infrastructure, but it also means that the internals of a
 * list differ slightly than on Solaris.  This is not a problem as long as
 * all callers stick to the published API.  The two major differences are:
 *
 * 1) A list_node_t is mapped to a linux list_head struct which changes
 *    the name of the list_next/list_prev pointers to next/prev respectively.
 *
 * 2) A list_node_t which is not attached to a list on Solaris is denoted
 *    by having its list_next/list_prev pointers set to NULL.  Under linux
 *    the next/prev pointers are set to LIST_POISON1 and LIST_POISON2
 *    respectively.  At this moment this only impacts the implementation
 *    of the list_link_init() and list_link_active() functions.
 */

typedef struct list_head list_node_t;

typedef struct list {
	size_t list_size;
	size_t list_offset;
	list_node_t list_head;
} list_t;

#define list_d2l(a, obj) ((list_node_t *)(((char *)obj) + (a)->list_offset))
#define list_object(a, node) ((void *)(((char *)node) - (a)->list_offset))

static inline int
list_is_empty(list_t *list)
{
	return list_empty(&list->list_head);
}

static inline void
list_link_init(list_node_t *node)
{
	node->next = LIST_POISON1;
	node->prev = LIST_POISON2;
}

static inline void
list_create(list_t *list, size_t size, size_t offset)
{
	ASSERT(list);
	ASSERT(size > 0);
	ASSERT(size >= offset + sizeof(list_node_t));

	list->list_size = size;
	list->list_offset = offset;
	INIT_LIST_HEAD(&list->list_head);
}

static inline void
list_destroy(list_t *list)
{
	ASSERT(list);
	ASSERT(list_is_empty(list));

	list_del(&list->list_head);
}

static inline void
list_insert_head(list_t *list, void *object)
{
	list_add(list_d2l(list, object), &list->list_head);
}

static inline void
list_insert_tail(list_t *list, void *object)
{
	list_add_tail(list_d2l(list, object), &list->list_head);
}

static inline void
list_insert_after(list_t *list, void *object, void *nobject)
{
	if (object == NULL)
		list_insert_head(list, nobject);
	else
		list_add(list_d2l(list, nobject), list_d2l(list, object));
}

static inline void
list_insert_before(list_t *list, void *object, void *nobject)
{
	if (object == NULL)
		list_insert_tail(list, nobject);
	else
		list_add_tail(list_d2l(list, nobject), list_d2l(list, object));
}

static inline void
list_remove(list_t *list, void *object)
{
	ASSERT(!list_is_empty(list));
	list_del(list_d2l(list, object));
}

static inline void *
list_remove_head(list_t *list)
{
	list_node_t *head = list->list_head.next;
	if (head == &list->list_head)
		return NULL;

	list_del(head);
	return list_object(list, head);
}

static inline void *
list_remove_tail(list_t *list)
{
	list_node_t *tail = list->list_head.prev;
	if (tail == &list->list_head)
		return NULL;

	list_del(tail);
	return list_object(list, tail);
}

static inline void *
list_head(list_t *list)
{
	if (list_is_empty(list))
		return NULL;

	return list_object(list, list->list_head.next);
}

static inline void *
list_tail(list_t *list)
{
	if (list_is_empty(list))
		return NULL;

	return list_object(list, list->list_head.prev);
}

static inline void *
list_next(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->next != &list->list_head)
		return list_object(list, node->next);

	return NULL;
}

static inline void *
list_prev(list_t *list, void *object)
{
	list_node_t *node = list_d2l(list, object);

	if (node->prev != &list->list_head)
		return list_object(list, node->prev);

	return NULL;
}

static inline int
list_link_active(list_node_t *node)
{
        return (node->next != LIST_POISON1) && (node->prev != LIST_POISON2);
}

#endif /* SPL_LIST_H */
