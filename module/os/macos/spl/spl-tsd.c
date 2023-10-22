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
 *
 * Copyright (C) 2014 Jorgen Lundman <lundman@lundman.net>
 *
 * A thread will call tsd_create(&key, dtor) to allocate a new
 * "variable" placement, called a "key". In illumos, this is the index
 * into an array of dtors. (If dtor is passed as NULL, TSD internally
 * set it to an empty function). So if the dtor array[i] is NULL, it
 * is "free" and can be allocated. (returned as *key = i).
 * illumos will grow this dtor array with realloc when required.
 * Then Any Thread can set a value on this "key index", and this value
 * is specific to each thread by calling tsd_set(key, value).
 * And can be retrieved with tsd_get(key).
 * When tsd_destroy(key) is called, we need to loop through all
 * threads different "values", and call the dtor on each one.
 * Likewise, we need to know when a thread exists, so we can clean up
 * the values (by calling dtor for each one) so we patch into the
 * thread_exit() call, to also call tsd_thread_exit().
 *
 * In OsX, we build an array of the dtors, and return the key index,
 * this is to store the dtor, and know which "key" values are valid.
 * Then we build an AVL tree, indexed by <key,threadid>, to store
 * each thread's value. This allows us to do key access quick.
 * On thread_exit, we iterate the dtor array, and for each key
 * remove <key,current_thread>.
 * On tsd_destroy(key), we use AVL find nearest with <key,0>, then
 * avl_next as long as key remains the same, to remove each thread value.
 *
 * Note a key of "0" is considered "invalid" in IllumOS, so we return
 * a "1" based index, even though internally it is 0 based.
 *
 */

#include <sys/kmem.h>
#include <sys/thread.h>
#include <sys/tsd.h>
#include <sys/avl.h>
#include <sys/debug.h>

/* Initial size of array, and realloc growth size */
#define	TSD_ALLOC_SIZE 10

/* array of dtors, allocated in init */
static dtor_func_t	*tsd_dtor_array = NULL;
static uint32_t		tsd_dtor_size  = 0;
static avl_tree_t	tsd_tree;

struct spl_tsd_node_s
{
	/* The index/key */
	uint_t		tsd_key;
	thread_t	tsd_thread;

	/* The payload */
	void		*tsd_value;

	/* Internal mumbo */
	avl_node_t	tsd_link_node;
};
typedef struct spl_tsd_node_s spl_tsd_node_t;

static kmutex_t spl_tsd_mutex;

/*
 * tsd_set - set thread specific data
 * @key: lookup key
 * @value: value to set
 *
 * Caller must prevent racing tsd_create() or tsd_destroy(), protected
 * from racing tsd_get() or tsd_set() because it is thread specific.
 * This function has been optimized to be fast for the update case.
 * When setting the tsd initially it will be slower due to additional
 * required locking and potential memory allocations.
 * If the value is set to NULL, we also release it.
 */
int
tsd_set(uint_t key, void *value)
{
	spl_tsd_node_t *entry = NULL;
	spl_tsd_node_t search;
	avl_index_t loc;
	uint_t i;

	/* Invalid key values? */
	if ((key < 1) ||
	    (key >= tsd_dtor_size)) {
		return (EINVAL);
	}

	i = key - 1;

	/*
	 * First handle the easy case, <key,thread> already has a node/value
	 * so we just need to find it, update it.
	 */

	search.tsd_key = i;
	search.tsd_thread = current_thread();

	mutex_enter(&spl_tsd_mutex);
	entry = avl_find(&tsd_tree, &search, &loc);
	mutex_exit(&spl_tsd_mutex);

	if (entry) {

		/* If value is set to NULL, release it as well */
		if (value == NULL) {
			mutex_enter(&spl_tsd_mutex);
			avl_remove(&tsd_tree, entry);
			mutex_exit(&spl_tsd_mutex);
			kmem_free(entry, sizeof (*entry));
			return (0);
		}
		entry->tsd_value = value;
		return (0);
	}

	/* No node, we need to create a new one and insert it. */
	/* But if the value is NULL, then why create one eh? */
	if (value == NULL)
		return (0);

	entry = kmem_alloc(sizeof (spl_tsd_node_t), KM_SLEEP);

	entry->tsd_key		= i;
	entry->tsd_thread	= current_thread();
	entry->tsd_value	= value;

	mutex_enter(&spl_tsd_mutex);
	avl_add(&tsd_tree, entry);
	mutex_exit(&spl_tsd_mutex);

	return (0);
}

/*
 * tsd_get - get thread specific data for specified thread
 * @key: lookup key
 *
 * Caller must prevent racing tsd_create() or tsd_destroy().  This
 * implementation is designed to be fast and scalable, it does not
 * lock the entire table only a single hash bin.
 */
void *
tsd_get_by_thread(uint_t key, thread_t thread)
{
	spl_tsd_node_t *entry = NULL;
	spl_tsd_node_t search;
	avl_index_t loc;
	uint_t i;

	/* Invalid key values? */
	if ((key < 1) ||
	    (key >= tsd_dtor_size)) {
		return (NULL);
	}

	i = key - 1;

	search.tsd_key = i;
	search.tsd_thread = thread;

	mutex_enter(&spl_tsd_mutex);
	entry = avl_find(&tsd_tree, &search, &loc);
	mutex_exit(&spl_tsd_mutex);

	return (entry ? entry->tsd_value : NULL);
}

