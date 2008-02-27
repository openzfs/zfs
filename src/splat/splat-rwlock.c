#include <splat-ctl.h>

#define KZT_SUBSYSTEM_RWLOCK		0x0700
#define KZT_RWLOCK_NAME			"rwlock"
#define KZT_RWLOCK_DESC			"Kernel RW Lock Tests"

#define KZT_RWLOCK_TEST1_ID		0x0701
#define KZT_RWLOCK_TEST1_NAME		"rwtest1"
#define KZT_RWLOCK_TEST1_DESC		"Multiple Readers One Writer"

#define KZT_RWLOCK_TEST2_ID		0x0702
#define KZT_RWLOCK_TEST2_NAME		"rwtest2"
#define KZT_RWLOCK_TEST2_DESC		"Multiple Writers"

#define KZT_RWLOCK_TEST3_ID		0x0703
#define KZT_RWLOCK_TEST3_NAME		"rwtest3"
#define KZT_RWLOCK_TEST3_DESC		"Owner Verification"

#define KZT_RWLOCK_TEST4_ID		0x0704
#define KZT_RWLOCK_TEST4_NAME		"rwtest4"
#define KZT_RWLOCK_TEST4_DESC		"Trylock Test"

#define KZT_RWLOCK_TEST5_ID		0x0705
#define KZT_RWLOCK_TEST5_NAME		"rwtest5"
#define KZT_RWLOCK_TEST5_DESC		"Write Downgrade Test"

#define KZT_RWLOCK_TEST6_ID		0x0706
#define KZT_RWLOCK_TEST6_NAME		"rwtest6"
#define KZT_RWLOCK_TEST6_DESC		"Read Upgrade Test"

#define KZT_RWLOCK_TEST_MAGIC		0x115599DDUL
#define KZT_RWLOCK_TEST_NAME		"rwlock_test"
#define KZT_RWLOCK_TEST_COUNT		8

#define KZT_RWLOCK_RELEASE_INIT		0
#define KZT_RWLOCK_RELEASE_WRITERS	1
#define KZT_RWLOCK_RELEASE_READERS	2

typedef struct rw_priv {
        unsigned long rw_magic;
        struct file *rw_file;
	krwlock_t rwl;
	spinlock_t rw_priv_lock;
	wait_queue_head_t rw_waitq;
	atomic_t rw_completed;
	atomic_t rw_acquired;
	atomic_t rw_waiters;
	atomic_t rw_release;
} rw_priv_t;

typedef struct rw_thr {
	int rwt_id;
	const char *rwt_name;
	rw_priv_t *rwt_rwp;
	int rwt_rc;
} rw_thr_t;

static inline void
kzt_rwlock_sleep(signed long delay)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(delay);
}

#define kzt_rwlock_lock_and_test(lock,test)	\
({						\
	int ret = 0;				\
						\
	spin_lock(lock);			\
	ret = (test) ? 1 : 0;			\
	spin_unlock(lock);			\
	ret;					\
})

void kzt_init_rw_priv(rw_priv_t *rwv, struct file *file)
{
	rwv->rw_magic = KZT_RWLOCK_TEST_MAGIC;
	rwv->rw_file = file;
	spin_lock_init(&rwv->rw_priv_lock);
	init_waitqueue_head(&rwv->rw_waitq);
	atomic_set(&rwv->rw_completed, 0);
	atomic_set(&rwv->rw_acquired, 0);
	atomic_set(&rwv->rw_waiters, 0);
	atomic_set(&rwv->rw_release, KZT_RWLOCK_RELEASE_INIT);
	
	/* Initialize the read/write lock */
	rw_init(&rwv->rwl, KZT_RWLOCK_TEST_NAME, RW_DEFAULT, NULL);
}

int
kzt_rwlock_test1_writer_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == KZT_RWLOCK_TEST_MAGIC);
        snprintf(name, sizeof(name), "%s%d", 
		 KZT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
	kzt_rwlock_sleep(rnd * HZ / 1000);

	spin_lock(&rwv->rw_priv_lock);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread trying to acquire rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	atomic_inc(&rwv->rw_waiters);
	spin_unlock(&rwv->rw_priv_lock);

	/* Take the semaphore for writing 
	 * release it when we are told to */
	rw_enter(&rwv->rwl, RW_WRITER);

	spin_lock(&rwv->rw_priv_lock);
	atomic_dec(&rwv->rw_waiters);
	atomic_inc(&rwv->rw_acquired);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread acquired rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Wait here until the control thread
	 * says we can release the write lock */
	wait_event_interruptible(rwv->rw_waitq,
				 kzt_rwlock_lock_and_test(&rwv->rw_priv_lock,
					 atomic_read(&rwv->rw_release) ==
					 KZT_RWLOCK_RELEASE_WRITERS));
	spin_lock(&rwv->rw_priv_lock);
	atomic_inc(&rwv->rw_completed);
	atomic_dec(&rwv->rw_acquired);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread dropped rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Release the semaphore */
	rw_exit(&rwv->rwl);
	return 0;
}

