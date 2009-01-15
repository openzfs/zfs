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

#define SPLAT_SUBSYSTEM_RWLOCK		0x0700
#define SPLAT_RWLOCK_NAME		"rwlock"
#define SPLAT_RWLOCK_DESC		"Kernel RW Lock Tests"

#define SPLAT_RWLOCK_TEST1_ID		0x0701
#define SPLAT_RWLOCK_TEST1_NAME		"rwtest1"
#define SPLAT_RWLOCK_TEST1_DESC		"Multiple Readers One Writer"

#define SPLAT_RWLOCK_TEST2_ID		0x0702
#define SPLAT_RWLOCK_TEST2_NAME		"rwtest2"
#define SPLAT_RWLOCK_TEST2_DESC		"Multiple Writers"

#define SPLAT_RWLOCK_TEST3_ID		0x0703
#define SPLAT_RWLOCK_TEST3_NAME		"rwtest3"
#define SPLAT_RWLOCK_TEST3_DESC		"Owner Verification"

#define SPLAT_RWLOCK_TEST4_ID		0x0704
#define SPLAT_RWLOCK_TEST4_NAME		"rwtest4"
#define SPLAT_RWLOCK_TEST4_DESC		"Trylock Test"

#define SPLAT_RWLOCK_TEST5_ID		0x0705
#define SPLAT_RWLOCK_TEST5_NAME		"rwtest5"
#define SPLAT_RWLOCK_TEST5_DESC		"Write Downgrade Test"

#define SPLAT_RWLOCK_TEST6_ID		0x0706
#define SPLAT_RWLOCK_TEST6_NAME		"rwtest6"
#define SPLAT_RWLOCK_TEST6_DESC		"Read Upgrade Test"

#define SPLAT_RWLOCK_TEST_MAGIC		0x115599DDUL
#define SPLAT_RWLOCK_TEST_NAME		"rwlock_test"
#define SPLAT_RWLOCK_TEST_COUNT		8

#define SPLAT_RWLOCK_RELEASE_INIT	0
#define SPLAT_RWLOCK_RELEASE_WRITERS	1
#define SPLAT_RWLOCK_RELEASE_READERS	2

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
splat_rwlock_sleep(signed long delay)
{
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(delay);
}

#define splat_rwlock_lock_and_test(lock,test)	\
({						\
	int ret = 0;				\
						\
	spin_lock(lock);			\
	ret = (test) ? 1 : 0;			\
	spin_unlock(lock);			\
	ret;					\
})

void splat_init_rw_priv(rw_priv_t *rwv, struct file *file)
{
	rwv->rw_magic = SPLAT_RWLOCK_TEST_MAGIC;
	rwv->rw_file = file;
	spin_lock_init(&rwv->rw_priv_lock);
	init_waitqueue_head(&rwv->rw_waitq);
	atomic_set(&rwv->rw_completed, 0);
	atomic_set(&rwv->rw_acquired, 0);
	atomic_set(&rwv->rw_waiters, 0);
	atomic_set(&rwv->rw_release, SPLAT_RWLOCK_RELEASE_INIT);

	/* Initialize the read/write lock */
	rw_init(&rwv->rwl, SPLAT_RWLOCK_TEST_NAME, RW_DEFAULT, NULL);
}