void *
tsd_get(uint_t key)
{
	return (tsd_get_by_thread(key, current_thread()));
}

static void
tsd_internal_dtor(void *value)
{
}

/*
 * Create TSD for a pid and fill in key with unique value, remember the dtor
 *
 * We cheat and create an entry with pid=0, to keep the dtor.
 */
void
tsd_create(uint_t *keyp, dtor_func_t dtor)
{
	uint_t i;

	if (*keyp)
		return;

	// Iterate the dtor_array, looking for first NULL
	for (i = 0; i < TSD_ALLOC_SIZE; i++) {
		if (tsd_dtor_array[i] == NULL) break;
	}

	/* Do we need to grow the list? */
	if (i >= tsd_dtor_size) {
		printf("SPL: tsd list growing not implemented\n");
		return;
	}

	if (dtor == NULL)
		dtor = tsd_internal_dtor;

	tsd_dtor_array[i] = dtor;

	*keyp = i + 1;
}

void
tsd_destroy(uint_t *keyp)
{
	spl_tsd_node_t *entry = NULL, *next = NULL;
	spl_tsd_node_t search;
	avl_index_t loc;
	dtor_func_t dtor = NULL;
	uint_t i;

	/* Invalid key values? */
	if ((*keyp < 1) ||
	    (*keyp >= tsd_dtor_size)) {
		return;
	}

	i = *keyp - 1;
	*keyp = 0;

	ASSERT(tsd_dtor_array[i] != NULL);

	dtor = tsd_dtor_array[i];
	tsd_dtor_array[i] = NULL;

	/*
	 * For each thread;
	 *   if it has a value
	 *   call the dtor
	 */
	search.tsd_key = i;
	search.tsd_thread = NULL;

	mutex_enter(&spl_tsd_mutex);
	entry = avl_find(&tsd_tree, &search, &loc);

	/*
	 * "entry" should really be NULL here, as we searched for the
	 * NULL thread
	 */
	if (entry == NULL)
		entry = avl_nearest(&tsd_tree, loc, AVL_AFTER);

	/* Now, free node, and go to next, as long as the key matches */
	while (entry && (entry->tsd_key == i)) {
		next = AVL_NEXT(&tsd_tree, entry);

		/* If we have a value, call the dtor for this thread */
		if (entry->tsd_value)
			dtor(entry->tsd_value);

		avl_remove(&tsd_tree, entry);

		kmem_free(entry, sizeof (*entry));

		entry = next;
	}

	mutex_exit(&spl_tsd_mutex);
}



/*
 * A thread is exiting, clear out any tsd values it might have.
 */
void
tsd_thread_exit(void)
{
	spl_tsd_node_t *entry = NULL;
	spl_tsd_node_t search;
	avl_index_t loc;
	int i;

	search.tsd_thread = current_thread();

	/* For all defined dtor/values */
	for (i = 0; i < tsd_dtor_size; i++) {

		/* If not allocated, skip */
		if (tsd_dtor_array[i] == NULL) continue;

		/* Find out of this thread has a value */
		search.tsd_key = i;

		mutex_enter(&spl_tsd_mutex);
		entry = avl_find(&tsd_tree, &search, &loc);
		if (entry) avl_remove(&tsd_tree, entry);
		mutex_exit(&spl_tsd_mutex);

		if (entry == NULL) continue;

		/* If we have a value, call dtor */
		if (entry->tsd_value)
			tsd_dtor_array[i](entry->tsd_value);

		kmem_free(entry, sizeof (*entry));
	} // for all i
}

static int
tsd_tree_cmp(const void *arg1, const void *arg2)
{
	const spl_tsd_node_t *node1 = arg1;
	const spl_tsd_node_t *node2 = arg2;
	if (node1->tsd_key > node2->tsd_key)
		return (1);
	if (node1->tsd_key < node2->tsd_key)
		return (-1);
	if (node1->tsd_thread > node2->tsd_thread)
		return (1);
	if (node1->tsd_thread < node2->tsd_thread)
		return (-1);
	return (0);
}

int
spl_tsd_init(void)
{
	tsd_dtor_array = kmem_zalloc(sizeof (dtor_func_t) * TSD_ALLOC_SIZE,
	    KM_SLEEP);
	tsd_dtor_size = TSD_ALLOC_SIZE;

	mutex_init(&spl_tsd_mutex, NULL, MUTEX_DEFAULT, NULL);
	avl_create(&tsd_tree, tsd_tree_cmp,
	    sizeof (spl_tsd_node_t),
	    offsetof(spl_tsd_node_t, tsd_link_node));
	return (0);
}


uint64_t
spl_tsd_size(void)
{
	return (avl_numnodes(&tsd_tree));
}

void
spl_tsd_fini(void)
{
	spl_tsd_node_t *entry = NULL;
	void *cookie = NULL;

	printf("SPL: tsd unloading %llu\n", spl_tsd_size());

	mutex_enter(&spl_tsd_mutex);
	cookie = NULL;
	while ((entry = avl_destroy_nodes(&tsd_tree, &cookie))) {
		kmem_free(entry, sizeof (*entry));
	}
	mutex_exit(&spl_tsd_mutex);

	avl_destroy(&tsd_tree);
	mutex_destroy(&spl_tsd_mutex);

	kmem_free(tsd_dtor_array, sizeof (dtor_func_t) * tsd_dtor_size);
	tsd_dtor_size = 0;
}
