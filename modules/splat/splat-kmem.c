#include <splat-ctl.h>

#define KZT_SUBSYSTEM_KMEM		0x0100
#define KZT_KMEM_NAME			"kmem"
#define KZT_KMEM_DESC			"Kernel Malloc/Slab Tests"

#define KZT_KMEM_TEST1_ID		0x0101
#define KZT_KMEM_TEST1_NAME		"kmem_alloc"
#define KZT_KMEM_TEST1_DESC		"Memory allocation test (kmem_alloc)"

#define KZT_KMEM_TEST2_ID		0x0102
#define KZT_KMEM_TEST2_NAME		"kmem_zalloc"
#define KZT_KMEM_TEST2_DESC		"Memory allocation test (kmem_zalloc)"

#define KZT_KMEM_TEST3_ID		0x0103
#define KZT_KMEM_TEST3_NAME		"slab_alloc"
#define KZT_KMEM_TEST3_DESC		"Slab constructor/destructor test"

#define KZT_KMEM_TEST4_ID		0x0104
#define KZT_KMEM_TEST4_NAME		"slab_reap"
#define KZT_KMEM_TEST4_DESC		"Slab reaping test"

#define KZT_KMEM_ALLOC_COUNT		10
/* XXX - This test may fail under tight memory conditions */
static int
kzt_kmem_test1(struct file *file, void *arg)
{
	void *ptr[KZT_KMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, count, rc = 0;

	while ((!rc) && (size < (PAGE_SIZE * 16))) {
		count = 0;

		for (i = 0; i < KZT_KMEM_ALLOC_COUNT; i++) {
			ptr[i] = kmem_alloc(size, KM_SLEEP);
			if (ptr[i])
				count++;
		}

		for (i = 0; i < KZT_KMEM_ALLOC_COUNT; i++)
			if (ptr[i])
				kmem_free(ptr[i], size);

		kzt_vprint(file, KZT_KMEM_TEST1_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, KZT_KMEM_ALLOC_COUNT);
		if (count != KZT_KMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	return rc;
}

static int
kzt_kmem_test2(struct file *file, void *arg)
{
	void *ptr[KZT_KMEM_ALLOC_COUNT];
	int size = PAGE_SIZE;
	int i, j, count, rc = 0;

	while ((!rc) && (size < (PAGE_SIZE * 16))) {
		count = 0;

		for (i = 0; i < KZT_KMEM_ALLOC_COUNT; i++) {
			ptr[i] = kmem_zalloc(size, KM_SLEEP);
			if (ptr[i])
				count++;
		}

		/* Ensure buffer has been zero filled */
		for (i = 0; i < KZT_KMEM_ALLOC_COUNT; i++) {
			for (j = 0; j < size; j++) {
				if (((char *)ptr[i])[j] != '\0') {
					kzt_vprint(file, KZT_KMEM_TEST2_NAME,
				                  "%d-byte allocation was "
					          "not zeroed\n", size);
					rc = -EFAULT;
				}
			}
		}

		for (i = 0; i < KZT_KMEM_ALLOC_COUNT; i++)
			if (ptr[i])
				kmem_free(ptr[i], size);

		kzt_vprint(file, KZT_KMEM_TEST2_NAME,
	                   "%d byte allocations, %d/%d successful\n",
		           size, count, KZT_KMEM_ALLOC_COUNT);
		if (count != KZT_KMEM_ALLOC_COUNT)
			rc = -ENOMEM;

		size *= 2;
	}

	return rc;
}

#define KZT_KMEM_TEST_MAGIC		0x004488CCUL
#define KZT_KMEM_CACHE_NAME		"kmem_test"
#define KZT_KMEM_CACHE_SIZE		256
#define KZT_KMEM_OBJ_COUNT		128
#define KZT_KMEM_OBJ_RECLAIM		64

typedef struct kmem_cache_data {
	char kcd_buf[KZT_KMEM_CACHE_SIZE];
	unsigned long kcd_magic;
	int kcd_flag;
} kmem_cache_data_t;

typedef struct kmem_cache_priv {
	unsigned long kcp_magic;
	struct file *kcp_file;
	kmem_cache_t *kcp_cache;
	kmem_cache_data_t *kcp_kcd[KZT_KMEM_OBJ_COUNT];
	int kcp_count;
	int kcp_rc;
} kmem_cache_priv_t;

static int
kzt_kmem_test34_constructor(void *ptr, void *priv, int flags)
{
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;

	if (kcd) {
		memset(kcd->kcd_buf, 0xaa, KZT_KMEM_CACHE_SIZE);
		kcd->kcd_flag = 1;

		if (kcp) {
	 		kcd->kcd_magic = kcp->kcp_magic;
			kcp->kcp_count++;
		}
	}

	return 0;
}

static void
kzt_kmem_test34_destructor(void *ptr, void *priv)
{
	kmem_cache_data_t *kcd = (kmem_cache_data_t *)ptr;
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;

	if (kcd) {
		memset(kcd->kcd_buf, 0xbb, KZT_KMEM_CACHE_SIZE);
		kcd->kcd_flag = 0;

		if (kcp)
			kcp->kcp_count--;
	}

	return;
}

static int
kzt_kmem_test3(struct file *file, void *arg)
{
	kmem_cache_t *cache = NULL;
	kmem_cache_data_t *kcd = NULL;
	kmem_cache_priv_t kcp;
	int rc = 0, max;

	kcp.kcp_magic = KZT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;
	kcp.kcp_count = 0;
	kcp.kcp_rc = 0;

	cache = kmem_cache_create(KZT_KMEM_CACHE_NAME, sizeof(*kcd), 0,
	                          kzt_kmem_test34_constructor,
	                          kzt_kmem_test34_destructor,
	                          NULL, &kcp, NULL, 0);
	if (!cache) {
		kzt_vprint(file, KZT_KMEM_TEST3_NAME,
	                   "Unable to create '%s'\n", KZT_KMEM_CACHE_NAME);
		return -ENOMEM;
	}

	kcd = kmem_cache_alloc(cache, 0);
	if (!kcd) {
		kzt_vprint(file, KZT_KMEM_TEST3_NAME,
	                   "Unable to allocate from '%s'\n",
		           KZT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	if (!kcd->kcd_flag) {
		kzt_vprint(file, KZT_KMEM_TEST3_NAME,
		           "Failed to run contructor for '%s'\n",
		           KZT_KMEM_CACHE_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	if (kcd->kcd_magic != kcp.kcp_magic) {
		kzt_vprint(file, KZT_KMEM_TEST3_NAME,
		           "Failed to pass private data to constructor "
		           "for '%s'\n", KZT_KMEM_CACHE_NAME);
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
		kzt_vprint(file, KZT_KMEM_TEST3_NAME,
		           "Failed to run destructor on all slab objects "
		           "for '%s'\n", KZT_KMEM_CACHE_NAME);
		rc = -EINVAL;
	}

	kzt_vprint(file, KZT_KMEM_TEST3_NAME,
	           "%d allocated/destroyed objects for '%s'\n",
	           max, KZT_KMEM_CACHE_NAME);

	return rc;

out_free:
	if (kcd)
		kmem_cache_free(cache, kcd);

	kmem_cache_destroy(cache);
	return rc;
}

static void
kzt_kmem_test4_reclaim(void *priv)
{
	kmem_cache_priv_t *kcp = (kmem_cache_priv_t *)priv;
	int i;

	kzt_vprint(kcp->kcp_file, KZT_KMEM_TEST4_NAME,
                   "Reaping %d objects from '%s'\n",
	           KZT_KMEM_OBJ_RECLAIM, KZT_KMEM_CACHE_NAME);
	for (i = 0; i < KZT_KMEM_OBJ_RECLAIM; i++) {
		if (kcp->kcp_kcd[i]) {
			kmem_cache_free(kcp->kcp_cache, kcp->kcp_kcd[i]);
			kcp->kcp_kcd[i] = NULL;
		}
	}

	return;
}

static int
kzt_kmem_test4(struct file *file, void *arg)
{
	kmem_cache_t *cache;
	kmem_cache_priv_t kcp;
	int i, rc = 0, max, reclaim_percent, target_percent;

	kcp.kcp_magic = KZT_KMEM_TEST_MAGIC;
	kcp.kcp_file = file;
	kcp.kcp_count = 0;
	kcp.kcp_rc = 0;

	cache = kmem_cache_create(KZT_KMEM_CACHE_NAME,
	                          sizeof(kmem_cache_data_t), 0,
	                          kzt_kmem_test34_constructor,
	                          kzt_kmem_test34_destructor,
	                          kzt_kmem_test4_reclaim, &kcp, NULL, 0);
	if (!cache) {
		kzt_vprint(file, KZT_KMEM_TEST4_NAME,
	                   "Unable to create '%s'\n", KZT_KMEM_CACHE_NAME);
		return -ENOMEM;
	}

	kcp.kcp_cache = cache;

	for (i = 0; i < KZT_KMEM_OBJ_COUNT; i++) {
		/* All allocations need not succeed */
		kcp.kcp_kcd[i] = kmem_cache_alloc(cache, 0);
		if (!kcp.kcp_kcd[i]) {
			kzt_vprint(file, KZT_KMEM_TEST4_NAME,
		                   "Unable to allocate from '%s'\n",
			           KZT_KMEM_CACHE_NAME);
		}
	}

	max = kcp.kcp_count;

	/* Force shrinker to run */
	kmem_reap();

	/* Reclaim reclaimed objects, this ensure the destructors are run */
	kmem_cache_reap_now(cache);

	reclaim_percent = ((kcp.kcp_count * 100) / max);
	target_percent = (((KZT_KMEM_OBJ_COUNT - KZT_KMEM_OBJ_RECLAIM) * 100) /
	                    KZT_KMEM_OBJ_COUNT);
	kzt_vprint(file, KZT_KMEM_TEST4_NAME,
                   "%d%% (%d/%d) of previous size, target of "
	           "%d%%-%d%% for '%s'\n", reclaim_percent, kcp.kcp_count,
	           max, target_percent - 10, target_percent + 10,
	           KZT_KMEM_CACHE_NAME);
	if ((reclaim_percent < target_percent - 10) ||
	    (reclaim_percent > target_percent + 10))
		rc = -EINVAL;

	/* Cleanup our mess */
	for (i = 0; i < KZT_KMEM_OBJ_COUNT; i++)
		if (kcp.kcp_kcd[i])
			kmem_cache_free(cache, kcp.kcp_kcd[i]);

	kmem_cache_destroy(cache);

	return rc;
}

kzt_subsystem_t *
kzt_kmem_init(void)
{
        kzt_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, KZT_KMEM_NAME, KZT_NAME_SIZE);
	strncpy(sub->desc.desc, KZT_KMEM_DESC, KZT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = KZT_SUBSYSTEM_KMEM;

        KZT_TEST_INIT(sub, KZT_KMEM_TEST1_NAME, KZT_KMEM_TEST1_DESC,
	              KZT_KMEM_TEST1_ID, kzt_kmem_test1);
        KZT_TEST_INIT(sub, KZT_KMEM_TEST2_NAME, KZT_KMEM_TEST2_DESC,
	              KZT_KMEM_TEST2_ID, kzt_kmem_test2);
        KZT_TEST_INIT(sub, KZT_KMEM_TEST3_NAME, KZT_KMEM_TEST3_DESC,
	              KZT_KMEM_TEST3_ID, kzt_kmem_test3);
        KZT_TEST_INIT(sub, KZT_KMEM_TEST4_NAME, KZT_KMEM_TEST4_DESC,
	              KZT_KMEM_TEST4_ID, kzt_kmem_test4);

        return sub;
}

void
kzt_kmem_fini(kzt_subsystem_t *sub)
{
        ASSERT(sub);
        KZT_TEST_FINI(sub, KZT_KMEM_TEST4_ID);
        KZT_TEST_FINI(sub, KZT_KMEM_TEST3_ID);
        KZT_TEST_FINI(sub, KZT_KMEM_TEST2_ID);
        KZT_TEST_FINI(sub, KZT_KMEM_TEST1_ID);

        kfree(sub);
}

int
kzt_kmem_id(void) {
        return KZT_SUBSYSTEM_KMEM;
}