int
kzt_rwlock_test1_reader_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == KZT_RWLOCK_TEST_MAGIC);
        snprintf(name, sizeof(name), "%s%d",
		 KZT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
        kzt_rwlock_sleep(rnd * HZ / 1000);

	/* Don't try and and take the semaphore until
	 * someone else has already acquired it */
        wait_event_interruptible(rwv->rw_waitq,
				 kzt_rwlock_lock_and_test(&rwv->rw_priv_lock,
					 atomic_read(&rwv->rw_acquired) > 0));

	spin_lock(&rwv->rw_priv_lock);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s reader thread trying to acquire rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	atomic_inc(&rwv->rw_waiters);
	spin_unlock(&rwv->rw_priv_lock);

	/* Take the semaphore for reading
	 * release it when we are told to */
	rw_enter(&rwv->rwl, RW_READER);

	spin_lock(&rwv->rw_priv_lock);
	atomic_dec(&rwv->rw_waiters);
	atomic_inc(&rwv->rw_acquired);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s reader thread acquired rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Wait here until the control thread
         * says we can release the read lock */
	wait_event_interruptible(rwv->rw_waitq,
				 kzt_rwlock_lock_and_test(&rwv->rw_priv_lock,
				 atomic_read(&rwv->rw_release) ==
				 KZT_RWLOCK_RELEASE_READERS));

	spin_lock(&rwv->rw_priv_lock);
	atomic_inc(&rwv->rw_completed);
	atomic_dec(&rwv->rw_acquired);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s reader thread dropped rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Release the semaphore */
	rw_exit(&rwv->rwl);
	return 0;
}

static int
kzt_rwlock_test1(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	long pids[KZT_RWLOCK_TEST_COUNT];
	rw_thr_t rwt[KZT_RWLOCK_TEST_COUNT];
	rw_priv_t rwv;

	/* Initialize private data 
	 * including the rwlock */
	kzt_init_rw_priv(&rwv, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < KZT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = &rwv;
		rwt[i].rwt_id = i;
		rwt[i].rwt_name = KZT_RWLOCK_TEST1_NAME;
		rwt[i].rwt_rc = 0;

		/* The first thread will be a writer */
		if (i == 0) {
			pids[i] = kernel_thread(kzt_rwlock_test1_writer_thread,
						&rwt[i], 0);
		} else {
			pids[i] = kernel_thread(kzt_rwlock_test1_reader_thread,
						&rwt[i], 0);
		}
		
		if (pids[i] >= 0) {
			count++;
		}
	}

	/* Once the writer has the lock, release the readers */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock, atomic_read(&rwv.rw_acquired) <= 0)) {
		kzt_rwlock_sleep(1 * HZ);
	}
	wake_up_interruptible(&rwv.rw_waitq);

	/* Ensure that there is only 1 writer and all readers are waiting */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock, 
				        atomic_read(&rwv.rw_acquired) != 1 ||
					atomic_read(&rwv.rw_waiters) !=
					KZT_RWLOCK_TEST_COUNT - 1)) {

		kzt_rwlock_sleep(1 * HZ);
	}
	/* Relase the writer */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, KZT_RWLOCK_RELEASE_WRITERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Now ensure that there are multiple reader threads holding the lock */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) <= 1)) {
		kzt_rwlock_sleep(1 * HZ);
	}
	/* Release the readers */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, KZT_RWLOCK_RELEASE_READERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Wait for the test to complete */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) != 0 ||
	       atomic_read(&rwv.rw_waiters) != 0)) {
		kzt_rwlock_sleep(1 * HZ);

	}

	rw_destroy(&rwv.rwl);
	return rc;
}

