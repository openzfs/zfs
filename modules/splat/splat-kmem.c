/*
 *  This file is part of the SPL: Solaris Porting Layer.
 *
 *  Copyright (c) 2008 Lawrence Livermore National Security, LLC.
 *  Produced at Lawrence Livermore National Laboratory
 *  Written by:
 *          Brian Behlendorf <behlendorf1@llnl.gov>,
 *          Herb Wartens <wartens2@llnl.gov>,
 *          Jim Garlick <garlick@llnl.gov>
 *  UCRL-CODE-235197
 *
 *  This is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA.
 */

#include "splat-internal.h"

#define SPLAT_SUBSYSTEM_KMEM		0x0100
#define SPLAT_KMEM_NAME			"kmem"
#define SPLAT_KMEM_DESC			"Kernel Malloc/Slab Tests"

#define SPLAT_KMEM_TEST1_ID		0x0101
#define SPLAT_KMEM_TEST1_NAME		"kmem_alloc"
#define SPLAT_KMEM_TEST1_DESC		"Memory allocation test (kmem_alloc)"

#define SPLAT_KMEM_TEST2_ID		0x0102
#define SPLAT_KMEM_TEST2_NAME		"kmem_zalloc"
#define SPLAT_KMEM_TEST2_DESC		"Memory allocation test (kmem_zalloc)"

#define SPLAT_KMEM_TEST3_ID		0x0103
#define SPLAT_KMEM_TEST3_NAME		"slab_alloc"
#define SPLAT_KMEM_TEST3_DESC		"Slab constructor/destructor test"

#define SPLAT_KMEM_TEST4_ID		0x0104
#define SPLAT_KMEM_TEST4_NAME		"slab_reap"
#define SPLAT_KMEM_TEST4_DESC		"Slab reaping test"

#define SPLAT_KMEM_TEST5_ID		0x0105
#define SPLAT_KMEM_TEST5_NAME		"vmem_alloc"
#define SPLAT_KMEM_TEST5_DESC		"Memory allocation test (vmem_alloc)"

#define SPLAT_KMEM_ALLOC_COUNT		10
#define SPLAT_VMEM_ALLOC_COUNT		10

