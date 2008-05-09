#include <sys/kmem.h>

#ifdef DEBUG_SUBSYSTEM
#undef DEBUG_SUBSYSTEM
#endif

#define DEBUG_SUBSYSTEM S_KMEM

/*
 * Memory allocation interfaces
 */
#ifdef DEBUG_KMEM
/* Shim layer memory accounting */
atomic64_t kmem_alloc_used;
unsigned long kmem_alloc_max = 0;
atomic64_t vmem_alloc_used;
unsigned long vmem_alloc_max = 0;
int kmem_warning_flag = 1;
atomic64_t kmem_cache_alloc_failed;

spinlock_t kmem_lock;
struct hlist_head kmem_table[KMEM_TABLE_SIZE];
struct list_head kmem_list;

spinlock_t vmem_lock;
struct hlist_head vmem_table[VMEM_TABLE_SIZE];
struct list_head vmem_list;

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);
EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);
EXPORT_SYMBOL(kmem_warning_flag);

EXPORT_SYMBOL(kmem_lock);
EXPORT_SYMBOL(kmem_table);
EXPORT_SYMBOL(kmem_list);

EXPORT_SYMBOL(vmem_lock);
EXPORT_SYMBOL(vmem_table);
EXPORT_SYMBOL(vmem_list);

int kmem_set_warning(int flag) { return (kmem_warning_flag = !!flag); }
#else
int kmem_set_warning(int flag) { return 0; }
#endif
EXPORT_SYMBOL(kmem_set_warning);

/*
 * Slab allocation interfaces
 *
 * While the linux slab implementation was inspired by solaris they
 * have made some changes to the API which complicates this shim
 * layer.  For one thing the same symbol names are used with different
 * arguments for the prototypes.  To deal with this we must use the
 * preprocessor to re-order arguments.  Happily for us standard C says,
 * "Macro's appearing in their own expansion are not reexpanded" so
 * this does not result in an infinite recursion.  Additionally the
 * function pointers registered by solarias differ from those used
 * by linux so a lookup and mapping from linux style callback to a
 * solaris style callback is needed.  There is some overhead in this
 * operation which isn't horibile but it needs to be kept in mind.
 */
#define KCC_MAGIC                0x7a7a7a7a
#define KCC_POISON               0x77

typedef struct kmem_cache_cb {
        int                 kcc_magic;
        struct list_head    kcc_list;
        kmem_cache_t *      kcc_cache;
        kmem_constructor_t  kcc_constructor;
        kmem_destructor_t   kcc_destructor;
        kmem_reclaim_t      kcc_reclaim;
        void *              kcc_private;
        void *              kcc_vmp;
	atomic_t            kcc_ref;
} kmem_cache_cb_t;

static struct rw_semaphore kmem_cache_cb_sem;
static struct list_head kmem_cache_cb_list;
static struct shrinker *kmem_cache_shrinker;

/* Function must be called while holding the kmem_cache_cb_sem
 * Because kmem_cache_t is an opaque datatype we're forced to
 * match pointers to identify specific cache entires.
 */
static kmem_cache_cb_t *
kmem_cache_find_cache_cb(kmem_cache_t *cache)
{
        kmem_cache_cb_t *kcc;
#ifdef CONFIG_RWSEM_GENERIC_SPINLOCK
        ASSERT(rwsem_is_locked(&kmem_cache_cb_sem));
#endif

        list_for_each_entry(kcc, &kmem_cache_cb_list, kcc_list)
		if (cache == kcc->kcc_cache)
                        return kcc;

        return NULL;
}

static kmem_cache_cb_t *
kmem_cache_add_cache_cb(kmem_cache_t *cache,
			kmem_constructor_t constructor,
                        kmem_destructor_t destructor,
			kmem_reclaim_t reclaim,
                        void *priv, void *vmp)
{
        kmem_cache_cb_t *kcc;

        kcc = (kmem_cache_cb_t *)kmalloc(sizeof(*kcc), GFP_KERNEL);
        if (kcc) {
		kcc->kcc_magic = KCC_MAGIC;
		kcc->kcc_cache = cache;
                kcc->kcc_constructor = constructor;
                kcc->kcc_destructor = destructor;
                kcc->kcc_reclaim = reclaim;
                kcc->kcc_private = priv;
                kcc->kcc_vmp = vmp;
		atomic_set(&kcc->kcc_ref, 0);
		down_write(&kmem_cache_cb_sem);
                list_add(&kcc->kcc_list, &kmem_cache_cb_list);
		up_write(&kmem_cache_cb_sem);
        }

        return kcc;
}