int
kzt_rwlock_test2_writer_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];
	
	ASSERT(rwv->rw_magic == KZT_RWLOCK_TEST_MAGIC);
	snprintf(name, sizeof(name), "%s%d",
		 KZT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
	kzt_rwlock_sleep(rnd * HZ / 1000);

	/* Here just increment the waiters count even if we are not
	 * exactly about to call rw_enter().  Not really a big deal
	 * since more than likely will be true when we simulate work
	 * later on */
	spin_lock(&rwv->rw_priv_lock);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread trying to acquire rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	atomic_inc(&rwv->rw_waiters);
	spin_unlock(&rwv->rw_priv_lock);

	/* Wait here until the control thread
	 * says we can acquire the write lock */
	wait_event_interruptible(rwv->rw_waitq,
				 kzt_rwlock_lock_and_test(&rwv->rw_priv_lock,
				 atomic_read(&rwv->rw_release) ==
				 KZT_RWLOCK_RELEASE_WRITERS));
	
	/* Take the semaphore for writing */
	rw_enter(&rwv->rwl, RW_WRITER);

	spin_lock(&rwv->rw_priv_lock);
	atomic_dec(&rwv->rw_waiters);
	atomic_inc(&rwv->rw_acquired);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread acquired rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Give up the processor for a bit to simulate
	 * doing some work while taking the write lock */
	kzt_rwlock_sleep(rnd * HZ / 1000);

	/* Ensure that we are the only one writing */
	if (atomic_read(&rwv->rw_acquired) > 1) {
		rwt->rwt_rc = 1;
	} else {
		rwt->rwt_rc = 0;
	}

	spin_lock(&rwv->rw_priv_lock);
	atomic_inc(&rwv->rw_completed);
	atomic_dec(&rwv->rw_acquired);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread dropped rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	rw_exit(&rwv->rwl);
	

	return 0;
}

static int
kzt_rwlock_test2(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	long pids[KZT_RWLOCK_TEST_COUNT];
	rw_thr_t rwt[KZT_RWLOCK_TEST_COUNT];
	rw_priv_t rwv;

	/* Initialize private data 
	 * including the rwlock */
	kzt_init_rw_priv(&rwv, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < KZT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = &rwv;
		rwt[i].rwt_id = i;
		rwt[i].rwt_name = KZT_RWLOCK_TEST2_NAME;
		rwt[i].rwt_rc = 0;

		/* The first thread will be a writer */
		pids[i] = kernel_thread(kzt_rwlock_test2_writer_thread,
					&rwt[i], 0);

		if (pids[i] >= 0) {
			count++;
		}
	}

	/* Wait for writers to get queued up */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_waiters) < KZT_RWLOCK_TEST_COUNT)) {
		kzt_rwlock_sleep(1 * HZ);
	}
	/* Relase the writers */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, KZT_RWLOCK_RELEASE_WRITERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Wait for the test to complete */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) != 0 ||
	       atomic_read(&rwv.rw_waiters) != 0)) {
		kzt_rwlock_sleep(1 * HZ);
	}

	/* If any of the write threads ever acquired the lock
	 * while another thread had it, make sure we return
	 * an error */
	for (i = 0; i < KZT_RWLOCK_TEST_COUNT; i++) {
		if (rwt[i].rwt_rc) {
			rc++;
		}
	}

	rw_destroy(&rwv.rwl);
	return rc;
}

static int
kzt_rwlock_test3(struct file *file, void *arg)
{
	kthread_t *owner;
	rw_priv_t rwv;
	int rc = 0;

	/* Initialize private data 
	 * including the rwlock */
	kzt_init_rw_priv(&rwv, file);

	/* Take the rwlock for writing */
	rw_enter(&rwv.rwl, RW_WRITER);
	owner = rw_owner(&rwv.rwl);
	if (current != owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST3_NAME, "rwlock should "
			   "be owned by pid %d but is owned by pid %d\n",
			   current->pid, owner ? owner->pid : -1);
		rc = -EINVAL;
		goto out;
	}

	/* Release the rwlock */
	rw_exit(&rwv.rwl);
	owner = rw_owner(&rwv.rwl);
	if (owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST3_NAME, "rwlock should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		rc = -EINVAL;
		goto out;
	}

	/* Take the rwlock for reading.
	 * Should not have an owner */
	rw_enter(&rwv.rwl, RW_READER);
	owner = rw_owner(&rwv.rwl);
	if (owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST3_NAME, "rwlock should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		/* Release the rwlock */
		rw_exit(&rwv.rwl);
		rc = -EINVAL;
		goto out;
	}

	/* Release the rwlock */
	rw_exit(&rwv.rwl);

out:
	rw_destroy(&rwv.rwl);
	return rc;
}