int
splat_rwlock_test1_writer_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);
        snprintf(name, sizeof(name), "%s%d",
		 SPLAT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
	splat_rwlock_sleep(rnd * HZ / 1000);

	spin_lock(&rwv->rw_priv_lock);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
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
	splat_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread acquired rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Wait here until the control thread
	 * says we can release the write lock */
	wait_event_interruptible(rwv->rw_waitq,
				 splat_rwlock_lock_and_test(&rwv->rw_priv_lock,
					 atomic_read(&rwv->rw_release) ==
					 SPLAT_RWLOCK_RELEASE_WRITERS));
	spin_lock(&rwv->rw_priv_lock);
	atomic_inc(&rwv->rw_completed);
	atomic_dec(&rwv->rw_acquired);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
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
splat_rwlock_test1_reader_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);
        snprintf(name, sizeof(name), "%s%d",
		 SPLAT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
        splat_rwlock_sleep(rnd * HZ / 1000);

	/* Don't try and and take the semaphore until
	 * someone else has already acquired it */
        wait_event_interruptible(rwv->rw_waitq,
				 splat_rwlock_lock_and_test(&rwv->rw_priv_lock,
					 atomic_read(&rwv->rw_acquired) > 0));

	spin_lock(&rwv->rw_priv_lock);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
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
	splat_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s reader thread acquired rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Wait here until the control thread
         * says we can release the read lock */
	wait_event_interruptible(rwv->rw_waitq,
				 splat_rwlock_lock_and_test(&rwv->rw_priv_lock,
				 atomic_read(&rwv->rw_release) ==
				 SPLAT_RWLOCK_RELEASE_READERS));

	spin_lock(&rwv->rw_priv_lock);
	atomic_inc(&rwv->rw_completed);
	atomic_dec(&rwv->rw_acquired);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
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
splat_rwlock_test1(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	long pids[SPLAT_RWLOCK_TEST_COUNT];
	rw_thr_t rwt[SPLAT_RWLOCK_TEST_COUNT];
	rw_priv_t rwv;

	/* Initialize private data including the rwlock */
	splat_init_rw_priv(&rwv, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = &rwv;
		rwt[i].rwt_id = i;
		rwt[i].rwt_name = SPLAT_RWLOCK_TEST1_NAME;
		rwt[i].rwt_rc = 0;

		/* The first thread will be a writer */
		if (i == 0) {
			pids[i] = kernel_thread(splat_rwlock_test1_writer_thread,
						&rwt[i], 0);
		} else {
			pids[i] = kernel_thread(splat_rwlock_test1_reader_thread,
						&rwt[i], 0);
		}

		if (pids[i] >= 0) {
			count++;
		}
	}

	/* Once the writer has the lock, release the readers */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock, atomic_read(&rwv.rw_acquired) <= 0)) {
		splat_rwlock_sleep(1 * HZ);
	}
	wake_up_interruptible(&rwv.rw_waitq);

	/* Ensure that there is only 1 writer and all readers are waiting */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
				        atomic_read(&rwv.rw_acquired) != 1 ||
					atomic_read(&rwv.rw_waiters) !=
					SPLAT_RWLOCK_TEST_COUNT - 1)) {

		splat_rwlock_sleep(1 * HZ);
	}
	/* Relase the writer */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, SPLAT_RWLOCK_RELEASE_WRITERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Now ensure that there are multiple reader threads holding the lock */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) <= 1)) {
		splat_rwlock_sleep(1 * HZ);
	}
	/* Release the readers */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, SPLAT_RWLOCK_RELEASE_READERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Wait for the test to complete */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) != 0 ||
	       atomic_read(&rwv.rw_waiters) != 0)) {
		splat_rwlock_sleep(1 * HZ);

	}

	rw_destroy(&rwv.rwl);
	return rc;
}