static void
kmem_cache_remove_cache_cb(kmem_cache_cb_t *kcc)
{
        down_write(&kmem_cache_cb_sem);
	ASSERT(atomic_read(&kcc->kcc_ref) == 0);
        list_del(&kcc->kcc_list);
        up_write(&kmem_cache_cb_sem);

        if (kcc){
		memset(kcc, KCC_POISON, sizeof(*kcc));
                kfree(kcc);
	}
}

static void
kmem_cache_generic_constructor(void *ptr, kmem_cache_t *cache, unsigned long flags)
{
        kmem_cache_cb_t *kcc;
	kmem_constructor_t constructor;
	void *private;

	/* Ensure constructor verifies are not passed to the registered
	 * constructors.  This may not be safe due to the Solaris constructor
	 * not being aware of how to handle the SLAB_CTOR_VERIFY flag
	 */
	if (flags & SLAB_CTOR_VERIFY)
		return;

	/* We can be called with interrupts disabled so it is critical that
	 * this function and the registered constructor never sleep.
	 */
        while (!down_read_trylock(&kmem_cache_cb_sem));

        /* Callback list must be in sync with linux slab caches */
        kcc = kmem_cache_find_cache_cb(cache);
        ASSERT(kcc);
	ASSERT(kcc->kcc_magic == KCC_MAGIC);
	atomic_inc(&kcc->kcc_ref);

	constructor = kcc->kcc_constructor;
	private = kcc->kcc_private;

        up_read(&kmem_cache_cb_sem);

	if (constructor)
		constructor(ptr, private, (int)flags);

	atomic_dec(&kcc->kcc_ref);

	/* Linux constructor has no return code, silently eat it */
}

static void
kmem_cache_generic_destructor(void *ptr, kmem_cache_t *cache, unsigned long flags)
{
        kmem_cache_cb_t *kcc;
        kmem_destructor_t destructor;
	void *private;

	/* We can be called with interrupts disabled so it is critical that
	 * this function and the registered constructor never sleep.
	 */
        while (!down_read_trylock(&kmem_cache_cb_sem));

        /* Callback list must be in sync with linux slab caches */
        kcc = kmem_cache_find_cache_cb(cache);
	ASSERT(kcc);
	ASSERT(kcc->kcc_magic == KCC_MAGIC);
	atomic_inc(&kcc->kcc_ref);

	destructor = kcc->kcc_destructor;
	private = kcc->kcc_private;

        up_read(&kmem_cache_cb_sem);

	/* Solaris destructor takes no flags, silently eat them */
	if (destructor)
		destructor(ptr, private);

	atomic_dec(&kcc->kcc_ref);
}

/* XXX - Arguments are ignored */
static int
kmem_cache_generic_shrinker(int nr_to_scan, unsigned int gfp_mask)
{
        kmem_cache_cb_t *kcc;
        int total = 0;

	/* Under linux a shrinker is not tightly coupled with a slab
	 * cache.  In fact linux always systematically trys calling all
	 * registered shrinker callbacks until its target reclamation level
	 * is reached.  Because of this we only register one shrinker
	 * function in the shim layer for all slab caches.  And we always
	 * attempt to shrink all caches when this generic shrinker is called.
	 */
        down_read(&kmem_cache_cb_sem);

        list_for_each_entry(kcc, &kmem_cache_cb_list, kcc_list) {
	        ASSERT(kcc);
                ASSERT(kcc->kcc_magic == KCC_MAGIC);

		/* Take a reference on the cache in question.  If that
		 * cache is contended simply skip it, it may already be
		 * in the process of a reclaim or the ctor/dtor may be
		 * running in either case it's best to skip it.
		 */
	        atomic_inc(&kcc->kcc_ref);
		if (atomic_read(&kcc->kcc_ref) > 1) {
	                atomic_dec(&kcc->kcc_ref);
			continue;
		}

	        /* Under linux the desired number and gfp type of objects
		 * is passed to the reclaiming function as a sugested reclaim
		 * target.  I do not pass these args on because reclaim
		 * policy is entirely up to the owner under solaris.  We only
		 * pass on the pre-registered private data.
                 */
		if (kcc->kcc_reclaim)
                        kcc->kcc_reclaim(kcc->kcc_private);

	        atomic_dec(&kcc->kcc_ref);
	        total += 1;
        }

	/* Under linux we should return the remaining number of entires in
	 * the cache.  Unfortunately, I don't see an easy way to safely
	 * emulate this behavior so I'm returning one entry per cache which
	 * was registered with the generic shrinker.  This should fake out
	 * the linux VM when it attempts to shrink caches.
	 */
        up_read(&kmem_cache_cb_sem);

	return total;
}