int
kzt_rwlock_test4_reader_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == KZT_RWLOCK_TEST_MAGIC);
        snprintf(name, sizeof(name), "%s%d",
		 KZT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
        kzt_rwlock_sleep(rnd * HZ / 1000);

	/* Don't try and and take the semaphore until
	 * someone else has already acquired it */
        wait_event_interruptible(rwv->rw_waitq,
				 kzt_rwlock_lock_and_test(&rwv->rw_priv_lock,
				 atomic_read(&rwv->rw_acquired) > 0));

	spin_lock(&rwv->rw_priv_lock);
	kzt_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s reader thread trying to acquire rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Take the semaphore for reading
	 * release it when we are told to */
	rwt->rwt_rc = rw_tryenter(&rwv->rwl, RW_READER);

	/* Here we acquired the lock this is a
	 * failure since the writer should be
	 * holding the lock */
	if (rwt->rwt_rc == 1) {
		spin_lock(&rwv->rw_priv_lock);
		atomic_inc(&rwv->rw_acquired);
		kzt_vprint(rwv->rw_file, rwt->rwt_name,
			   "%s reader thread acquired rwlock with "
			   "%d holding lock and %d waiting\n",
			   name, atomic_read(&rwv->rw_acquired),
			   atomic_read(&rwv->rw_waiters));
		spin_unlock(&rwv->rw_priv_lock);
		
		spin_lock(&rwv->rw_priv_lock);
		atomic_dec(&rwv->rw_acquired);
		kzt_vprint(rwv->rw_file, rwt->rwt_name,
			   "%s reader thread dropped rwlock with "
			   "%d holding lock and %d waiting\n",
			   name, atomic_read(&rwv->rw_acquired),
			   atomic_read(&rwv->rw_waiters));
		spin_unlock(&rwv->rw_priv_lock);
		
		/* Release the semaphore */
		rw_exit(&rwv->rwl);
	}
	/* Here we know we didn't block and didn't
	 * acquire the rwlock for reading */
	else {
		spin_lock(&rwv->rw_priv_lock);
		atomic_inc(&rwv->rw_completed);
		kzt_vprint(rwv->rw_file, rwt->rwt_name,
			   "%s reader thread could not acquire rwlock with "
			   "%d holding lock and %d waiting\n",
			   name, atomic_read(&rwv->rw_acquired),
			   atomic_read(&rwv->rw_waiters));
		spin_unlock(&rwv->rw_priv_lock);
	}

	return 0;
}

static int
kzt_rwlock_test4(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	long pids[KZT_RWLOCK_TEST_COUNT];
	rw_thr_t rwt[KZT_RWLOCK_TEST_COUNT];
	rw_priv_t rwv;

	/* Initialize private data 
	 * including the rwlock */
	kzt_init_rw_priv(&rwv, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < KZT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = &rwv;
		rwt[i].rwt_id = i;
		rwt[i].rwt_name = KZT_RWLOCK_TEST4_NAME;
		rwt[i].rwt_rc = 0;

		/* The first thread will be a writer */
		if (i == 0) {
			/* We can reuse the test1 writer thread here */
			pids[i] = kernel_thread(kzt_rwlock_test1_writer_thread,
						&rwt[i], 0);
		} else {
			 pids[i] = kernel_thread(kzt_rwlock_test4_reader_thread,
						&rwt[i], 0);
		}

		if (pids[i] >= 0) {
			count++;
		}
	}

	/* Once the writer has the lock, release the readers */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) <= 0)) {
		kzt_rwlock_sleep(1 * HZ);
	}
	wake_up_interruptible(&rwv.rw_waitq);

	/* Make sure that the reader threads complete */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_completed) != KZT_RWLOCK_TEST_COUNT - 1)) {
		kzt_rwlock_sleep(1 * HZ);
	}
	/* Release the writer */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, KZT_RWLOCK_RELEASE_WRITERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Wait for the test to complete */
	while (kzt_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) != 0 ||
	       atomic_read(&rwv.rw_waiters) != 0)) {
		kzt_rwlock_sleep(1 * HZ);
	}

	/* If any of the reader threads ever acquired the lock
	 * while another thread had it, make sure we return
	 * an error since the rw_tryenter() should have failed */
	for (i = 0; i < KZT_RWLOCK_TEST_COUNT; i++) {
		if (rwt[i].rwt_rc) {
			rc++;
		}
	}

	rw_destroy(&rwv.rwl);
	return rc;
}

