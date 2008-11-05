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
#define SPLAT_KMEM_TEST3_NAME		"vmem_alloc"
#define SPLAT_KMEM_TEST3_DESC		"Memory allocation test (vmem_alloc)"

#define SPLAT_KMEM_TEST4_ID		0x0104
#define SPLAT_KMEM_TEST4_NAME		"vmem_zalloc"
#define SPLAT_KMEM_TEST4_DESC		"Memory allocation test (vmem_zalloc)"

#define SPLAT_KMEM_TEST5_ID		0x0105
#define SPLAT_KMEM_TEST5_NAME		"kmem_cache1"
#define SPLAT_KMEM_TEST5_DESC		"Slab ctor/dtor test (small)"

#define SPLAT_KMEM_TEST6_ID		0x0106
#define SPLAT_KMEM_TEST6_NAME		"kmem_cache2"
#define SPLAT_KMEM_TEST6_DESC		"Slab ctor/dtor test (large)"

#define SPLAT_KMEM_TEST7_ID		0x0107
#define SPLAT_KMEM_TEST7_NAME		"kmem_reap"
#define SPLAT_KMEM_TEST7_DESC		"Slab reaping test"

#define SPLAT_KMEM_TEST8_ID		0x0108
#define SPLAT_KMEM_TEST8_NAME		"kmem_lock"
#define SPLAT_KMEM_TEST8_DESC		"Slab locking test"

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

static int
splat_kmem_test3(struct file *file, void *arg)
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

		splat_vprint(file, SPLAT_KMEM_TEST3_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, SPLAT_VMEM_ALLOC_COUNT);
		if (count != SPLAT_VMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	return rc;
}