int
splat_rwlock_test2_writer_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);
	snprintf(name, sizeof(name), "%s%d",
		 SPLAT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
	splat_rwlock_sleep(rnd * HZ / 1000);

	/* Here just increment the waiters count even if we are not
	 * exactly about to call rw_enter().  Not really a big deal
	 * since more than likely will be true when we simulate work
	 * later on */
	spin_lock(&rwv->rw_priv_lock);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread trying to acquire rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	atomic_inc(&rwv->rw_waiters);
	spin_unlock(&rwv->rw_priv_lock);

	/* Wait here until the control thread
	 * says we can acquire the write lock */
	wait_event_interruptible(rwv->rw_waitq,
				 splat_rwlock_lock_and_test(&rwv->rw_priv_lock,
				 atomic_read(&rwv->rw_release) ==
				 SPLAT_RWLOCK_RELEASE_WRITERS));

	/* Take the semaphore for writing */
	rw_enter(&rwv->rwl, RW_WRITER);

	spin_lock(&rwv->rw_priv_lock);
	atomic_dec(&rwv->rw_waiters);
	atomic_inc(&rwv->rw_acquired);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread acquired rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	/* Give up the processor for a bit to simulate
	 * doing some work while taking the write lock */
	splat_rwlock_sleep(rnd * HZ / 1000);

	/* Ensure that we are the only one writing */
	if (atomic_read(&rwv->rw_acquired) > 1) {
		rwt->rwt_rc = 1;
	} else {
		rwt->rwt_rc = 0;
	}

	spin_lock(&rwv->rw_priv_lock);
	atomic_inc(&rwv->rw_completed);
	atomic_dec(&rwv->rw_acquired);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
	           "%s writer thread dropped rwlock with "
		   "%d holding lock and %d waiting\n",
		   name, atomic_read(&rwv->rw_acquired),
		   atomic_read(&rwv->rw_waiters));
	spin_unlock(&rwv->rw_priv_lock);

	rw_exit(&rwv->rwl);

	return 0;
}

static int
splat_rwlock_test2(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	long pids[SPLAT_RWLOCK_TEST_COUNT];
	rw_thr_t rwt[SPLAT_RWLOCK_TEST_COUNT];
	rw_priv_t rwv;

	/* Initialize private data including the rwlock */
	splat_init_rw_priv(&rwv, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = &rwv;
		rwt[i].rwt_id = i;
		rwt[i].rwt_name = SPLAT_RWLOCK_TEST2_NAME;
		rwt[i].rwt_rc = 0;

		/* The first thread will be a writer */
		pids[i] = kernel_thread(splat_rwlock_test2_writer_thread,
					&rwt[i], 0);

		if (pids[i] >= 0) {
			count++;
		}
	}

	/* Wait for writers to get queued up */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_waiters) < SPLAT_RWLOCK_TEST_COUNT)) {
		splat_rwlock_sleep(1 * HZ);
	}
	/* Relase the writers */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, SPLAT_RWLOCK_RELEASE_WRITERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Wait for the test to complete */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) != 0 ||
	       atomic_read(&rwv.rw_waiters) != 0)) {
		splat_rwlock_sleep(1 * HZ);
	}

	/* If any of the write threads ever acquired the lock
	 * while another thread had it, make sure we return
	 * an error */
	for (i = 0; i < SPLAT_RWLOCK_TEST_COUNT; i++) {
		if (rwt[i].rwt_rc) {
			rc++;
		}
	}

	rw_destroy(&rwv.rwl);
	return rc;
}

