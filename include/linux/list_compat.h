#ifndef _SPL_LIST_COMPAT_H
#define _SPL_LIST_COMPAT_H

#include <linux/list.h>

#ifndef list_for_each_entry_safe_reverse

/**
 * list_for_each_entry_safe_reverse
 * @pos:        the type * to use as a loop cursor.
 * @n:          another type * to use as temporary storage
 * @head:       the head for your list.
 * @member:     the name of the list_struct within the struct.
 *
 * Iterate backwards over list of given type, safe against removal
 * of list entry.
 */
#define list_for_each_entry_safe_reverse(pos, n, head, member)          \
        for (pos = list_entry((head)->prev, typeof(*pos), member),      \
                n = list_entry(pos->member.prev, typeof(*pos), member); \
             &pos->member != (head);                                    \
             pos = n, n = list_entry(n->member.prev, typeof(*n), member))

#endif /* list_for_each_entry_safe_reverse */

#endif /* SPL_LIST_COMPAT_H */