/* XXX - This test may fail under tight memory conditions */
static int
splat_kmem_test1(struct file *file, void *arg)
{
	void *ptr[SPLAT_KMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, count, rc = 0;

	/* We are intentionally going to push kmem_alloc to its max
	 * allocation size, so suppress the console warnings for now */
	kmem_set_warning(0);

	while ((!rc) && (size <= (PAGE_SIZE * 32))) {
		count = 0;

		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++) {
			ptr[i] = kmem_alloc(size, KM_SLEEP);
			if (ptr[i])
				count++;
		}

		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++)
			if (ptr[i])
				kmem_free(ptr[i], size);

		splat_vprint(file, SPLAT_KMEM_TEST1_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, SPLAT_KMEM_ALLOC_COUNT);
		if (count != SPLAT_KMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	kmem_set_warning(1);

	return rc;
}

static int
splat_kmem_test2(struct file *file, void *arg)
{
	void *ptr[SPLAT_KMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, j, count, rc = 0;

	/* We are intentionally going to push kmem_alloc to its max
	 * allocation size, so suppress the console warnings for now */
	kmem_set_warning(0);

	while ((!rc) && (size <= (PAGE_SIZE * 32))) {
		count = 0;

		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++) {
			ptr[i] = kmem_zalloc(size, KM_SLEEP);
			if (ptr[i])
				count++;
		}

		/* Ensure buffer has been zero filled */
		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++) {
			for (j = 0; j < size; j++) {
				if (((char *)ptr[i])[j] != '\0') {
					splat_vprint(file, SPLAT_KMEM_TEST2_NAME,
				                  "%d-byte allocation was "
					          "not zeroed\n", size);
					rc = -EFAULT;
				}
			}
		}

		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++)
			if (ptr[i])
				kmem_free(ptr[i], size);

		splat_vprint(file, SPLAT_KMEM_TEST2_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, SPLAT_KMEM_ALLOC_COUNT);
		if (count != SPLAT_KMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	kmem_set_warning(1);

	return rc;
}

#define SPLAT_KMEM_TEST_MAGIC		0x004488CCUL
#define SPLAT_KMEM_CACHE_NAME		"kmem_test"
#define SPLAT_KMEM_CACHE_SIZE		256
#define SPLAT_KMEM_OBJ_COUNT		128
#define SPLAT_KMEM_OBJ_RECLAIM		64

typedef struct kmem_cache_data {
	char kcd_buf[SPLAT_KMEM_CACHE_SIZE];
	unsigned long kcd_magic;
	int kcd_flag;
} kmem_cache_data_t;

typedef struct kmem_cache_priv {
	unsigned long kcp_magic;
	struct file *kcp_file;
	kmem_cache_t *kcp_cache;
	kmem_cache_data_t *kcp_kcd[SPLAT_KMEM_OBJ_COUNT];
	int kcp_count;
	int kcp_rc;
} kmem_cache_priv_t;

static int
splat_kmem_test34_constructor(void *ptr, void *priv, int flags)
{
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;

	if (kcd) {
		memset(kcd->kcd_buf, 0xaa, SPLAT_KMEM_CACHE_SIZE);
		kcd->kcd_flag = 1;

		if (kcp) {
			kcd->kcd_magic = kcp->kcp_magic;
			kcp->kcp_count++;
		}
	}

	return 0;
}

static void
splat_kmem_test34_destructor(void *ptr, void *priv)
{
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;

	if (kcd) {
		memset(kcd->kcd_buf, 0xbb, SPLAT_KMEM_CACHE_SIZE);
		kcd->kcd_flag = 0;

		if (kcp)
			kcp->kcp_count--;
	}

	return;
}

static int
splat_kmem_test3(struct file *file, void *arg)
{
	kmem_cache_t *cache = NULL;
	kmem_cache_data_t *kcd = NULL;
	kmem_cache_priv_t kcp;
	int rc = 0, max;

	kcp.kcp_magic = SPLAT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;
	kcp.kcp_count = 0;
	kcp.kcp_rc = 0;

	cache = kmem_cache_create(SPLAT_KMEM_CACHE_NAME, sizeof(*kcd), 0,
	                          splat_kmem_test34_constructor,
	                          splat_kmem_test34_destructor,
	                          NULL, &kcp, NULL, 0);
	if (!cache) {
		splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
	                   "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		return -ENOMEM;
	}

	kcd = kmem_cache_alloc(cache, 0);
	if (!kcd) {
		splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
	                   "Unable to allocate from '%s'\n",
		           SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	if (!kcd->kcd_flag) {
		splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
		           "Failed to run contructor for '%s'\n",
		           SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	if (kcd->kcd_magic != kcp.kcp_magic) {
		splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
		           "Failed to pass private data to constructor "
		           "for '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	max = kcp.kcp_count;

	/* Destructor's run lazily so it hard to check correctness here.
	 * We assume if it doesn't crash the free worked properly */
	kmem_cache_free(cache, kcd);

	/* Destroy the entire cache which will force destructors to
	 * run and we can verify one was called for every object */
	kmem_cache_destroy(cache);
	if (kcp.kcp_count) {
		splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
		           "Failed to run destructor on all slab objects "
		           "for '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
	}

	splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
	           "%d allocated/destroyed objects for '%s'\n",
	           max, SPLAT_KMEM_CACHE_NAME);

	return rc;

out_free:
	if (kcd)
		kmem_cache_free(cache, kcd);

	kmem_cache_destroy(cache);
	return rc;
}

static void
splat_kmem_test4_reclaim(void *priv)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	int i;

	splat_vprint(kcp->kcp_file, SPLAT_KMEM_TEST4_NAME,
                     "Reaping %d objects from '%s'\n",
	             SPLAT_KMEM_OBJ_RECLAIM, SPLAT_KMEM_CACHE_NAME);
	for (i = 0; i < SPLAT_KMEM_OBJ_RECLAIM; i++) {
		if (kcp->kcp_kcd[i]) {
			kmem_cache_free(kcp->kcp_cache, kcp->kcp_kcd[i]);
			kcp->kcp_kcd[i] = NULL;
		}
	}

	return;
}

static int
splat_kmem_test4(struct file *file, void *arg)
{
	kmem_cache_t *cache;
	kmem_cache_priv_t kcp;
	int i, rc = 0, max, reclaim_percent, target_percent;

	kcp.kcp_magic = SPLAT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;
	kcp.kcp_count = 0;
	kcp.kcp_rc = 0;

	cache = kmem_cache_create(SPLAT_KMEM_CACHE_NAME,
	                          sizeof(kmem_cache_data_t), 0,
	                          splat_kmem_test34_constructor,
	                          splat_kmem_test34_destructor,
	                          splat_kmem_test4_reclaim, &kcp, NULL, 0);
	if (!cache) {
		splat_vprint(file, SPLAT_KMEM_TEST4_NAME,
	                   "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		return -ENOMEM;
	}

	kcp.kcp_cache = cache;

	for (i = 0; i < SPLAT_KMEM_OBJ_COUNT; i++) {
		/* All allocations need not succeed */
		kcp.kcp_kcd[i] = kmem_cache_alloc(cache, 0);
		if (!kcp.kcp_kcd[i]) {
			splat_vprint(file, SPLAT_KMEM_TEST4_NAME,
		                   "Unable to allocate from '%s'\n",
			           SPLAT_KMEM_CACHE_NAME);
		}
	}

	max = kcp.kcp_count;
	ASSERT(max > 0);

	/* Force shrinker to run */
	kmem_reap();

	/* Reclaim reclaimed objects, this ensure the destructors are run */
	kmem_cache_reap_now(cache);

	reclaim_percent = ((kcp.kcp_count * 100) / max);
	target_percent = (((SPLAT_KMEM_OBJ_COUNT - SPLAT_KMEM_OBJ_RECLAIM) * 100) /
	                    SPLAT_KMEM_OBJ_COUNT);
	splat_vprint(file, SPLAT_KMEM_TEST4_NAME,
                   "%d%% (%d/%d) of previous size, target of "
	           "%d%%-%d%% for '%s'\n", reclaim_percent, kcp.kcp_count,
	           max, target_percent - 10, target_percent + 10,
	           SPLAT_KMEM_CACHE_NAME);
	if ((reclaim_percent < target_percent - 10) ||
	    (reclaim_percent > target_percent + 10))
		rc = -EINVAL;

	/* Cleanup our mess */
	for (i = 0; i < SPLAT_KMEM_OBJ_COUNT; i++)
		if (kcp.kcp_kcd[i])
			kmem_cache_free(cache, kcp.kcp_kcd[i]);

	kmem_cache_destroy(cache);

	return rc;
}

static int
splat_kmem_test5(struct file *file, void *arg)
{
	void *ptr[SPLAT_VMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, count, rc = 0;

	while ((!rc) && (size <= (PAGE_SIZE * 1024))) {
		count = 0;

		for (i = 0; i < SPLAT_VMEM_ALLOC_COUNT; i++) {
			ptr[i] = vmem_alloc(size, KM_SLEEP);
			if (ptr[i])
				count++;
		}

		for (i = 0; i < SPLAT_VMEM_ALLOC_COUNT; i++)
			if (ptr[i])
				vmem_free(ptr[i], size);

		splat_vprint(file, SPLAT_KMEM_TEST5_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, SPLAT_VMEM_ALLOC_COUNT);
		if (count != SPLAT_VMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	return rc;
}

splat_subsystem_t *
splat_kmem_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_KMEM_NAME, SPLAT_NAME_SIZE);
	strncpy(sub->desc.desc, SPLAT_KMEM_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_KMEM;

        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST1_NAME, SPLAT_KMEM_TEST1_DESC,
	              SPLAT_KMEM_TEST1_ID, splat_kmem_test1);
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST2_NAME, SPLAT_KMEM_TEST2_DESC,
	              SPLAT_KMEM_TEST2_ID, splat_kmem_test2);
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST3_NAME, SPLAT_KMEM_TEST3_DESC,
	              SPLAT_KMEM_TEST3_ID, splat_kmem_test3);
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST4_NAME, SPLAT_KMEM_TEST4_DESC,
	              SPLAT_KMEM_TEST4_ID, splat_kmem_test4);
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST5_NAME, SPLAT_KMEM_TEST5_DESC,
	              SPLAT_KMEM_TEST5_ID, splat_kmem_test5);

        return sub;
}

void
splat_kmem_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST5_ID);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST4_ID);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST1_ID);

        kfree(sub);
}

int
splat_kmem_id(void) {
        return SPLAT_SUBSYSTEM_KMEM;
}