static int
splat_rwlock_test3(struct file *file, void *arg)
{
	kthread_t *owner;
	rw_priv_t rwv;
	int rc = 0;

	/* Initialize private data 
	 * including the rwlock */
	splat_init_rw_priv(&rwv, file);

	/* Take the rwlock for writing */
	rw_enter(&rwv.rwl, RW_WRITER);
	owner = rw_owner(&rwv.rwl);
	if (current != owner) {
		splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "rwlock should "
			   "be owned by pid %d but is owned by pid %d\n",
			   current->pid, owner ? owner->pid : -1);
		rc = -EINVAL;
		goto out;
	}

	/* Release the rwlock */
	rw_exit(&rwv.rwl);
	owner = rw_owner(&rwv.rwl);
	if (owner) {
		splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "rwlock should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		rc = -EINVAL;
		goto out;
	}

	/* Take the rwlock for reading.
	 * Should not have an owner */
	rw_enter(&rwv.rwl, RW_READER);
	owner = rw_owner(&rwv.rwl);
	if (owner) {
		splat_vprint(file, SPLAT_RWLOCK_TEST3_NAME, "rwlock should not "
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
splat_rwlock_test4_reader_thread(void *arg)
{
	rw_thr_t *rwt = (rw_thr_t *)arg;
	rw_priv_t *rwv = rwt->rwt_rwp;
	uint8_t rnd = 0;
	char name[16];

	ASSERT(rwv->rw_magic == SPLAT_RWLOCK_TEST_MAGIC);
        snprintf(name, sizeof(name), "%s%d",
		 SPLAT_RWLOCK_TEST_NAME, rwt->rwt_id);
	daemonize(name);
	get_random_bytes((void *)&rnd, 1);
        splat_rwlock_sleep(rnd * HZ / 1000);

	/* Don't try and and take the semaphore until
	 * someone else has already acquired it */
        wait_event_interruptible(rwv->rw_waitq,
				 splat_rwlock_lock_and_test(&rwv->rw_priv_lock,
				 atomic_read(&rwv->rw_acquired) > 0));

	spin_lock(&rwv->rw_priv_lock);
	splat_vprint(rwv->rw_file, rwt->rwt_name,
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
		splat_vprint(rwv->rw_file, rwt->rwt_name,
			   "%s reader thread acquired rwlock with "
			   "%d holding lock and %d waiting\n",
			   name, atomic_read(&rwv->rw_acquired),
			   atomic_read(&rwv->rw_waiters));
		spin_unlock(&rwv->rw_priv_lock);

		spin_lock(&rwv->rw_priv_lock);
		atomic_dec(&rwv->rw_acquired);
		splat_vprint(rwv->rw_file, rwt->rwt_name,
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
		splat_vprint(rwv->rw_file, rwt->rwt_name,
			   "%s reader thread could not acquire rwlock with "
			   "%d holding lock and %d waiting\n",
			   name, atomic_read(&rwv->rw_acquired),
			   atomic_read(&rwv->rw_waiters));
		spin_unlock(&rwv->rw_priv_lock);
	}

	return 0;
}

static int
splat_rwlock_test4(struct file *file, void *arg)
{
	int i, count = 0, rc = 0;
	long pids[SPLAT_RWLOCK_TEST_COUNT];
	rw_thr_t rwt[SPLAT_RWLOCK_TEST_COUNT];
	rw_priv_t rwv;

	/* Initialize private data 
	 * including the rwlock */
	splat_init_rw_priv(&rwv, file);

	/* Create some threads, the exact number isn't important just as
	 * long as we know how many we managed to create and should expect. */
	for (i = 0; i < SPLAT_RWLOCK_TEST_COUNT; i++) {
		rwt[i].rwt_rwp = &rwv;
		rwt[i].rwt_id = i;
		rwt[i].rwt_name = SPLAT_RWLOCK_TEST4_NAME;
		rwt[i].rwt_rc = 0;

		/* The first thread will be a writer */
		if (i == 0) {
			/* We can reuse the test1 writer thread here */
			pids[i] = kernel_thread(splat_rwlock_test1_writer_thread,
						&rwt[i], 0);
		} else {
			 pids[i] = kernel_thread(splat_rwlock_test4_reader_thread,
						&rwt[i], 0);
		}

		if (pids[i] >= 0) {
			count++;
		}
	}

	/* Once the writer has the lock, release the readers */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) <= 0)) {
		splat_rwlock_sleep(1 * HZ);
	}
	wake_up_interruptible(&rwv.rw_waitq);

	/* Make sure that the reader threads complete */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_completed) != SPLAT_RWLOCK_TEST_COUNT - 1)) {
		splat_rwlock_sleep(1 * HZ);
	}
	/* Release the writer */
	spin_lock(&rwv.rw_priv_lock);
	atomic_set(&rwv.rw_release, SPLAT_RWLOCK_RELEASE_WRITERS);
	spin_unlock(&rwv.rw_priv_lock);
	wake_up_interruptible(&rwv.rw_waitq);

	/* Wait for the test to complete */
	while (splat_rwlock_lock_and_test(&rwv.rw_priv_lock,
	       atomic_read(&rwv.rw_acquired) != 0 ||
	       atomic_read(&rwv.rw_waiters) != 0)) {
		splat_rwlock_sleep(1 * HZ);
	}

	/* If any of the reader threads ever acquired the lock
	 * while another thread had it, make sure we return
	 * an error since the rw_tryenter() should have failed */
	for (i = 0; i < SPLAT_RWLOCK_TEST_COUNT; i++) {
		if (rwt[i].rwt_rc) {
			rc++;
		}
	}

	rw_destroy(&rwv.rwl);
	return rc;
}