/* Ensure the __kmem_cache_create/__kmem_cache_destroy macros are
 * removed here to prevent a recursive substitution, we want to call
 * the native linux version.
 */
#undef kmem_cache_create
#undef kmem_cache_destroy
#undef kmem_cache_alloc

kmem_cache_t *
__kmem_cache_create(char *name, size_t size, size_t align,
        kmem_constructor_t constructor,
	kmem_destructor_t destructor,
	kmem_reclaim_t reclaim,
        void *priv, void *vmp, int flags)
{
        kmem_cache_t *cache;
        kmem_cache_cb_t *kcc;
	int shrinker_flag = 0;
	char *cache_name;
	ENTRY;

        /* XXX: - Option currently unsupported by shim layer */
        ASSERT(!vmp);

	cache_name = kzalloc(strlen(name) + 1, GFP_KERNEL);
	if (cache_name == NULL)
		RETURN(NULL);

	strcpy(cache_name, name);
        cache = kmem_cache_create(cache_name, size, align, flags,
                                  kmem_cache_generic_constructor,
                                  kmem_cache_generic_destructor);
	if (cache == NULL)
                RETURN(NULL);

        /* Register shared shrinker function on initial cache create */
        down_read(&kmem_cache_cb_sem);
	if (list_empty(&kmem_cache_cb_list)) {
                kmem_cache_shrinker = set_shrinker(KMC_DEFAULT_SEEKS,
                                                 kmem_cache_generic_shrinker);
                if (kmem_cache_shrinker == NULL) {
                        kmem_cache_destroy(cache);
                        up_read(&kmem_cache_cb_sem);
                        RETURN(NULL);
                }

        }
        up_read(&kmem_cache_cb_sem);

        kcc = kmem_cache_add_cache_cb(cache, constructor, destructor,
                                      reclaim, priv, vmp);
        if (kcc == NULL) {
		if (shrinker_flag) /* New shrinker registered must be removed */
			remove_shrinker(kmem_cache_shrinker);

                kmem_cache_destroy(cache);
                RETURN(NULL);
        }

        RETURN(cache);
}
EXPORT_SYMBOL(__kmem_cache_create);

/* Return code provided despite Solaris's void return.  There should be no
 * harm here since the Solaris versions will ignore it anyway. */
int
__kmem_cache_destroy(kmem_cache_t *cache)
{
        kmem_cache_cb_t *kcc;
	char *name;
	int rc;
	ENTRY;

        down_read(&kmem_cache_cb_sem);
        kcc = kmem_cache_find_cache_cb(cache);
        if (kcc == NULL) {
                up_read(&kmem_cache_cb_sem);
                RETURN(-EINVAL);
        }
	atomic_inc(&kcc->kcc_ref);
        up_read(&kmem_cache_cb_sem);

	name = (char *)kmem_cache_name(cache);
        rc = kmem_cache_destroy(cache);

	atomic_dec(&kcc->kcc_ref);
        kmem_cache_remove_cache_cb(kcc);
	kfree(name);

	/* Unregister generic shrinker on removal of all caches */
        down_read(&kmem_cache_cb_sem);
	if (list_empty(&kmem_cache_cb_list))
                remove_shrinker(kmem_cache_shrinker);

        up_read(&kmem_cache_cb_sem);
	RETURN(rc);
}
EXPORT_SYMBOL(__kmem_cache_destroy);

/* Under Solaris if the KM_SLEEP flag is passed we absolutely must
 * sleep until we are allocated the memory.  Under Linux you can still
 * get a memory allocation failure, so I'm forced to keep requesting
 * the memory even if the system is under substantial memory pressure
 * of fragmentation prevents the allocation from succeeded.  This is
 * not the correct fix, or even a good one.  But it will do for now.
 */
void *
__kmem_cache_alloc(kmem_cache_t *cache, gfp_t flags)
{
	void *rc;
	ENTRY;

restart:
	rc = kmem_cache_alloc(cache, flags);
        if ((rc == NULL) && (flags & KM_SLEEP)) {
		atomic64_inc(&kmem_cache_alloc_failed);
		GOTO(restart, rc);
	}

	RETURN(rc);
}
EXPORT_SYMBOL(__kmem_cache_alloc);

void
__kmem_reap(void)
{
	ENTRY;
	/* Since there's no easy hook in to linux to force all the registered
	 * shrinkers to run we just run the ones registered for this shim */
	kmem_cache_generic_shrinker(KMC_REAP_CHUNK, GFP_KERNEL);
	EXIT;
}
EXPORT_SYMBOL(__kmem_reap);

