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

#include <sys/types.h>
#include <sys/kmem.h>
#include <sys/sunddi.h>
#include <sys/cmn_err.h>
#include <miscfs/devfs/devfs.h>


/*
 * Allocate a set of pointers to 'n_items' objects of size 'size'
 * bytes.  Each pointer is initialized to nil.
 *
 * The 'size' and 'n_items' values are stashed in the opaque
 * handle returned to the caller.
 *
 * This implementation interprets 'set of pointers' to mean 'array
 * of pointers' but note that nothing in the interface definition
 * precludes an implementation that uses, for example, a linked list.
 * However there should be a small efficiency gain from using an array
 * at lookup time.
 *
 * NOTE	As an optimization, we make our growable array allocations in
 *	powers of two (bytes), since that's how much kmem_alloc (currently)
 *	gives us anyway.  It should save us some free/realloc's ..
 *
 *	As a further optimization, we make the growable array start out
 *	with MIN_N_ITEMS in it.
 */


int
ddi_soft_state_init(void **state_p, size_t size, size_t n_items)
{
	struct i_ddi_soft_state *ss;

	if (state_p == NULL || *state_p != NULL || size == 0)
		return (EINVAL);

	ss = kmem_zalloc(sizeof (*ss), KM_SLEEP);
	mutex_init(&ss->lock, NULL, MUTEX_DRIVER, NULL);
	ss->size = size;

	if (n_items < MIN_N_ITEMS)
		ss->n_items = MIN_N_ITEMS;
	else {
		int bitlog;

		if ((bitlog = ddi_fls(n_items)) == ddi_ffs(n_items))
			bitlog--;
		ss->n_items = 1 << bitlog;
	}

	ASSERT(ss->n_items >= n_items);

	ss->array = kmem_zalloc(ss->n_items * sizeof (void *), KM_SLEEP);

	*state_p = ss;

	return (0);
}


/*
 * Allocate a state structure of size 'size' to be associated
 * with item 'item'.
 *
 * In this implementation, the array is extended to
 * allow the requested offset, if needed.
 */
int
ddi_soft_state_zalloc(void *state, int item)
{
	struct i_ddi_soft_state *ss;
	void **array;
	void *new_element;

	if ((ss = state) == NULL || item < 0)
		return (DDI_FAILURE);

	mutex_enter(&ss->lock);
	if (ss->size == 0) {
		mutex_exit(&ss->lock);
		cmn_err(CE_WARN, "ddi_soft_state_zalloc: bad handle");
		return (DDI_FAILURE);
	}

	array = ss->array;	/* NULL if ss->n_items == 0 */
	ASSERT(ss->n_items != 0 && array != NULL);

	/*
	 * refuse to tread on an existing element
	 */
	if (item < ss->n_items && array[item] != NULL) {
		mutex_exit(&ss->lock);
		return (DDI_FAILURE);
	}

	/*
	 * Allocate a new element to plug in
	 */
	new_element = kmem_zalloc(ss->size, KM_SLEEP);

	/*
	 * Check if the array is big enough, if not, grow it.
	 */
	if (item >= ss->n_items) {
		void	**new_array;
		size_t	new_n_items;
		struct i_ddi_soft_state *dirty;

		/*
		 * Allocate a new array of the right length, copy
		 * all the old pointers to the new array, then
		 * if it exists at all, put the old array on the
		 * dirty list.
		 *
		 * Note that we can't kmem_free() the old array.
		 *
		 * Why -- well the 'get' operation is 'mutex-free', so we
		 * can't easily catch a suspended thread that is just about
		 * to dereference the array we just grew out of.  So we
		 * cons up a header and put it on a list of 'dirty'
		 * pointer arrays.  (Dirty in the sense that there may
		 * be suspended threads somewhere that are in the middle
		 * of referencing them).  Fortunately, we -can- garbage
		 * collect it all at ddi_soft_state_fini time.
		 */
		new_n_items = ss->n_items;
		while (new_n_items < (1 + item))
			new_n_items <<= 1;	/* double array size .. */

		ASSERT(new_n_items >= (1 + item));	/* sanity check! */

		new_array = kmem_zalloc(new_n_items * sizeof (void *),
		    KM_SLEEP);
		/*
		 * Copy the pointers into the new array
		 */
		memcpy(new_array, array, ss->n_items * sizeof (void *));

		/*
		 * Save the old array on the dirty list
		 */
		dirty = kmem_zalloc(sizeof (*dirty), KM_SLEEP);
		dirty->array = ss->array;
		dirty->n_items = ss->n_items;
		dirty->next = ss->next;
		ss->next = dirty;

		ss->array = (array = new_array);
		ss->n_items = new_n_items;
	}

	ASSERT(array != NULL && item < ss->n_items && array[item] == NULL);

	array[item] = new_element;

	mutex_exit(&ss->lock);
	return (DDI_SUCCESS);
}


/*
 * Fetch a pointer to the allocated soft state structure.
 *
 * This is designed to be cheap.
 *
 * There's an argument that there should be more checking for
 * nil pointers and out of bounds on the array.. but we do a lot
 * of that in the alloc/free routines.
 *
 * An array has the convenience that we don't need to lock read-access
 * to it c.f. a linked list.  However our "expanding array" strategy
 * means that we should hold a readers lock on the i_ddi_soft_state
 * structure.
 *
 * However, from a performance viewpoint, we need to do it without
 * any locks at all -- this also makes it a leaf routine.  The algorithm
 * is 'lock-free' because we only discard the pointer arrays at
 * ddi_soft_state_fini() time.
 */
