#include <sys/kmem.h>

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

EXPORT_SYMBOL(kmem_alloc_used);
EXPORT_SYMBOL(kmem_alloc_max);
EXPORT_SYMBOL(vmem_alloc_used);
EXPORT_SYMBOL(vmem_alloc_max);
EXPORT_SYMBOL(kmem_warning_flag);

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
typedef struct kmem_cache_cb {
        struct list_head    kcc_list;
        kmem_cache_t *      kcc_cache;
        kmem_constructor_t  kcc_constructor;
        kmem_destructor_t   kcc_destructor;
        kmem_reclaim_t      kcc_reclaim;
        void *              kcc_private;
        void *              kcc_vmp;
} kmem_cache_cb_t;


static spinlock_t kmem_cache_cb_lock = SPIN_LOCK_UNLOCKED;
//static spinlock_t kmem_cache_cb_lock = (spinlock_t) { 1 SPINLOCK_MAGIC_INIT };
static LIST_HEAD(kmem_cache_cb_list);
static struct shrinker *kmem_cache_shrinker;

/* Function must be called while holding the kmem_cache_cb_lock
 * Because kmem_cache_t is an opaque datatype we're forced to
 * match pointers to identify specific cache entires.
 */
static kmem_cache_cb_t *
kmem_cache_find_cache_cb(kmem_cache_t *cache)
{
        kmem_cache_cb_t *kcc;

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
		kcc->kcc_cache = cache;
                kcc->kcc_constructor = constructor;
                kcc->kcc_destructor = destructor;
                kcc->kcc_reclaim = reclaim;
                kcc->kcc_private = priv;
                kcc->kcc_vmp = vmp;
		spin_lock(&kmem_cache_cb_lock);
                list_add(&kcc->kcc_list, &kmem_cache_cb_list);
		spin_unlock(&kmem_cache_cb_lock);
        }

        return kcc;
}

static void
kmem_cache_remove_cache_cb(kmem_cache_cb_t *kcc)
{
	spin_lock(&kmem_cache_cb_lock);
        list_del(&kcc->kcc_list);
	spin_unlock(&kmem_cache_cb_lock);

       if (kcc)
              kfree(kcc);
}

static void
kmem_cache_generic_constructor(void *ptr, kmem_cache_t *cache, unsigned long flags)
{
        kmem_cache_cb_t *kcc;

	spin_lock(&kmem_cache_cb_lock);

        /* Callback list must be in sync with linux slab caches */
        kcc = kmem_cache_find_cache_cb(cache);
        BUG_ON(!kcc);

	kcc->kcc_constructor(ptr, kcc->kcc_private, (int)flags);
	spin_unlock(&kmem_cache_cb_lock);
	/* Linux constructor has no return code, silently eat it */
}

static void
kmem_cache_generic_destructor(void *ptr, kmem_cache_t *cache, unsigned long flags)
{
        kmem_cache_cb_t *kcc;

	spin_lock(&kmem_cache_cb_lock);

        /* Callback list must be in sync with linux slab caches */
        kcc = kmem_cache_find_cache_cb(cache);
        BUG_ON(!kcc);

	/* Solaris destructor takes no flags, silently eat them */
	kcc->kcc_destructor(ptr, kcc->kcc_private);
	spin_unlock(&kmem_cache_cb_lock);
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
	spin_lock(&kmem_cache_cb_lock);

        list_for_each_entry(kcc, &kmem_cache_cb_list, kcc_list) {
	        /* Under linux the desired number and gfp type of objects
		 * is passed to the reclaiming function as a sugested reclaim
		 * target.  I do not pass these args on because reclaim
		 * policy is entirely up to the owner under solaris.  We only
		 * pass on the pre-registered private data.
                 */
		if (kcc->kcc_reclaim)
                        kcc->kcc_reclaim(kcc->kcc_private);

	        total += 1;
        }

	/* Under linux we should return the remaining number of entires in
	 * the cache.  Unfortunately, I don't see an easy way to safely
	 * emulate this behavior so I'm returning one entry per cache which
	 * was registered with the generic shrinker.  This should fake out
	 * the linux VM when it attempts to shrink caches.
	 */
	spin_unlock(&kmem_cache_cb_lock);
	return total;
}

/* Ensure the __kmem_cache_create/__kmem_cache_destroy macros are
 * removed here to prevent a recursive substitution, we want to call
 * the native linux version.
 */
#undef kmem_cache_create
#undef kmem_cache_destroy

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

        /* FIXME: - Option currently unsupported by shim layer */
        BUG_ON(vmp);

	cache_name = kzalloc(strlen(name) + 1, GFP_KERNEL);
	if (cache_name == NULL)
		return NULL;

	strcpy(cache_name, name);
        cache = kmem_cache_create(cache_name, size, align, flags,
                                  kmem_cache_generic_constructor,
                                  kmem_cache_generic_destructor);
	if (cache == NULL)
                return NULL;

        /* Register shared shrinker function on initial cache create */
	spin_lock(&kmem_cache_cb_lock);
	if (list_empty(&kmem_cache_cb_list)) {
                kmem_cache_shrinker = set_shrinker(KMC_DEFAULT_SEEKS,
                                                 kmem_cache_generic_shrinker);
                if (kmem_cache_shrinker == NULL) {
                        kmem_cache_destroy(cache);
			spin_unlock(&kmem_cache_cb_lock);
                        return NULL;
                }

        }
	spin_unlock(&kmem_cache_cb_lock);

        kcc = kmem_cache_add_cache_cb(cache, constructor, destructor,
                                      reclaim, priv, vmp);
        if (kcc == NULL) {
		if (shrinker_flag) /* New shrinker registered must be removed */
			remove_shrinker(kmem_cache_shrinker);

                kmem_cache_destroy(cache);
                return NULL;
        }

        return cache;
}
EXPORT_SYMBOL(__kmem_cache_create);

/* Return codes discarded because Solaris implementation has void return */
void
__kmem_cache_destroy(kmem_cache_t *cache)
{
        kmem_cache_cb_t *kcc;
	char *name;

	spin_lock(&kmem_cache_cb_lock);
        kcc = kmem_cache_find_cache_cb(cache);
	spin_unlock(&kmem_cache_cb_lock);
        if (kcc == NULL)
                return;

	name = (char *)kmem_cache_name(cache);
        kmem_cache_destroy(cache);
        kmem_cache_remove_cache_cb(kcc);
	kfree(name);

	/* Unregister generic shrinker on removal of all caches */
	spin_lock(&kmem_cache_cb_lock);
	if (list_empty(&kmem_cache_cb_list))
                remove_shrinker(kmem_cache_shrinker);

	spin_unlock(&kmem_cache_cb_lock);
}
EXPORT_SYMBOL(__kmem_cache_destroy);

void
__kmem_reap(void) {
	/* Since there's no easy hook in to linux to force all the registered
	 * shrinkers to run we just run the ones registered for this shim */
	kmem_cache_generic_shrinker(KMC_REAP_CHUNK, GFP_KERNEL);
}
EXPORT_SYMBOL(__kmem_reap);