int
kmem_init(void)
{
        ENTRY;

	init_rwsem(&kmem_cache_cb_sem);
        INIT_LIST_HEAD(&kmem_cache_cb_list);
#ifdef DEBUG_KMEM
        {
                int i;
		atomic64_set(&kmem_alloc_used, 0);
		atomic64_set(&vmem_alloc_used, 0);

                spin_lock_init(&kmem_lock);
                INIT_LIST_HEAD(&kmem_list);

                for (i = 0; i < KMEM_TABLE_SIZE; i++)
                        INIT_HLIST_HEAD(&kmem_table[i]);

                spin_lock_init(&vmem_lock);
                INIT_LIST_HEAD(&vmem_list);

                for (i = 0; i < VMEM_TABLE_SIZE; i++)
                        INIT_HLIST_HEAD(&vmem_table[i]);

		atomic64_set(&kmem_cache_alloc_failed, 0);
        }
#endif
	RETURN(0);
}

static char *sprintf_addr(kmem_debug_t *kd, char *str, int len, int min)
{
        int size = ((len - 1) < kd->kd_size) ? (len - 1) : kd->kd_size;
	int i, flag = 1;

	ASSERT(str != NULL && len >= 17);
        memset(str, 0, len);

	/* Check for a fully printable string, and while we are at
         * it place the printable characters in the passed buffer. */
	for (i = 0; i < size; i++) {
                str[i] = ((char *)(kd->kd_addr))[i];
                if (isprint(str[i])) {
                        continue;
                } else {
                        /* Minimum number of printable characters found
                         * to make it worthwhile to print this as ascii. */
                        if (i > min)
                                break;

                         flag = 0;
                         break;
                }

	}

	if (!flag) {
		sprintf(str, "%02x%02x%02x%02x%02x%02x%02x%02x",
		        *((uint8_t *)kd->kd_addr),
		        *((uint8_t *)kd->kd_addr + 2),
		        *((uint8_t *)kd->kd_addr + 4),
		        *((uint8_t *)kd->kd_addr + 6),
		        *((uint8_t *)kd->kd_addr + 8),
		        *((uint8_t *)kd->kd_addr + 10),
		        *((uint8_t *)kd->kd_addr + 12),
		        *((uint8_t *)kd->kd_addr + 14));
	}

	return str;
}

void
kmem_fini(void)
{
	ENTRY;
#ifdef DEBUG_KMEM
        {
                unsigned long flags;
                kmem_debug_t *kd;
		char str[17];

		/* Display all unreclaimed memory addresses, including the
		 * allocation size and the first few bytes of what's located
		 * at that address to aid in debugging.  Performance is not
		 * a serious concern here since it is module unload time. */
                if (atomic64_read(&kmem_alloc_used) != 0)
                        CWARN("kmem leaked %ld/%ld bytes\n",
                               atomic_read(&kmem_alloc_used), kmem_alloc_max);

                spin_lock_irqsave(&kmem_lock, flags);
                if (!list_empty(&kmem_list))
                        CDEBUG(D_WARNING, "%-16s %-5s %-16s %s:%s\n",
			       "address", "size", "data", "func", "line");

                list_for_each_entry(kd, &kmem_list, kd_list)
                        CDEBUG(D_WARNING, "%p %-5d %-16s %s:%d\n",
			       kd->kd_addr, kd->kd_size,
                               sprintf_addr(kd, str, 17, 8),
			       kd->kd_func, kd->kd_line);

                spin_unlock_irqrestore(&kmem_lock, flags);

                if (atomic64_read(&vmem_alloc_used) != 0)
                        CWARN("vmem leaked %ld/%ld bytes\n",
                               atomic_read(&vmem_alloc_used), vmem_alloc_max);

                spin_lock_irqsave(&vmem_lock, flags);
                if (!list_empty(&vmem_list))
                        CDEBUG(D_WARNING, "%-16s %-5s %-16s %s:%s\n",
			       "address", "size", "data", "func", "line");

                list_for_each_entry(kd, &vmem_list, kd_list)
                        CDEBUG(D_WARNING, "%p %-5d %-16s %s:%d\n",
			       kd->kd_addr, kd->kd_size,
                               sprintf_addr(kd, str, 17, 8),
			       kd->kd_func, kd->kd_line);

                spin_unlock_irqrestore(&vmem_lock, flags);
        }
#endif
	EXIT;
}
