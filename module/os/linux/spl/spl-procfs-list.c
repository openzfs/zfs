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
 * Copyright (c) 2018 by Delphix. All rights reserved.
 */

#include <sys/list.h>
#include <sys/mutex.h>
#include <sys/procfs_list.h>
#include <linux/proc_fs.h>

/*
 * A procfs_list is a wrapper around a linked list which implements the seq_file
 * interface, allowing the contents of the list to be exposed through procfs.
 * The kernel already has some utilities to help implement the seq_file
 * interface for linked lists (seq_list_*), but they aren't appropriate for use
 * with lists that have many entries, because seq_list_start walks the list at
 * the start of each read syscall to find where it left off, so reading a file
 * ends up being quadratic in the number of entries in the list.
 *
 * This implementation avoids this penalty by maintaining a separate cursor into
 * the list per instance of the file that is open. It also maintains some extra
 * information in each node of the list to prevent reads of entries that have
 * been dropped from the list.
 *
 * Callers should only add elements to the list using procfs_list_add, which
 * adds an element to the tail of the list. Other operations can be performed
 * directly on the wrapped list using the normal list manipulation functions,
 * but elements should only be removed from the head of the list.
 */

#define	NODE_ID(procfs_list, obj) \
		(((procfs_list_node_t *)(((char *)obj) + \
		(procfs_list)->pl_node_offset))->pln_id)

typedef struct procfs_list_cursor {
	procfs_list_t	*procfs_list;	/* List into which this cursor points */
	void		*cached_node;	/* Most recently accessed node */
	loff_t		cached_pos;	/* Position of cached_node */
} procfs_list_cursor_t;

static int
procfs_list_seq_show(struct seq_file *f, void *p)
{
	procfs_list_cursor_t *cursor = f->private;
	procfs_list_t *procfs_list = cursor->procfs_list;

	ASSERT(MUTEX_HELD(&procfs_list->pl_lock));
	if (p == SEQ_START_TOKEN) {
		if (procfs_list->pl_show_header != NULL)
			return (procfs_list->pl_show_header(f));
		else
			return (0);
	}
	return (procfs_list->pl_show(f, p));
}

static void *
procfs_list_next_node(procfs_list_cursor_t *cursor, loff_t *pos)
{
	void *next_node;
	procfs_list_t *procfs_list = cursor->procfs_list;

	if (cursor->cached_node == SEQ_START_TOKEN)
		next_node = list_head(&procfs_list->pl_list);
	else
		next_node = list_next(&procfs_list->pl_list,
		    cursor->cached_node);

	if (next_node != NULL) {
		cursor->cached_node = next_node;
		cursor->cached_pos = NODE_ID(procfs_list, cursor->cached_node);
		*pos = cursor->cached_pos;
	} else {
		/*
		 * seq_read() expects ->next() to update the position even
		 * when there are no more entries. Advance the position to
		 * prevent a warning from being logged.
		 */
		cursor->cached_node = NULL;
		cursor->cached_pos++;
		*pos = cursor->cached_pos;
	}

	return (next_node);
}

static void *
procfs_list_seq_start(struct seq_file *f, loff_t *pos)
{
	procfs_list_cursor_t *cursor = f->private;
	procfs_list_t *procfs_list = cursor->procfs_list;

	mutex_enter(&procfs_list->pl_lock);

	if (*pos == 0) {
		cursor->cached_node = SEQ_START_TOKEN;
		cursor->cached_pos = 0;
		return (SEQ_START_TOKEN);
	} else if (cursor->cached_node == NULL) {
		return (NULL);
	}

	/*
	 * Check if our cached pointer has become stale, which happens if the
	 * the message where we left off has been dropped from the list since
	 * the last read syscall completed.
	 */
	void *oldest_node = list_head(&procfs_list->pl_list);
	if (cursor->cached_node != SEQ_START_TOKEN && (oldest_node == NULL ||
	    NODE_ID(procfs_list, oldest_node) > cursor->cached_pos))
		return (ERR_PTR(-EIO));

	/*
	 * If it isn't starting from the beginning of the file, the seq_file
	 * code will either pick up at the same position it visited last or the
	 * following one.
	 */
	if (*pos == cursor->cached_pos) {
		return (cursor->cached_node);
	} else {
		ASSERT3U(*pos, ==, cursor->cached_pos + 1);
		return (procfs_list_next_node(cursor, pos));
	}
}

static void *
procfs_list_seq_next(struct seq_file *f, void *p, loff_t *pos)
{
	procfs_list_cursor_t *cursor = f->private;
	ASSERT(MUTEX_HELD(&cursor->procfs_list->pl_lock));
	return (procfs_list_next_node(cursor, pos));
}