static int
splat_rwlock_test5(struct file *file, void *arg)
{
	kthread_t *owner;
	rw_priv_t rwv;
	int rc = 0;

	/* Initialize private data 
	 * including the rwlock */
	splat_init_rw_priv(&rwv, file);

	/* Take the rwlock for writing */
	rw_enter(&rwv.rwl, RW_WRITER);
	owner = rw_owner(&rwv.rwl);
	if (current != owner) {
		splat_vprint(file, SPLAT_RWLOCK_TEST5_NAME, "rwlock should "
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
		splat_vprint(file, SPLAT_RWLOCK_TEST5_NAME, "rwlock should not "
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
splat_rwlock_test6(struct file *file, void *arg)
{
	kthread_t *owner;
	rw_priv_t rwv;
	int rc = 0;

	/* Initialize private data 
	 * including the rwlock */
	splat_init_rw_priv(&rwv, file);

	/* Take the rwlock for reading */
	rw_enter(&rwv.rwl, RW_READER);
	owner = rw_owner(&rwv.rwl);
	if (owner) {
		splat_vprint(file, SPLAT_RWLOCK_TEST6_NAME, "rwlock should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		rc = -EINVAL;
		goto out;
	}

	/* Make sure that the upgrade
	 * worked properly */
	rc = !rw_tryupgrade(&rwv.rwl);

	owner = rw_owner(&rwv.rwl);
	if (rc || current != owner) {
		splat_vprint(file, SPLAT_RWLOCK_TEST6_NAME, "rwlock should "
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

splat_subsystem_t *
splat_rwlock_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_RWLOCK_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_RWLOCK_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_RWLOCK;

        SPLAT_TEST_INIT(sub, SPLAT_RWLOCK_TEST1_NAME, SPLAT_RWLOCK_TEST1_DESC,
                      SPLAT_RWLOCK_TEST1_ID, splat_rwlock_test1);
        SPLAT_TEST_INIT(sub, SPLAT_RWLOCK_TEST2_NAME, SPLAT_RWLOCK_TEST2_DESC,
                      SPLAT_RWLOCK_TEST2_ID, splat_rwlock_test2);
        SPLAT_TEST_INIT(sub, SPLAT_RWLOCK_TEST3_NAME, SPLAT_RWLOCK_TEST3_DESC,
                      SPLAT_RWLOCK_TEST3_ID, splat_rwlock_test3);
        SPLAT_TEST_INIT(sub, SPLAT_RWLOCK_TEST4_NAME, SPLAT_RWLOCK_TEST4_DESC,
                      SPLAT_RWLOCK_TEST4_ID, splat_rwlock_test4);
        SPLAT_TEST_INIT(sub, SPLAT_RWLOCK_TEST5_NAME, SPLAT_RWLOCK_TEST5_DESC,
                      SPLAT_RWLOCK_TEST5_ID, splat_rwlock_test5);
        SPLAT_TEST_INIT(sub, SPLAT_RWLOCK_TEST6_NAME, SPLAT_RWLOCK_TEST6_DESC,
                      SPLAT_RWLOCK_TEST6_ID, splat_rwlock_test6);

        return sub;
}

void
splat_rwlock_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_RWLOCK_TEST6_ID);
        SPLAT_TEST_FINI(sub, SPLAT_RWLOCK_TEST5_ID);
        SPLAT_TEST_FINI(sub, SPLAT_RWLOCK_TEST4_ID);
        SPLAT_TEST_FINI(sub, SPLAT_RWLOCK_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_RWLOCK_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_RWLOCK_TEST1_ID);
        kfree(sub);
}

int
splat_rwlock_id(void) {
        return SPLAT_SUBSYSTEM_RWLOCK;
}
