/*****************************************************************************\
 *  Copyright (C) 2007-2010 Lawrence Livermore National Security, LLC.
 *  Copyright (C) 2007 The Regents of the University of California.
 *  Produced at Lawrence Livermore National Laboratory (cf, DISCLAIMER).
 *  Written by Brian Behlendorf <behlendorf1@llnl.gov>.
 *  UCRL-CODE-235197
 *
 *  This file is part of the SPL, Solaris Porting Layer.
 *  For details, see <http://github.com/behlendorf/spl/>.
 *
 *  The SPL is free software; you can redistribute it and/or modify it
 *  under the terms of the GNU General Public License as published by the
 *  Free Software Foundation; either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  The SPL is distributed in the hope that it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 *  for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with the SPL.  If not, see <http://www.gnu.org/licenses/>.
 *****************************************************************************
 *  Solaris Porting LAyer Tests (SPLAT) Kmem Tests.
\*****************************************************************************/

#include "splat-internal.h"

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
#define SPLAT_KMEM_TEST5_NAME		"slab_small"
#define SPLAT_KMEM_TEST5_DESC		"Slab ctor/dtor test (small)"

#define SPLAT_KMEM_TEST6_ID		0x0106
#define SPLAT_KMEM_TEST6_NAME		"slab_large"
#define SPLAT_KMEM_TEST6_DESC		"Slab ctor/dtor test (large)"

#define SPLAT_KMEM_TEST7_ID		0x0107
#define SPLAT_KMEM_TEST7_NAME		"slab_align"
#define SPLAT_KMEM_TEST7_DESC		"Slab alignment test"

#define SPLAT_KMEM_TEST8_ID		0x0108
#define SPLAT_KMEM_TEST8_NAME		"slab_reap"
#define SPLAT_KMEM_TEST8_DESC		"Slab reaping test"

#define SPLAT_KMEM_TEST9_ID		0x0109
#define SPLAT_KMEM_TEST9_NAME		"slab_age"
#define SPLAT_KMEM_TEST9_DESC		"Slab aging test"

#define SPLAT_KMEM_TEST10_ID		0x010a
#define SPLAT_KMEM_TEST10_NAME		"slab_lock"
#define SPLAT_KMEM_TEST10_DESC		"Slab locking test"

#ifdef _LP64
#define SPLAT_KMEM_TEST11_ID		0x010b
#define SPLAT_KMEM_TEST11_NAME		"slab_overcommit"
#define SPLAT_KMEM_TEST11_DESC		"Slab memory overcommit test"
#endif /* _LP64 */

#define SPLAT_KMEM_TEST12_ID		0x010c
#define SPLAT_KMEM_TEST12_NAME		"vmem_size"
#define SPLAT_KMEM_TEST12_DESC		"Memory zone test"

#define SPLAT_KMEM_TEST13_ID		0x010d
#define SPLAT_KMEM_TEST13_NAME		"slab_reclaim"
#define SPLAT_KMEM_TEST13_DESC		"Slab direct memory reclaim test"

#define SPLAT_KMEM_ALLOC_COUNT		10
#define SPLAT_VMEM_ALLOC_COUNT		10