static int
kzt_rwlock_test5(struct file *file, void *arg)
{
	kthread_t *owner;
	rw_priv_t rwv;
	int rc = 0;

	/* Initialize private data 
	 * including the rwlock */
	kzt_init_rw_priv(&rwv, file);

	/* Take the rwlock for writing */
	rw_enter(&rwv.rwl, RW_WRITER);
	owner = rw_owner(&rwv.rwl);
	if (current != owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST5_NAME, "rwlock should "
			   "be owned by pid %d but is owned by pid %d\n",
			   current->pid, owner ? owner->pid : -1);
		rc = -EINVAL;
		goto out;
	}

	/* Make sure that the downgrade
	 * worked properly */
	rw_downgrade(&rwv.rwl);

	owner = rw_owner(&rwv.rwl);
	if (owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST5_NAME, "rwlock should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		/* Release the rwlock */
		rw_exit(&rwv.rwl);
		rc = -EINVAL;
		goto out;
	}

	/* Release the rwlock */
	rw_exit(&rwv.rwl);

out:
	rw_destroy(&rwv.rwl);
	return rc;
}

static int
kzt_rwlock_test6(struct file *file, void *arg)
{
	kthread_t *owner;
	rw_priv_t rwv;
	int rc = 0;

	/* Initialize private data 
	 * including the rwlock */
	kzt_init_rw_priv(&rwv, file);

	/* Take the rwlock for reading */
	rw_enter(&rwv.rwl, RW_READER);
	owner = rw_owner(&rwv.rwl);
	if (owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST6_NAME, "rwlock should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		rc = -EINVAL;
		goto out;
	}

	/* Make sure that the upgrade
	 * worked properly */
	rc = !rw_tryupgrade(&rwv.rwl);

	owner = rw_owner(&rwv.rwl);
	if (rc || current != owner) {
		kzt_vprint(file, KZT_RWLOCK_TEST6_NAME, "rwlock should "
			   "be owned by pid %d but is owned by pid %d "
			   "trylock rc %d\n",
			   current->pid, owner ? owner->pid : -1, rc);
		rc = -EINVAL;
		goto out;
	}

	/* Release the rwlock */
	rw_exit(&rwv.rwl);

out:
	rw_destroy(&rwv.rwl);
	return rc;
}

kzt_subsystem_t *
kzt_rwlock_init(void)
{
        kzt_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, KZT_RWLOCK_NAME, KZT_NAME_SIZE);
        strncpy(sub->desc.desc, KZT_RWLOCK_DESC, KZT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = KZT_SUBSYSTEM_RWLOCK;

        KZT_TEST_INIT(sub, KZT_RWLOCK_TEST1_NAME, KZT_RWLOCK_TEST1_DESC,
                      KZT_RWLOCK_TEST1_ID, kzt_rwlock_test1);
        KZT_TEST_INIT(sub, KZT_RWLOCK_TEST2_NAME, KZT_RWLOCK_TEST2_DESC,
                      KZT_RWLOCK_TEST2_ID, kzt_rwlock_test2);
        KZT_TEST_INIT(sub, KZT_RWLOCK_TEST3_NAME, KZT_RWLOCK_TEST3_DESC,
                      KZT_RWLOCK_TEST3_ID, kzt_rwlock_test3);
        KZT_TEST_INIT(sub, KZT_RWLOCK_TEST4_NAME, KZT_RWLOCK_TEST4_DESC,
                      KZT_RWLOCK_TEST4_ID, kzt_rwlock_test4);
        KZT_TEST_INIT(sub, KZT_RWLOCK_TEST5_NAME, KZT_RWLOCK_TEST5_DESC,
                      KZT_RWLOCK_TEST5_ID, kzt_rwlock_test5);
        KZT_TEST_INIT(sub, KZT_RWLOCK_TEST6_NAME, KZT_RWLOCK_TEST6_DESC,
                      KZT_RWLOCK_TEST6_ID, kzt_rwlock_test6);

        return sub;
}

void
kzt_rwlock_fini(kzt_subsystem_t *sub)
{
        ASSERT(sub);
        KZT_TEST_FINI(sub, KZT_RWLOCK_TEST6_ID);
        KZT_TEST_FINI(sub, KZT_RWLOCK_TEST5_ID);
        KZT_TEST_FINI(sub, KZT_RWLOCK_TEST4_ID);
        KZT_TEST_FINI(sub, KZT_RWLOCK_TEST3_ID);
        KZT_TEST_FINI(sub, KZT_RWLOCK_TEST2_ID);
        KZT_TEST_FINI(sub, KZT_RWLOCK_TEST1_ID);
        kfree(sub);
}

int
kzt_rwlock_id(void) {
        return KZT_SUBSYSTEM_RWLOCK;
}