void *
ddi_get_soft_state(void *state, int item)
{
	struct i_ddi_soft_state *ss = state;

	ASSERT(ss != NULL && item >= 0);

	if (item < ss->n_items && ss->array != NULL)
		return (ss->array[item]);
	return (NULL);
}

/*
 * Free the state structure corresponding to 'item.'   Freeing an
 * element that has either gone or was never allocated is not
 * considered an error.  Note that we free the state structure, but
 * we don't shrink our pointer array, or discard 'dirty' arrays,
 * since even a few pointers don't really waste too much memory.
 *
 * Passing an item number that is out of bounds, or a null pointer will
 * provoke an error message.
 */
void
ddi_soft_state_free(void *state, int item)
{
	struct i_ddi_soft_state *ss;
	void **array;
	void *element;
	static char msg[] = "ddi_soft_state_free:";

	if ((ss = state) == NULL) {
		cmn_err(CE_WARN, "%s null handle",
		    msg);
		return;
	}

	element = NULL;

	mutex_enter(&ss->lock);

	if ((array = ss->array) == NULL || ss->size == 0) {
		cmn_err(CE_WARN, "%s bad handle",
		    msg);
	} else if (item < 0 || item >= ss->n_items) {
		cmn_err(CE_WARN, "%s item %d not in range [0..%lu]",
		    msg, item, ss->n_items - 1);
	} else if (array[item] != NULL) {
		element = array[item];
		array[item] = NULL;
	}

	mutex_exit(&ss->lock);

	if (element)
		kmem_free(element, ss->size);
}


/*
 * Free the entire set of pointers, and any
 * soft state structures contained therein.
 *
 * Note that we don't grab the ss->lock mutex, even though
 * we're inspecting the various fields of the data structure.
 *
 * There is an implicit assumption that this routine will
 * never run concurrently with any of the above on this
 * particular state structure i.e. by the time the driver
 * calls this routine, there should be no other threads
 * running in the driver.
 */
void
ddi_soft_state_fini(void **state_p)
{
	struct i_ddi_soft_state *ss, *dirty;
	int item;
	static char msg[] = "ddi_soft_state_fini:";

	if (state_p == NULL || (ss = *state_p) == NULL)
		return;

	if (ss->size == 0) {
		cmn_err(CE_WARN, "%s bad handle",
		    msg);
		return;
	}

	if (ss->n_items > 0) {
		for (item = 0; item < ss->n_items; item++)
			ddi_soft_state_free(ss, item);
		kmem_free(ss->array, ss->n_items * sizeof (void *));
	}

	/*
	 * Now delete any dirty arrays from previous 'grow' operations
	 */
	for (dirty = ss->next; dirty; dirty = ss->next) {
		ss->next = dirty->next;
		kmem_free(dirty->array, dirty->n_items * sizeof (void *));
		kmem_free(dirty, sizeof (*dirty));
	}

	mutex_destroy(&ss->lock);
	kmem_free(ss, sizeof (*ss));

	*state_p = NULL;
}

int
ddi_create_minor_node(dev_info_t *dip, char *name, int spec_type,
    minor_t minor_num, char *node_type, int flag)
{
	dev_t dev;
	int error = 0;
	char *r, *dup;

	dev = makedev(flag, minor_num);
	dip->dev = dev;

	/*
	 * http://lists.apple.com/archives/darwin-kernel/2007/Nov/msg00038.html
	 *
	 * devfs_make_name() has an off-by-one error when using directories
	 * and it appears Apple does not want to fix it.
	 *
	 * We then change "/" to "_" and create more Apple-like /dev names
	 *
	 */
	MALLOC(dup, char *, strlen(name)+1, M_TEMP, M_WAITOK);
	if (dup == NULL)
		return (ENOMEM);
	memcpy(dup, name, strlen(name));
	dup[strlen(name)] = '\0';

	for (r = dup;
	    (r = strchr(r, '/'));
	    *r = '_')
		/* empty */;

	dip->devc = NULL;
	dip->devb = NULL;

	if (spec_type == S_IFCHR)
		dip->devc = devfs_make_node(dev, DEVFS_CHAR,
		    UID_ROOT, GID_OPERATOR,
		    0600, "rdisk_%s", dup);
	else
		dip->devb = devfs_make_node(dev, DEVFS_BLOCK,
		    UID_ROOT, GID_OPERATOR,
		    0600, "disk_%s", dup);
	FREE(dup, M_TEMP);

	return (error);
}

void
ddi_remove_minor_node(dev_info_t *dip, char *name)
{
	if (dip->devc) {
		devfs_remove(dip->devc);
		dip->devc = NULL;
	}
	if (dip->devb) {
		devfs_remove(dip->devb);
		dip->devb = NULL;
	}
}

int
strspn(const char *string,
    char *charset)
{
	const char *p, *q;

	for (q = string; *q != '\0'; ++q) {
		for (p = charset; *p != '\0' && *p != *q; ++p)
			;
		if (*p == '\0')
			break;
	}
	return (q-string);
}

#undef strcmp
int
spl_strcmp(const char *s1, const char *s2)
{
	char c1, c2;

	while (1) {
		c1 = *s1++;
		c2 = *s2++;

		if ((c1 == 0) &&
		    (c2 == 0))
			break;

		if ((c1 == 0) ||
		    (c1 < c2))
			return (-1);
		if ((c2 == 0) ||
		    (c1 > c2))
			return (1);
	} // while

	return (0);
}