static int
splat_kmem_test4(struct file *file, void *arg)
{
	void *ptr[SPLAT_VMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, j, count, rc = 0;

	while ((!rc) && (size <= (PAGE_SIZE * 1024))) {
		count = 0;

		for (i = 0; i < SPLAT_VMEM_ALLOC_COUNT; i++) {
			ptr[i] = vmem_zalloc(size, KM_SLEEP);
			if (ptr[i])
				count++;
		}

		/* Ensure buffer has been zero filled */
		for (i = 0; i < SPLAT_VMEM_ALLOC_COUNT; i++) {
			for (j = 0; j < size; j++) {
				if (((char *)ptr[i])[j] != '\0') {
					splat_vprint(file, SPLAT_KMEM_TEST4_NAME,
				                  "%d-byte allocation was "
					          "not zeroed\n", size);
					rc = -EFAULT;
				}
			}
		}

		for (i = 0; i < SPLAT_VMEM_ALLOC_COUNT; i++)
			if (ptr[i])
				vmem_free(ptr[i], size);

		splat_vprint(file, SPLAT_KMEM_TEST4_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, SPLAT_VMEM_ALLOC_COUNT);
		if (count != SPLAT_VMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	return rc;
}

#define SPLAT_KMEM_TEST_MAGIC		0x004488CCUL
#define SPLAT_KMEM_CACHE_NAME		"kmem_test"
#define SPLAT_KMEM_OBJ_COUNT		128
#define SPLAT_KMEM_OBJ_RECLAIM		16

typedef struct kmem_cache_data {
	unsigned long kcd_magic;
	int kcd_flag;
	char kcd_buf[0];
} kmem_cache_data_t;

typedef struct kmem_cache_priv {
	unsigned long kcp_magic;
	struct file *kcp_file;
	kmem_cache_t *kcp_cache;
	kmem_cache_data_t *kcp_kcd[SPLAT_KMEM_OBJ_COUNT];
	spinlock_t kcp_lock;
	wait_queue_head_t kcp_waitq;
	int kcp_size;
	int kcp_count;
	int kcp_threads;
	int kcp_alloc;
	int kcp_rc;
} kmem_cache_priv_t;

static int
splat_kmem_cache_test_constructor(void *ptr, void *priv, int flags)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;

	if (kcd && kcp) {
		kcd->kcd_magic = kcp->kcp_magic;
		kcd->kcd_flag = 1;
		memset(kcd->kcd_buf, 0xaa, kcp->kcp_size - (sizeof *kcd));
		kcp->kcp_count++;
	}

	return 0;
}

static void
splat_kmem_cache_test_destructor(void *ptr, void *priv)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;

	if (kcd && kcp) {
		kcd->kcd_magic = 0;
		kcd->kcd_flag = 0;
		memset(kcd->kcd_buf, 0xbb, kcp->kcp_size - (sizeof *kcd));
		kcp->kcp_count--;
	}

	return;
}

static int
splat_kmem_cache_size_test(struct file *file, void *arg,
			   char *name, int size, int flags)
{
	kmem_cache_t *cache = NULL;
	kmem_cache_data_t *kcd = NULL;
	kmem_cache_priv_t kcp;
	int rc = 0, max;

	kcp.kcp_magic = SPLAT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;
	kcp.kcp_size = size;
	kcp.kcp_count = 0;
	kcp.kcp_rc = 0;

	cache = kmem_cache_create(SPLAT_KMEM_CACHE_NAME, kcp.kcp_size, 0,
	                          splat_kmem_cache_test_constructor,
	                          splat_kmem_cache_test_destructor,
	                          NULL, &kcp, NULL, flags);
	if (!cache) {
		splat_vprint(file, name,
	                   "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		return -ENOMEM;
	}

	kcd = kmem_cache_alloc(cache, KM_SLEEP);
	if (!kcd) {
		splat_vprint(file, name,
	                   "Unable to allocate from '%s'\n",
		           SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	if (!kcd->kcd_flag) {
		splat_vprint(file, name,
		           "Failed to run contructor for '%s'\n",
		           SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	if (kcd->kcd_magic != kcp.kcp_magic) {
		splat_vprint(file, name,
		           "Failed to pass private data to constructor "
		           "for '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	max = kcp.kcp_count;
	kmem_cache_free(cache, kcd);

	/* Destroy the entire cache which will force destructors to
	 * run and we can verify one was called for every object */
	kmem_cache_destroy(cache);
	if (kcp.kcp_count) {
		splat_vprint(file, name,
		           "Failed to run destructor on all slab objects "
		           "for '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
	}

	splat_vprint(file, name,
	           "Successfully ran ctors/dtors for %d elements in '%s'\n",
	           max, SPLAT_KMEM_CACHE_NAME);

	return rc;

out_free:
	if (kcd)
		kmem_cache_free(cache, kcd);

	kmem_cache_destroy(cache);
	return rc;
}

/* Validate small object cache behavior for dynamic/kmem/vmem caches */
static int
splat_kmem_test5(struct file *file, void *arg)
{
	char *name = SPLAT_KMEM_TEST5_NAME;
	int rc;

	rc = splat_kmem_cache_size_test(file, arg, name, 128, 0);
	if (rc)
		return rc;

	rc = splat_kmem_cache_size_test(file, arg, name, 128, KMC_KMEM);
	if (rc)
		return rc;

	return splat_kmem_cache_size_test(file, arg, name, 128, KMC_VMEM);
}

/* Validate large object cache behavior for dynamic/kmem/vmem caches */
static int
splat_kmem_test6(struct file *file, void *arg)
{
	char *name = SPLAT_KMEM_TEST6_NAME;
	int rc;

	rc = splat_kmem_cache_size_test(file, arg, name, 128 * 1024, 0);
	if (rc)
		return rc;

	rc = splat_kmem_cache_size_test(file, arg, name, 128 * 1024, KMC_KMEM);
	if (rc)
		return rc;

	return splat_kmem_cache_size_test(file, arg, name, 128 * 1028, KMC_VMEM);
}

static void
splat_kmem_cache_test_reclaim(void *priv)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	int i, count;

	count = min(SPLAT_KMEM_OBJ_RECLAIM, kcp->kcp_count);
	splat_vprint(kcp->kcp_file, SPLAT_KMEM_TEST7_NAME,
                     "Reaping %d objects from '%s'\n", count,
	             SPLAT_KMEM_CACHE_NAME);

	for (i = 0; i < SPLAT_KMEM_OBJ_COUNT; i++) {
		if (kcp->kcp_kcd[i]) {
			kmem_cache_free(kcp->kcp_cache, kcp->kcp_kcd[i]);
			kcp->kcp_kcd[i] = NULL;

			if (--count == 0)
				break;
		}
	}

	return;
}

static int
splat_kmem_test7(struct file *file, void *arg)
{
	kmem_cache_t *cache;
	kmem_cache_priv_t kcp;
	int i, rc = 0;

	kcp.kcp_magic = SPLAT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;
	kcp.kcp_size = 256;
	kcp.kcp_count = 0;
	kcp.kcp_rc = 0;

	cache = kmem_cache_create(SPLAT_KMEM_CACHE_NAME, kcp.kcp_size, 0,
	                          splat_kmem_cache_test_constructor,
	                          splat_kmem_cache_test_destructor,
	                          splat_kmem_cache_test_reclaim,
				  &kcp, NULL, 0);
	if (!cache) {
		splat_vprint(file, SPLAT_KMEM_TEST7_NAME,
	                   "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		return -ENOMEM;
	}

	kcp.kcp_cache = cache;

	for (i = 0; i < SPLAT_KMEM_OBJ_COUNT; i++) {
		/* All allocations need not succeed */
		kcp.kcp_kcd[i] = kmem_cache_alloc(cache, KM_SLEEP);
		if (!kcp.kcp_kcd[i]) {
			splat_vprint(file, SPLAT_KMEM_TEST7_NAME,
		                   "Unable to allocate from '%s'\n",
			           SPLAT_KMEM_CACHE_NAME);
		}
	}

	ASSERT(kcp.kcp_count > 0);

	/* Request the slab cache free any objects it can.  For a few reasons
	 * this may not immediately result in more free memory even if objects
	 * are freed.  First off, due to fragmentation we may not be able to
	 * reclaim any slabs.  Secondly, even if we do we fully clear some
	 * slabs we will not want to immedately reclaim all of them because
	 * we may contend with cache allocs and thrash.  What we want to see
	 * is slab size decrease more gradually as it becomes clear they
	 * will not be needed.  This should be acheivable in less than minute
	 * if it takes longer than this something has gone wrong.
	 */
	for (i = 0; i < 60; i++) {
		kmem_cache_reap_now(cache);
		splat_vprint(file, SPLAT_KMEM_TEST7_NAME,
                             "%s cache objects %d, slabs %u/%u objs %u/%u\n",
			     SPLAT_KMEM_CACHE_NAME, kcp.kcp_count,
			    (unsigned)cache->skc_slab_alloc,
			    (unsigned)cache->skc_slab_total,
			    (unsigned)cache->skc_obj_alloc,
			    (unsigned)cache->skc_obj_total);

		if (cache->skc_obj_total == 0)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	if (cache->skc_obj_total == 0) {
		splat_vprint(file, SPLAT_KMEM_TEST7_NAME,
			"Successfully created %d objects "
			"in cache %s and reclaimed them\n",
		        SPLAT_KMEM_OBJ_COUNT, SPLAT_KMEM_CACHE_NAME);
	} else {
		splat_vprint(file, SPLAT_KMEM_TEST7_NAME,
			"Failed to reclaim %u/%d objects from cache %s\n",
		        (unsigned)cache->skc_obj_total, SPLAT_KMEM_OBJ_COUNT,
			SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
	}

	/* Cleanup our mess (for failure case of time expiring) */
	for (i = 0; i < SPLAT_KMEM_OBJ_COUNT; i++)
		if (kcp.kcp_kcd[i])
			kmem_cache_free(cache, kcp.kcp_kcd[i]);

	kmem_cache_destroy(cache);

	return rc;
}

static void
splat_kmem_test8_thread(void *arg)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)arg;
	int count = kcp->kcp_alloc, rc = 0, i;
	void **objs;

	ASSERT(kcp->kcp_magic == SPLAT_KMEM_TEST_MAGIC);

	objs = vmem_zalloc(count * sizeof(void *), KM_SLEEP);
	if (!objs) {
		splat_vprint(kcp->kcp_file, SPLAT_KMEM_TEST8_NAME,
	                     "Unable to alloc objp array for cache '%s'\n",
		             kcp->kcp_cache->skc_name);
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < count; i++) {
		objs[i] = kmem_cache_alloc(kcp->kcp_cache, KM_SLEEP);
		if (!objs[i]) {
			splat_vprint(kcp->kcp_file, SPLAT_KMEM_TEST8_NAME,
		                     "Unable to allocate from cache '%s'\n",
			             kcp->kcp_cache->skc_name);
			rc = -ENOMEM;
			break;
		}
	}

	for (i = 0; i < count; i++)
		if (objs[i])
			kmem_cache_free(kcp->kcp_cache, objs[i]);

	vmem_free(objs, count * sizeof(void *));
out:
	spin_lock(&kcp->kcp_lock);
	if (!kcp->kcp_rc)
		kcp->kcp_rc = rc;

	if (--kcp->kcp_threads == 0)
	        wake_up(&kcp->kcp_waitq);

	spin_unlock(&kcp->kcp_lock);

        thread_exit();
}

static int
splat_kmem_test8_count(kmem_cache_priv_t *kcp, int threads)
{
	int ret;

	spin_lock(&kcp->kcp_lock);
	ret = (kcp->kcp_threads == threads);
	spin_unlock(&kcp->kcp_lock);

	return ret;
}

/* This test will always pass and is simply here so I can easily
 * eyeball the slab cache locking overhead to ensure it is reasonable.
 */
static int
splat_kmem_test8_sc(struct file *file, void *arg, int size, int count)
{
	kmem_cache_priv_t kcp;
	kthread_t *thr;
	struct timespec start, stop, delta;
	char cache_name[32];
	int i, j, rc = 0, threads = 32;

	kcp.kcp_magic = SPLAT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;

        splat_vprint(file, SPLAT_KMEM_TEST8_NAME, "%-22s  %s", "name",
	             "time (sec)\tslabs       \tobjs        \thash\n");
        splat_vprint(file, SPLAT_KMEM_TEST8_NAME, "%-22s  %s", "",
	             "          \ttot/max/calc\ttot/max/calc\n");

	for (i = 1; i <= count; i *= 2) {
		kcp.kcp_size = size;
		kcp.kcp_count = 0;
		kcp.kcp_threads = 0;
		kcp.kcp_alloc = i;
		kcp.kcp_rc = 0;
	        spin_lock_init(&kcp.kcp_lock);
	        init_waitqueue_head(&kcp.kcp_waitq);

		(void)snprintf(cache_name, 32, "%s-%d-%d",
			       SPLAT_KMEM_CACHE_NAME, size, i);
		kcp.kcp_cache = kmem_cache_create(cache_name, kcp.kcp_size, 0,
	                                  splat_kmem_cache_test_constructor,
	                                  splat_kmem_cache_test_destructor,
					  NULL, &kcp, NULL, 0);
		if (!kcp.kcp_cache) {
			splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
		                     "Unable to create '%s' cache\n",
				     SPLAT_KMEM_CACHE_NAME);
			rc = -ENOMEM;
			break;
		}

		start = current_kernel_time();

		for (j = 0; j < threads; j++) {
			thr = thread_create(NULL, 0, splat_kmem_test8_thread,
			                    &kcp, 0, &p0, TS_RUN, minclsyspri);
			if (thr == NULL) {
				rc = -ESRCH;
				break;
			}
			spin_lock(&kcp.kcp_lock);
			kcp.kcp_threads++;
			spin_unlock(&kcp.kcp_lock);
		}

	        /* Sleep until the thread sets kcp.kcp_threads == 0 */
	        wait_event(kcp.kcp_waitq, splat_kmem_test8_count(&kcp, 0));
		stop = current_kernel_time();
		delta = timespec_sub(stop, start);

	        splat_vprint(file, SPLAT_KMEM_TEST8_NAME, "%-22s %2ld.%09ld\t"
			     "%lu/%lu/%lu\t%lu/%lu/%lu\n",
			     kcp.kcp_cache->skc_name,
			     delta.tv_sec, delta.tv_nsec,
			     (unsigned long)kcp.kcp_cache->skc_slab_total,
			     (unsigned long)kcp.kcp_cache->skc_slab_max,
			     (unsigned long)(kcp.kcp_alloc * threads /
					    SPL_KMEM_CACHE_OBJ_PER_SLAB),
			     (unsigned long)kcp.kcp_cache->skc_obj_total,
			     (unsigned long)kcp.kcp_cache->skc_obj_max,
			     (unsigned long)(kcp.kcp_alloc * threads));

		kmem_cache_destroy(kcp.kcp_cache);

		if (!rc && kcp.kcp_rc)
			rc = kcp.kcp_rc;

		if (rc)
			break;
	}

	return rc;
}

static int
splat_kmem_test8(struct file *file, void *arg)
{
	int i, rc = 0;

	/* Run through slab cache with objects size from
	 * 16-1Mb in 4x multiples with 1024 objects each */
	for (i = 16; i <= 1024*1024; i *= 4) {
		rc = splat_kmem_test8_sc(file, arg, i, 256);
		if (rc)
			break;
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
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST6_NAME, SPLAT_KMEM_TEST6_DESC,
	              SPLAT_KMEM_TEST6_ID, splat_kmem_test6);
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST7_NAME, SPLAT_KMEM_TEST7_DESC,
	              SPLAT_KMEM_TEST7_ID, splat_kmem_test7);
        SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST8_NAME, SPLAT_KMEM_TEST8_DESC,
	              SPLAT_KMEM_TEST8_ID, splat_kmem_test8);

        return sub;
}

void
splat_kmem_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST8_ID);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST7_ID);
        SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST6_ID);
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