static int
splat_kmem_test1(struct file *file, void *arg)
{
	void *ptr[SPLAT_KMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, count, rc = 0;

	while ((!rc) && (size <= (PAGE_SIZE * 32))) {
		count = 0;

		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++) {
			ptr[i] = kmem_alloc(size, KM_SLEEP | KM_NODEBUG);
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

	return rc;
}

static int
splat_kmem_test2(struct file *file, void *arg)
{
	void *ptr[SPLAT_KMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, j, count, rc = 0;

	while ((!rc) && (size <= (PAGE_SIZE * 32))) {
		count = 0;

		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++) {
			ptr[i] = kmem_zalloc(size, KM_SLEEP | KM_NODEBUG);
			if (ptr[i])
				count++;
		}

		/* Ensure buffer has been zero filled */
		for (i = 0; i < SPLAT_KMEM_ALLOC_COUNT; i++) {
			for (j = 0; j < size; j++) {
				if (((char *)ptr[i])[j] != '\0') {
					splat_vprint(file,SPLAT_KMEM_TEST2_NAME,
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
#define SPLAT_KMEM_OBJ_COUNT		1024
#define SPLAT_KMEM_OBJ_RECLAIM		1000 /* objects */
#define SPLAT_KMEM_THREADS		32

#define KCP_FLAG_READY			0x01

typedef struct kmem_cache_data {
	unsigned long kcd_magic;
	struct list_head kcd_node;
	int kcd_flag;
	char kcd_buf[0];
} kmem_cache_data_t;

typedef struct kmem_cache_thread {
	spinlock_t kct_lock;
	int kct_id;
	struct list_head kct_list;
} kmem_cache_thread_t;

typedef struct kmem_cache_priv {
	unsigned long kcp_magic;
	struct file *kcp_file;
	kmem_cache_t *kcp_cache;
	spinlock_t kcp_lock;
	wait_queue_head_t kcp_ctl_waitq;
	wait_queue_head_t kcp_thr_waitq;
	int kcp_flags;
	int kcp_kct_count;
	kmem_cache_thread_t *kcp_kct[SPLAT_KMEM_THREADS];
	int kcp_size;
	int kcp_align;
	int kcp_count;
	int kcp_alloc;
	int kcp_rc;
} kmem_cache_priv_t;

static kmem_cache_priv_t *
splat_kmem_cache_test_kcp_alloc(struct file *file, char *name,
				int size, int align, int alloc)
{
	kmem_cache_priv_t *kcp;

	kcp = kmem_zalloc(sizeof(kmem_cache_priv_t), KM_SLEEP);
	if (!kcp)
		return NULL;

	kcp->kcp_magic = SPLAT_KMEM_TEST_MAGIC;
	kcp->kcp_file = file;
	kcp->kcp_cache = NULL;
	spin_lock_init(&kcp->kcp_lock);
	init_waitqueue_head(&kcp->kcp_ctl_waitq);
	init_waitqueue_head(&kcp->kcp_thr_waitq);
	kcp->kcp_flags = 0;
	kcp->kcp_kct_count = -1;
	kcp->kcp_size = size;
	kcp->kcp_align = align;
	kcp->kcp_count = 0;
	kcp->kcp_alloc = alloc;
	kcp->kcp_rc = 0;

	return kcp;
}

static void
splat_kmem_cache_test_kcp_free(kmem_cache_priv_t *kcp)
{
	kmem_free(kcp, sizeof(kmem_cache_priv_t));
}

static kmem_cache_thread_t *
splat_kmem_cache_test_kct_alloc(kmem_cache_priv_t *kcp, int id)
{
	kmem_cache_thread_t *kct;

	ASSERTF(id < SPLAT_KMEM_THREADS, "id=%d\n", id);
	ASSERT(kcp->kcp_kct[id] == NULL);

	kct = kmem_zalloc(sizeof(kmem_cache_thread_t), KM_SLEEP);
	if (!kct)
		return NULL;

	spin_lock_init(&kct->kct_lock);
	kct->kct_id = id;
	INIT_LIST_HEAD(&kct->kct_list);

	spin_lock(&kcp->kcp_lock);
	kcp->kcp_kct[id] = kct;
	spin_unlock(&kcp->kcp_lock);

	return kct;
}

static void
splat_kmem_cache_test_kct_free(kmem_cache_priv_t *kcp,
			       kmem_cache_thread_t *kct)
{
	spin_lock(&kcp->kcp_lock);
	kcp->kcp_kct[kct->kct_id] = NULL;
	spin_unlock(&kcp->kcp_lock);

	kmem_free(kct, sizeof(kmem_cache_thread_t));
}

static void
splat_kmem_cache_test_kcd_free(kmem_cache_priv_t *kcp,
			       kmem_cache_thread_t *kct)
{
	kmem_cache_data_t *kcd;

	spin_lock(&kct->kct_lock);
	while (!list_empty(&kct->kct_list)) {
		kcd = list_entry(kct->kct_list.next,
				 kmem_cache_data_t, kcd_node);
		list_del(&kcd->kcd_node);
		spin_unlock(&kct->kct_lock);

		kmem_cache_free(kcp->kcp_cache, kcd);

		spin_lock(&kct->kct_lock);
	}
	spin_unlock(&kct->kct_lock);
}

static int
splat_kmem_cache_test_kcd_alloc(kmem_cache_priv_t *kcp,
				kmem_cache_thread_t *kct, int count)
{
	kmem_cache_data_t *kcd;
	int i;

	for (i = 0; i < count; i++) {
		kcd = kmem_cache_alloc(kcp->kcp_cache, KM_SLEEP);
		if (kcd == NULL) {
			splat_kmem_cache_test_kcd_free(kcp, kct);
			return -ENOMEM;
		}

		spin_lock(&kct->kct_lock);
		list_add_tail(&kcd->kcd_node, &kct->kct_list);
		spin_unlock(&kct->kct_lock);
	}

	return 0;
}

static void
splat_kmem_cache_test_debug(struct file *file, char *name,
			    kmem_cache_priv_t *kcp)
{
	int j;

	splat_vprint(file, name,
		     "%s cache objects %d, slabs %u/%u objs %u/%u mags ",
		     kcp->kcp_cache->skc_name, kcp->kcp_count,
		     (unsigned)kcp->kcp_cache->skc_slab_alloc,
		     (unsigned)kcp->kcp_cache->skc_slab_total,
		     (unsigned)kcp->kcp_cache->skc_obj_alloc,
		     (unsigned)kcp->kcp_cache->skc_obj_total);

	for_each_online_cpu(j)
		splat_print(file, "%u/%u ",
			     kcp->kcp_cache->skc_mag[j]->skm_avail,
			     kcp->kcp_cache->skc_mag[j]->skm_size);

	splat_print(file, "%s\n", "");
}

static int
splat_kmem_cache_test_constructor(void *ptr, void *priv, int flags)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;

	if (kcd && kcp) {
		kcd->kcd_magic = kcp->kcp_magic;
		INIT_LIST_HEAD(&kcd->kcd_node);
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

/*
 * Generic reclaim function which assumes that all objects may
 * be reclaimed at any time.  We free a small  percentage of the
 * objects linked off the kcp or kct[] every time we are called.
 */
static void
splat_kmem_cache_test_reclaim(void *priv)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	kmem_cache_thread_t *kct;
	kmem_cache_data_t *kcd;
	LIST_HEAD(reclaim);
	int i, count;

	ASSERT(kcp->kcp_magic == SPLAT_KMEM_TEST_MAGIC);

	/* For each kct thread reclaim some objects */
	spin_lock(&kcp->kcp_lock);
	for (i = 0; i < SPLAT_KMEM_THREADS; i++) {
		kct = kcp->kcp_kct[i];
		if (!kct)
			continue;

		spin_unlock(&kcp->kcp_lock);
		spin_lock(&kct->kct_lock);

		count = SPLAT_KMEM_OBJ_RECLAIM;
		while (count > 0 && !list_empty(&kct->kct_list)) {
			kcd = list_entry(kct->kct_list.next,
					 kmem_cache_data_t, kcd_node);
			list_del(&kcd->kcd_node);
			list_add(&kcd->kcd_node, &reclaim);
			count--;
		}

		spin_unlock(&kct->kct_lock);
		spin_lock(&kcp->kcp_lock);
	}
	spin_unlock(&kcp->kcp_lock);

	/* Freed outside the spin lock */
	while (!list_empty(&reclaim)) {
		kcd = list_entry(reclaim.next, kmem_cache_data_t, kcd_node);
		list_del(&kcd->kcd_node);
		kmem_cache_free(kcp->kcp_cache, kcd);
	}

	return;
}

static int
splat_kmem_cache_test_threads(kmem_cache_priv_t *kcp, int threads)
{
	int rc;

	spin_lock(&kcp->kcp_lock);
	rc = (kcp->kcp_kct_count == threads);
	spin_unlock(&kcp->kcp_lock);

	return rc;
}

static int
splat_kmem_cache_test_flags(kmem_cache_priv_t *kcp, int flags)
{
	int rc;

	spin_lock(&kcp->kcp_lock);
	rc = (kcp->kcp_flags & flags);
	spin_unlock(&kcp->kcp_lock);

	return rc;
}

static void
splat_kmem_cache_test_thread(void *arg)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)arg;
	kmem_cache_thread_t *kct;
	int rc = 0, id;

	ASSERT(kcp->kcp_magic == SPLAT_KMEM_TEST_MAGIC);

	/* Assign thread ids */
	spin_lock(&kcp->kcp_lock);
	if (kcp->kcp_kct_count == -1)
		kcp->kcp_kct_count = 0;

	id = kcp->kcp_kct_count;
	kcp->kcp_kct_count++;
	spin_unlock(&kcp->kcp_lock);

	kct = splat_kmem_cache_test_kct_alloc(kcp, id);
	if (!kct) {
		rc = -ENOMEM;
		goto out;
	}

	/* Wait for all threads to have started and report they are ready */
	if (kcp->kcp_kct_count == SPLAT_KMEM_THREADS)
		wake_up(&kcp->kcp_ctl_waitq);

	wait_event(kcp->kcp_thr_waitq,
		splat_kmem_cache_test_flags(kcp, KCP_FLAG_READY));

	/* Create and destroy objects */
	rc = splat_kmem_cache_test_kcd_alloc(kcp, kct, kcp->kcp_alloc);
	splat_kmem_cache_test_kcd_free(kcp, kct);
out:
	if (kct)
		splat_kmem_cache_test_kct_free(kcp, kct);

	spin_lock(&kcp->kcp_lock);
	if (!kcp->kcp_rc)
		kcp->kcp_rc = rc;

	if ((--kcp->kcp_kct_count) == 0)
		wake_up(&kcp->kcp_ctl_waitq);

	spin_unlock(&kcp->kcp_lock);

	thread_exit();
}

static int
splat_kmem_cache_test(struct file *file, void *arg, char *name,
		      int size, int align, int flags)
{
	kmem_cache_priv_t *kcp;
	kmem_cache_data_t *kcd = NULL;
	int rc = 0, max;

	kcp = splat_kmem_cache_test_kcp_alloc(file, name, size, align, 0);
	if (!kcp) {
		splat_vprint(file, name, "Unable to create '%s'\n", "kcp");
		return -ENOMEM;
	}

	kcp->kcp_cache =
		kmem_cache_create(SPLAT_KMEM_CACHE_NAME,
				  kcp->kcp_size, kcp->kcp_align,
				  splat_kmem_cache_test_constructor,
				  splat_kmem_cache_test_destructor,
				  NULL, kcp, NULL, flags);
	if (!kcp->kcp_cache) {
		splat_vprint(file, name,
			     "Unable to create '%s'\n",
			     SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
		goto out_free;
	}

	kcd = kmem_cache_alloc(kcp->kcp_cache, KM_SLEEP);
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

	if (kcd->kcd_magic != kcp->kcp_magic) {
		splat_vprint(file, name,
			     "Failed to pass private data to constructor "
			     "for '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	max = kcp->kcp_count;
	kmem_cache_free(kcp->kcp_cache, kcd);

	/* Destroy the entire cache which will force destructors to
	 * run and we can verify one was called for every object */
	kmem_cache_destroy(kcp->kcp_cache);
	if (kcp->kcp_count) {
		splat_vprint(file, name,
			     "Failed to run destructor on all slab objects "
			     "for '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -EINVAL;
	}

	splat_kmem_cache_test_kcp_free(kcp);
	splat_vprint(file, name,
		     "Successfully ran ctors/dtors for %d elements in '%s'\n",
		     max, SPLAT_KMEM_CACHE_NAME);

	return rc;

out_free:
	if (kcd)
		kmem_cache_free(kcp->kcp_cache, kcd);

	if (kcp->kcp_cache)
		kmem_cache_destroy(kcp->kcp_cache);

	splat_kmem_cache_test_kcp_free(kcp);

	return rc;
}

static int
splat_kmem_cache_thread_test(struct file *file, void *arg, char *name,
			     int size, int alloc, int max_time)
{
	kmem_cache_priv_t *kcp;
	kthread_t *thr;
	struct timespec start, stop, delta;
	char cache_name[32];
	int i, rc = 0;

	kcp = splat_kmem_cache_test_kcp_alloc(file, name, size, 0, alloc);
	if (!kcp) {
		splat_vprint(file, name, "Unable to create '%s'\n", "kcp");
		return -ENOMEM;
	}

	(void)snprintf(cache_name, 32, "%s-%d-%d",
		       SPLAT_KMEM_CACHE_NAME, size, alloc);
	kcp->kcp_cache =
		kmem_cache_create(cache_name, kcp->kcp_size, 0,
				  splat_kmem_cache_test_constructor,
				  splat_kmem_cache_test_destructor,
				  splat_kmem_cache_test_reclaim,
				  kcp, NULL, 0);
	if (!kcp->kcp_cache) {
		splat_vprint(file, name, "Unable to create '%s'\n", cache_name);
		rc = -ENOMEM;
		goto out_kcp;
	}

	start = current_kernel_time();

	for (i = 0; i < SPLAT_KMEM_THREADS; i++) {
		thr = thread_create(NULL, 0,
				    splat_kmem_cache_test_thread,
				    kcp, 0, &p0, TS_RUN, minclsyspri);
		if (thr == NULL) {
			rc = -ESRCH;
			goto out_cache;
		}
	}

	/* Sleep until all threads have started, then set the ready
	 * flag and wake them all up for maximum concurrency. */
	wait_event(kcp->kcp_ctl_waitq,
		   splat_kmem_cache_test_threads(kcp, SPLAT_KMEM_THREADS));

	spin_lock(&kcp->kcp_lock);
	kcp->kcp_flags |= KCP_FLAG_READY;
	spin_unlock(&kcp->kcp_lock);
	wake_up_all(&kcp->kcp_thr_waitq);

	/* Sleep until all thread have finished */
	wait_event(kcp->kcp_ctl_waitq, splat_kmem_cache_test_threads(kcp, 0));

	stop = current_kernel_time();
	delta = timespec_sub(stop, start);

	splat_vprint(file, name,
		     "%-22s %2ld.%09ld\t"
		     "%lu/%lu/%lu\t%lu/%lu/%lu\n",
		     kcp->kcp_cache->skc_name,
		     delta.tv_sec, delta.tv_nsec,
		     (unsigned long)kcp->kcp_cache->skc_slab_total,
		     (unsigned long)kcp->kcp_cache->skc_slab_max,
		     (unsigned long)(kcp->kcp_alloc *
				    SPLAT_KMEM_THREADS /
				    SPL_KMEM_CACHE_OBJ_PER_SLAB),
		     (unsigned long)kcp->kcp_cache->skc_obj_total,
		     (unsigned long)kcp->kcp_cache->skc_obj_max,
		     (unsigned long)(kcp->kcp_alloc *
				     SPLAT_KMEM_THREADS));

	if (delta.tv_sec >= max_time)
		rc = -ETIME;

	if (!rc && kcp->kcp_rc)
		rc = kcp->kcp_rc;

out_cache:
	kmem_cache_destroy(kcp->kcp_cache);
out_kcp:
	splat_kmem_cache_test_kcp_free(kcp);
	return rc;
}

/* Validate small object cache behavior for dynamic/kmem/vmem caches */
static int
splat_kmem_test5(struct file *file, void *arg)
{
	char *name = SPLAT_KMEM_TEST5_NAME;
	int rc;

	rc = splat_kmem_cache_test(file, arg, name, 128, 0, 0);
	if (rc)
		return rc;

	rc = splat_kmem_cache_test(file, arg, name, 128, 0, KMC_KMEM);
	if (rc)
		return rc;

	return splat_kmem_cache_test(file, arg, name, 128, 0, KMC_VMEM);
}

/*
 * Validate large object cache behavior for dynamic/kmem/vmem caches
 */
static int
splat_kmem_test6(struct file *file, void *arg)
{
	char *name = SPLAT_KMEM_TEST6_NAME;
	int rc;

	rc = splat_kmem_cache_test(file, arg, name, 256*1024, 0, 0);
	if (rc)
		return rc;

	rc = splat_kmem_cache_test(file, arg, name, 64*1024, 0, KMC_KMEM);
	if (rc)
		return rc;

	return splat_kmem_cache_test(file, arg, name, 1024*1024, 0, KMC_VMEM);
}

/*
 * Validate object alignment cache behavior for caches
 */
static int
splat_kmem_test7(struct file *file, void *arg)
{
	char *name = SPLAT_KMEM_TEST7_NAME;
	int i, rc;

	for (i = SPL_KMEM_CACHE_ALIGN; i <= PAGE_SIZE; i *= 2) {
		rc = splat_kmem_cache_test(file, arg, name, 157, i, 0);
		if (rc)
			return rc;
	}

	return rc;
}

/*
 * Validate kmem_cache_reap() by requesting the slab cache free any objects
 * it can.  For a few reasons this may not immediately result in more free
 * memory even if objects are freed.  First off, due to fragmentation we
 * may not be able to reclaim any slabs.  Secondly, even if we do we fully
 * clear some slabs we will not want to immediately reclaim all of them
 * because we may contend with cache allocations and thrash.  What we want
 * to see is the slab size decrease more gradually as it becomes clear they
 * will not be needed.  This should be achievable in less than a minute.
 * If it takes longer than this something has gone wrong.
 */
static int
splat_kmem_test8(struct file *file, void *arg)
{
	kmem_cache_priv_t *kcp;
	kmem_cache_thread_t *kct;
	int i, rc = 0;

	kcp = splat_kmem_cache_test_kcp_alloc(file, SPLAT_KMEM_TEST8_NAME,
					      256, 0, 0);
	if (!kcp) {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
			     "Unable to create '%s'\n", "kcp");
		rc = -ENOMEM;
		goto out;
	}

	kcp->kcp_cache =
		kmem_cache_create(SPLAT_KMEM_CACHE_NAME, kcp->kcp_size, 0,
				  splat_kmem_cache_test_constructor,
				  splat_kmem_cache_test_destructor,
				  splat_kmem_cache_test_reclaim,
				  kcp, NULL, 0);
	if (!kcp->kcp_cache) {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
			   "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
		goto out_kcp;
	}

	kct = splat_kmem_cache_test_kct_alloc(kcp, 0);
	if (!kct) {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
			     "Unable to create '%s'\n", "kct");
		rc = -ENOMEM;
		goto out_cache;
	}

	rc = splat_kmem_cache_test_kcd_alloc(kcp, kct, SPLAT_KMEM_OBJ_COUNT);
	if (rc) {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME, "Unable to "
			     "allocate from '%s'\n", SPLAT_KMEM_CACHE_NAME);
		goto out_kct;
	}

	for (i = 0; i < 60; i++) {
		kmem_cache_reap_now(kcp->kcp_cache);
		splat_kmem_cache_test_debug(file, SPLAT_KMEM_TEST8_NAME, kcp);

		if (kcp->kcp_cache->skc_obj_total == 0)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	if (kcp->kcp_cache->skc_obj_total == 0) {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
			"Successfully created %d objects "
			"in cache %s and reclaimed them\n",
			SPLAT_KMEM_OBJ_COUNT, SPLAT_KMEM_CACHE_NAME);
	} else {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
			"Failed to reclaim %u/%d objects from cache %s\n",
			(unsigned)kcp->kcp_cache->skc_obj_total,
			SPLAT_KMEM_OBJ_COUNT, SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
	}

	/* Cleanup our mess (for failure case of time expiring) */
	splat_kmem_cache_test_kcd_free(kcp, kct);
out_kct:
	splat_kmem_cache_test_kct_free(kcp, kct);
out_cache:
	kmem_cache_destroy(kcp->kcp_cache);
out_kcp:
	splat_kmem_cache_test_kcp_free(kcp);
out:
	return rc;
}

/* Test cache aging, we have allocated a large number of objects thus
 * creating a large number of slabs and then free'd them all.  However,
 * since there should be little memory pressure at the moment those
 * slabs have not been freed.  What we want to see is the slab size
 * decrease gradually as it becomes clear they will not be be needed.
 * This should be achievable in less than minute.  If it takes longer
 * than this something has gone wrong.
 */
static int
splat_kmem_test9(struct file *file, void *arg)
{
	kmem_cache_priv_t *kcp;
	kmem_cache_thread_t *kct;
	int i, rc = 0, count = SPLAT_KMEM_OBJ_COUNT * 128;

	kcp = splat_kmem_cache_test_kcp_alloc(file, SPLAT_KMEM_TEST9_NAME,
					      256, 0, 0);
	if (!kcp) {
		splat_vprint(file, SPLAT_KMEM_TEST9_NAME,
			     "Unable to create '%s'\n", "kcp");
		rc = -ENOMEM;
		goto out;
	}

	kcp->kcp_cache =
		kmem_cache_create(SPLAT_KMEM_CACHE_NAME, kcp->kcp_size, 0,
				  splat_kmem_cache_test_constructor,
				  splat_kmem_cache_test_destructor,
				  NULL, kcp, NULL, 0);
	if (!kcp->kcp_cache) {
		splat_vprint(file, SPLAT_KMEM_TEST9_NAME,
			   "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
		goto out_kcp;
	}

	kct = splat_kmem_cache_test_kct_alloc(kcp, 0);
	if (!kct) {
		splat_vprint(file, SPLAT_KMEM_TEST8_NAME,
			     "Unable to create '%s'\n", "kct");
		rc = -ENOMEM;
		goto out_cache;
	}

	rc = splat_kmem_cache_test_kcd_alloc(kcp, kct, count);
	if (rc) {
		splat_vprint(file, SPLAT_KMEM_TEST9_NAME, "Unable to "
			     "allocate from '%s'\n", SPLAT_KMEM_CACHE_NAME);
		goto out_kct;
	}

	splat_kmem_cache_test_kcd_free(kcp, kct);

	for (i = 0; i < 60; i++) {
		splat_kmem_cache_test_debug(file, SPLAT_KMEM_TEST9_NAME, kcp);

		if (kcp->kcp_cache->skc_obj_total == 0)
			break;

		set_current_state(TASK_INTERRUPTIBLE);
		schedule_timeout(HZ);
	}

	if (kcp->kcp_cache->skc_obj_total == 0) {
		splat_vprint(file, SPLAT_KMEM_TEST9_NAME,
			"Successfully created %d objects "
			"in cache %s and reclaimed them\n",
			count, SPLAT_KMEM_CACHE_NAME);
	} else {
		splat_vprint(file, SPLAT_KMEM_TEST9_NAME,
			"Failed to reclaim %u/%d objects from cache %s\n",
			(unsigned)kcp->kcp_cache->skc_obj_total, count,
			SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
	}

out_kct:
	splat_kmem_cache_test_kct_free(kcp, kct);
out_cache:
	kmem_cache_destroy(kcp->kcp_cache);
out_kcp:
	splat_kmem_cache_test_kcp_free(kcp);
out:
	return rc;
}

/*
 * This test creates N threads with a shared kmem cache.  They then all
 * concurrently allocate and free from the cache to stress the locking and
 * concurrent cache performance.  If any one test takes longer than 5
 * seconds to complete it is treated as a failure and may indicate a
 * performance regression.  On my test system no one test takes more
 * than 1 second to complete so a 5x slowdown likely a problem.
 */
static int
splat_kmem_test10(struct file *file, void *arg)
{
	uint64_t size, alloc, rc = 0;

	for (size = 32; size <= 1024*1024; size *= 2) {

		splat_vprint(file, SPLAT_KMEM_TEST10_NAME, "%-22s  %s", "name",
			     "time (sec)\tslabs       \tobjs	\thash\n");
		splat_vprint(file, SPLAT_KMEM_TEST10_NAME, "%-22s  %s", "",
			     "	  \ttot/max/calc\ttot/max/calc\n");

		for (alloc = 1; alloc <= 1024; alloc *= 2) {

			/* Skip tests which exceed available memory.  We
			 * leverage availrmem here for some extra testing */
			if (size * alloc * SPLAT_KMEM_THREADS > availrmem / 2)
				continue;

			rc = splat_kmem_cache_thread_test(file, arg,
				SPLAT_KMEM_TEST10_NAME, size, alloc, 5);
			if (rc)
				break;
		}
	}

	return rc;
}

#ifdef _LP64
/*
 * This test creates N threads with a shared kmem cache which overcommits
 * memory by 4x.  This makes it impossible for the slab to satify the
 * thread requirements without having its reclaim hook run which will
 * free objects back for use.  This behavior is triggered by the linum VM
 * detecting a low memory condition on the node and invoking the shrinkers.
 * This should allow all the threads to complete while avoiding deadlock
 * and for the most part out of memory events.  This is very tough on the
 * system so it is possible the test app may get oom'ed.  This particular
 * test has proven troublesome on 32-bit archs with limited virtual
 * address space so it only run on 64-bit systems.
 */
static int
splat_kmem_test11(struct file *file, void *arg)
{
	uint64_t size, alloc, rc;

	size = 8 * 1024;
	alloc = ((4 * physmem * PAGE_SIZE) / size) / SPLAT_KMEM_THREADS;

	splat_vprint(file, SPLAT_KMEM_TEST11_NAME, "%-22s  %s", "name",
		     "time (sec)\tslabs       \tobjs	\thash\n");
	splat_vprint(file, SPLAT_KMEM_TEST11_NAME, "%-22s  %s", "",
		     "	  \ttot/max/calc\ttot/max/calc\n");

	rc = splat_kmem_cache_thread_test(file, arg,
		SPLAT_KMEM_TEST11_NAME, size, alloc, 60);

	return rc;
}
#endif /* _LP64 */

/*
 * Check vmem_size() behavior by acquiring the alloc/free/total vmem
 * space, then allocate a known buffer size from vmem space.  We can
 * then check that vmem_size() values were updated properly with in
 * a fairly small tolerence.  The tolerance is important because we
 * are not the only vmem consumer on the system.  Other unrelated
 * allocations might occur during the small test window.  The vmem
 * allocation itself may also add in a little extra private space to
 * the buffer.  Finally, verify total space always remains unchanged.
 */
static int
splat_kmem_test12(struct file *file, void *arg)
{
	size_t alloc1, free1, total1;
	size_t alloc2, free2, total2;
	int size = 8*1024*1024;
	void *ptr;

	alloc1 = vmem_size(NULL, VMEM_ALLOC);
	free1  = vmem_size(NULL, VMEM_FREE);
	total1 = vmem_size(NULL, VMEM_ALLOC | VMEM_FREE);
	splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Vmem alloc=%lu "
		     "free=%lu total=%lu\n", (unsigned long)alloc1,
		     (unsigned long)free1, (unsigned long)total1);

	splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Alloc %d bytes\n", size);
	ptr = vmem_alloc(size, KM_SLEEP);
	if (!ptr) {
		splat_vprint(file, SPLAT_KMEM_TEST12_NAME,
		             "Failed to alloc %d bytes\n", size);
		return -ENOMEM;
	}

	alloc2 = vmem_size(NULL, VMEM_ALLOC);
	free2  = vmem_size(NULL, VMEM_FREE);
	total2 = vmem_size(NULL, VMEM_ALLOC | VMEM_FREE);
	splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Vmem alloc=%lu "
		     "free=%lu total=%lu\n", (unsigned long)alloc2,
		     (unsigned long)free2, (unsigned long)total2);

	splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Free %d bytes\n", size);
	vmem_free(ptr, size);
	if (alloc2 < (alloc1 + size - (size / 100)) ||
	    alloc2 > (alloc1 + size + (size / 100))) {
		splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Failed "
			     "VMEM_ALLOC size: %lu != %lu+%d (+/- 1%%)\n",
		             (unsigned long)alloc2,(unsigned long)alloc1,size);
		return -ERANGE;
	}

	if (free2 < (free1 - size - (size / 100)) ||
	    free2 > (free1 - size + (size / 100))) {
		splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Failed "
			     "VMEM_FREE size: %lu != %lu-%d (+/- 1%%)\n",
		             (unsigned long)free2, (unsigned long)free1, size);
		return -ERANGE;
	}

	if (total1 != total2) {
		splat_vprint(file, SPLAT_KMEM_TEST12_NAME, "Failed "
			     "VMEM_ALLOC | VMEM_FREE not constant: "
		             "%lu != %lu\n", (unsigned long)total2,
			     (unsigned long)total1);
		return -ERANGE;
	}

	splat_vprint(file, SPLAT_KMEM_TEST12_NAME,
	             "VMEM_ALLOC within tolerance: ~%ld%% (%ld/%d)\n",
	             (long)abs(alloc1 + (long)size - alloc2) * 100 / (long)size,
	             (long)abs(alloc1 + (long)size - alloc2), size);
	splat_vprint(file, SPLAT_KMEM_TEST12_NAME,
	             "VMEM_FREE within tolerance:  ~%ld%% (%ld/%d)\n",
	             (long)abs((free1 - (long)size) - free2) * 100 / (long)size,
	             (long)abs((free1 - (long)size) - free2), size);

	return 0;
}

typedef struct dummy_page {
	struct list_head dp_list;
	char             dp_pad[PAGE_SIZE - sizeof(struct list_head)];
} dummy_page_t;

/*
 * This test is designed to verify that direct reclaim is functioning as
 * expected.  We allocate a large number of objects thus creating a large
 * number of slabs.  We then apply memory pressure and expect that the
 * direct reclaim path can easily recover those slabs.  The registered
 * reclaim function will free the objects and the slab shrinker will call
 * it repeatedly until at least a single slab can be freed.
 *
 * Note it may not be possible to reclaim every last slab via direct reclaim
 * without a failure because the shrinker_rwsem may be contended.  For this
 * reason, quickly reclaiming 3/4 of the slabs is considered a success.
 *
 * This should all be possible within 10 seconds.  For reference, on a
 * system with 2G of memory this test takes roughly 0.2 seconds to run.
 * It may take longer on larger memory systems but should still easily
 * complete in the alloted 10 seconds.
 */
static int
splat_kmem_test13(struct file *file, void *arg)
{
	kmem_cache_priv_t *kcp;
	kmem_cache_thread_t *kct;
	dummy_page_t *dp;
	struct list_head list;
	struct timespec start, delta = { 0, 0 };
	int size, count, slabs, fails = 0;
	int i, rc = 0, max_time = 10;

	size = 128 * 1024;
	count = ((physmem * PAGE_SIZE) / 4 / size);

	kcp = splat_kmem_cache_test_kcp_alloc(file, SPLAT_KMEM_TEST13_NAME,
	                                      size, 0, 0);
	if (!kcp) {
		splat_vprint(file, SPLAT_KMEM_TEST13_NAME,
		             "Unable to create '%s'\n", "kcp");
		rc = -ENOMEM;
		goto out;
	}

	kcp->kcp_cache =
		kmem_cache_create(SPLAT_KMEM_CACHE_NAME, kcp->kcp_size, 0,
		                  splat_kmem_cache_test_constructor,
		                  splat_kmem_cache_test_destructor,
				  splat_kmem_cache_test_reclaim,
		                  kcp, NULL, 0);
	if (!kcp->kcp_cache) {
		splat_vprint(file, SPLAT_KMEM_TEST13_NAME,
		             "Unable to create '%s'\n", SPLAT_KMEM_CACHE_NAME);
		rc = -ENOMEM;
		goto out_kcp;
	}

	kct = splat_kmem_cache_test_kct_alloc(kcp, 0);
	if (!kct) {
		splat_vprint(file, SPLAT_KMEM_TEST13_NAME,
			     "Unable to create '%s'\n", "kct");
		rc = -ENOMEM;
		goto out_cache;
	}

	rc = splat_kmem_cache_test_kcd_alloc(kcp, kct, count);
	if (rc) {
		splat_vprint(file, SPLAT_KMEM_TEST13_NAME, "Unable to "
			     "allocate from '%s'\n", SPLAT_KMEM_CACHE_NAME);
		goto out_kct;
	}

	i = 0;
	slabs = kcp->kcp_cache->skc_slab_total;
	INIT_LIST_HEAD(&list);
	start = current_kernel_time();

	/* Apply memory pressure */
	while (kcp->kcp_cache->skc_slab_total > (slabs >> 2)) {

		if ((i % 10000) == 0)
			splat_kmem_cache_test_debug(
			    file, SPLAT_KMEM_TEST13_NAME, kcp);

		delta = timespec_sub(current_kernel_time(), start);
		if (delta.tv_sec >= max_time) {
			splat_vprint(file, SPLAT_KMEM_TEST13_NAME,
				     "Failed to reclaim 3/4 of cache in %ds, "
				     "%u/%u slabs remain\n", max_time,
				     (unsigned)kcp->kcp_cache->skc_slab_total,
				     slabs);
			rc = -ETIME;
			break;
		}

		dp = (dummy_page_t *)__get_free_page(GFP_KERNEL | __GFP_NORETRY);
		if (!dp) {
			fails++;
			splat_vprint(file, SPLAT_KMEM_TEST13_NAME,
				     "Failed (%d) to allocate page with %u "
				     "slabs still in the cache\n", fails,
				     (unsigned)kcp->kcp_cache->skc_slab_total);
			continue;
		}

		list_add(&dp->dp_list, &list);
		i++;
	}

	if (rc == 0)
		splat_vprint(file, SPLAT_KMEM_TEST13_NAME,
			     "Successfully created %u slabs and with %d alloc "
			     "failures reclaimed 3/4 of them in %d.%03ds\n",
			     slabs, fails,
			     (int)delta.tv_sec, (int)delta.tv_nsec / 1000000);

	/* Release memory pressure pages */
	while (!list_empty(&list)) {
		dp = list_entry(list.next, dummy_page_t, dp_list);
		list_del_init(&dp->dp_list);
		free_page((unsigned long)dp);
	}

	/* Release remaining kmem cache objects */
	splat_kmem_cache_test_kcd_free(kcp, kct);
out_kct:
	splat_kmem_cache_test_kct_free(kcp, kct);
out_cache:
	kmem_cache_destroy(kcp->kcp_cache);
out_kcp:
	splat_kmem_cache_test_kcp_free(kcp);
out:
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
	SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST9_NAME, SPLAT_KMEM_TEST9_DESC,
			SPLAT_KMEM_TEST9_ID, splat_kmem_test9);
	SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST10_NAME, SPLAT_KMEM_TEST10_DESC,
			SPLAT_KMEM_TEST10_ID, splat_kmem_test10);
#ifdef _LP64
	SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST11_NAME, SPLAT_KMEM_TEST11_DESC,
			SPLAT_KMEM_TEST11_ID, splat_kmem_test11);
#endif /* _LP64 */
	SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST12_NAME, SPLAT_KMEM_TEST12_DESC,
			SPLAT_KMEM_TEST12_ID, splat_kmem_test12);
	SPLAT_TEST_INIT(sub, SPLAT_KMEM_TEST13_NAME, SPLAT_KMEM_TEST13_DESC,
			SPLAT_KMEM_TEST13_ID, splat_kmem_test13);

	return sub;
}

void
splat_kmem_fini(splat_subsystem_t *sub)
{
	ASSERT(sub);
	SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST13_ID);
	SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST12_ID);
#ifdef _LP64
	SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST11_ID);
#endif /* _LP64 */
	SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST10_ID);
	SPLAT_TEST_FINI(sub, SPLAT_KMEM_TEST9_ID);
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