static void
procfs_list_seq_stop(struct seq_file *f, void *p)
{
	procfs_list_cursor_t *cursor = f->private;
	procfs_list_t *procfs_list = cursor->procfs_list;
	mutex_exit(&procfs_list->pl_lock);
}

static const struct seq_operations procfs_list_seq_ops = {
	.show  = procfs_list_seq_show,
	.start = procfs_list_seq_start,
	.next  = procfs_list_seq_next,
	.stop  = procfs_list_seq_stop,
};

static int
procfs_list_open(struct inode *inode, struct file *filp)
{
	int rc = seq_open_private(filp, &procfs_list_seq_ops,
	    sizeof (procfs_list_cursor_t));
	if (rc != 0)
		return (rc);

	struct seq_file *f = filp->private_data;
	procfs_list_cursor_t *cursor = f->private;
	cursor->procfs_list = PDE_DATA(inode);
	cursor->cached_node = NULL;
	cursor->cached_pos = 0;

	return (0);
}

static ssize_t
procfs_list_write(struct file *filp, const char __user *buf, size_t len,
    loff_t *ppos)
{
	struct seq_file *f = filp->private_data;
	procfs_list_cursor_t *cursor = f->private;
	procfs_list_t *procfs_list = cursor->procfs_list;
	int rc;

	if (procfs_list->pl_clear != NULL &&
	    (rc = procfs_list->pl_clear(procfs_list)) != 0)
		return (-rc);
	return (len);
}

static const kstat_proc_op_t procfs_list_operations = {
#ifdef HAVE_PROC_OPS_STRUCT
	.proc_open	= procfs_list_open,
	.proc_write	= procfs_list_write,
	.proc_read	= seq_read,
	.proc_lseek	= seq_lseek,
	.proc_release	= seq_release_private,
#else
	.open		= procfs_list_open,
	.write		= procfs_list_write,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release_private,
#endif
};

/*
 * Initialize a procfs_list and create a file for it in the proc filesystem
 * under the kstat namespace.
 */
void
procfs_list_install(const char *module,
    const char *submodule,
    const char *name,
    mode_t mode,
    procfs_list_t *procfs_list,
    int (*show)(struct seq_file *f, void *p),
    int (*show_header)(struct seq_file *f),
    int (*clear)(procfs_list_t *procfs_list),
    size_t procfs_list_node_off)
{
	char *modulestr;

	if (submodule != NULL)
		modulestr = kmem_asprintf("%s/%s", module, submodule);
	else
		modulestr = kmem_asprintf("%s", module);
	mutex_init(&procfs_list->pl_lock, NULL, MUTEX_DEFAULT, NULL);
	list_create(&procfs_list->pl_list,
	    procfs_list_node_off + sizeof (procfs_list_node_t),
	    procfs_list_node_off + offsetof(procfs_list_node_t, pln_link));
	procfs_list->pl_next_id = 1; /* Save id 0 for SEQ_START_TOKEN */
	procfs_list->pl_show = show;
	procfs_list->pl_show_header = show_header;
	procfs_list->pl_clear = clear;
	procfs_list->pl_node_offset = procfs_list_node_off;

	kstat_proc_entry_init(&procfs_list->pl_kstat_entry, modulestr, name);
	kstat_proc_entry_install(&procfs_list->pl_kstat_entry, mode,
	    &procfs_list_operations, procfs_list);
	kmem_strfree(modulestr);
}
EXPORT_SYMBOL(procfs_list_install);

/* Remove the proc filesystem file corresponding to the given list */
void
procfs_list_uninstall(procfs_list_t *procfs_list)
{
	kstat_proc_entry_delete(&procfs_list->pl_kstat_entry);
}
EXPORT_SYMBOL(procfs_list_uninstall);

void
procfs_list_destroy(procfs_list_t *procfs_list)
{
	ASSERT(list_is_empty(&procfs_list->pl_list));
	list_destroy(&procfs_list->pl_list);
	mutex_destroy(&procfs_list->pl_lock);
}
EXPORT_SYMBOL(procfs_list_destroy);

/*
 * Add a new node to the tail of the list. While the standard list manipulation
 * functions can be use for all other operation, adding elements to the list
 * should only be done using this helper so that the id of the new node is set
 * correctly.
 */
void
procfs_list_add(procfs_list_t *procfs_list, void *p)
{
	ASSERT(MUTEX_HELD(&procfs_list->pl_lock));
	NODE_ID(procfs_list, p) = procfs_list->pl_next_id++;
	list_insert_tail(&procfs_list->pl_list, p);
}
EXPORT_SYMBOL(procfs_list_add);
