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

#define SPLAT_SUBSYSTEM_MUTEX		0x0400
#define SPLAT_MUTEX_NAME		"mutex"
#define SPLAT_MUTEX_DESC		"Kernel Mutex Tests"

#define SPLAT_MUTEX_TEST1_ID		0x0401
#define SPLAT_MUTEX_TEST1_NAME		"tryenter"
#define SPLAT_MUTEX_TEST1_DESC		"Validate mutex_tryenter() correctness"

#define SPLAT_MUTEX_TEST2_ID		0x0402
#define SPLAT_MUTEX_TEST2_NAME		"race"
#define SPLAT_MUTEX_TEST2_DESC		"Many threads entering/exiting the mutex"

#define SPLAT_MUTEX_TEST3_ID		0x0403
#define SPLAT_MUTEX_TEST3_NAME		"owned"
#define SPLAT_MUTEX_TEST3_DESC		"Validate mutex_owned() correctness"

#define SPLAT_MUTEX_TEST4_ID		0x0404
#define SPLAT_MUTEX_TEST4_NAME		"owner"
#define SPLAT_MUTEX_TEST4_DESC		"Validate mutex_owner() correctness"

#define SPLAT_MUTEX_TEST_MAGIC		0x115599DDUL
#define SPLAT_MUTEX_TEST_NAME		"mutex_test"
#define SPLAT_MUTEX_TEST_WORKQ		"mutex_wq"
#define SPLAT_MUTEX_TEST_COUNT		128

typedef struct mutex_priv {
        unsigned long mp_magic;
        struct file *mp_file;
	struct work_struct mp_work[SPLAT_MUTEX_TEST_COUNT];
	kmutex_t mp_mtx;
	int mp_rc;
} mutex_priv_t;

static void
splat_mutex_test1_work(void *priv)
{
	mutex_priv_t *mp;

        mp = spl_get_work_data(priv, mutex_priv_t, mp_work.work);
	ASSERT(mp->mp_magic == SPLAT_MUTEX_TEST_MAGIC);
	mp->mp_rc = 0;

	if (!mutex_tryenter(&mp->mp_mtx))
		mp->mp_rc = -EBUSY;
}

static int
splat_mutex_test1(struct file *file, void *arg)
{
	struct workqueue_struct *wq;
	struct work_struct work;
	mutex_priv_t *mp;
	int rc = 0;

	mp = (mutex_priv_t *)kmalloc(sizeof(*mp), GFP_KERNEL);
	if (mp == NULL)
		return -ENOMEM;

	wq = create_singlethread_workqueue(SPLAT_MUTEX_TEST_WORKQ);
	if (wq == NULL) {
		rc = -ENOMEM;
		goto out2;
	}

	mutex_init(&(mp->mp_mtx), SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);
	mutex_enter(&(mp->mp_mtx));

	mp->mp_magic = SPLAT_MUTEX_TEST_MAGIC;
	mp->mp_file = file;
	spl_init_work(&work, splat_mutex_test1_work, mp);

	/* Schedule a work item which will try and aquire the mutex via
	  * mutex_tryenter() while its held.  This should fail and the work
	 * item will indicte this status in the passed private data. */
	if (!queue_work(wq, &work)) {
		mutex_exit(&(mp->mp_mtx));
		rc = -EINVAL;
		goto out;
	}

	flush_workqueue(wq);
	mutex_exit(&(mp->mp_mtx));

	/* Work item successfully aquired mutex, very bad! */
	if (mp->mp_rc != -EBUSY) {
		rc = -EINVAL;
		goto out;
	}

        splat_vprint(file, SPLAT_MUTEX_TEST1_NAME, "%s",
                   "mutex_trylock() correctly failed when mutex held\n");

	/* Schedule a work item which will try and aquire the mutex via
	 * mutex_tryenter() while it is not  held.  This should work and
	 * the item will indicte this status in the passed private data. */
	if (!queue_work(wq, &work)) {
		rc = -EINVAL;
		goto out;
	}

	flush_workqueue(wq);

	/* Work item failed to aquire mutex, very bad! */
	if (mp->mp_rc != 0) {
		rc = -EINVAL;
		goto out;
	}

        splat_vprint(file, SPLAT_MUTEX_TEST1_NAME, "%s",
                   "mutex_trylock() correctly succeeded when mutex unheld\n");
out:
	mutex_destroy(&(mp->mp_mtx));
	destroy_workqueue(wq);
out2:
	kfree(mp);
	return rc;
}

static void
splat_mutex_test2_work(void *priv)
{
	mutex_priv_t *mp;
	int rc;

	mp = spl_get_work_data(priv, mutex_priv_t, mp_work.work);
	ASSERT(mp->mp_magic == SPLAT_MUTEX_TEST_MAGIC);

	/* Read the value before sleeping and write it after we wake up to
	 * maximize the chance of a race if mutexs are not working properly */
	mutex_enter(&mp->mp_mtx);
	rc = mp->mp_rc;
	set_current_state(TASK_INTERRUPTIBLE);
	schedule_timeout(HZ / 100);  /* 1/100 of a second */
	mp->mp_rc = rc + 1;
	mutex_exit(&mp->mp_mtx);
}

static int
splat_mutex_test2(struct file *file, void *arg)
{
	struct workqueue_struct *wq;
	mutex_priv_t *mp;
	int i, rc = 0;

	mp = (mutex_priv_t *)kmalloc(sizeof(*mp), GFP_KERNEL);
	if (mp == NULL)
		return -ENOMEM;

	/* Create a thread per CPU items on queue will race */
	wq = create_workqueue(SPLAT_MUTEX_TEST_WORKQ);
	if (wq == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	mutex_init(&(mp->mp_mtx), SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);

	mp->mp_magic = SPLAT_MUTEX_TEST_MAGIC;
	mp->mp_file = file;
	mp->mp_rc = 0;

	/* Schedule N work items to the work queue each of which enters the
	 * mutex, sleeps briefly, then exits the mutex.  On a multiprocessor
	 * box these work items will be handled by all available CPUs.  The
	 * mutex is instrumented such that if any two processors are in the
	 * critical region at the same time the system will panic.  If the
	 * mutex is implemented right this will never happy, that's a pass. */
	for (i = 0; i < SPLAT_MUTEX_TEST_COUNT; i++) {
		spl_init_work(&(mp->mp_work[i]), splat_mutex_test2_work, mp);

		if (!queue_work(wq, &(mp->mp_work[i]))) {
		        splat_vprint(file, SPLAT_MUTEX_TEST2_NAME,
			           "Failed to queue work id %d\n", i);
			rc = -EINVAL;
		}
	}

	flush_workqueue(wq);

	if (mp->mp_rc == SPLAT_MUTEX_TEST_COUNT) {
	        splat_vprint(file, SPLAT_MUTEX_TEST2_NAME, "%d racing threads "
			   "correctly entered/exited the mutex %d times\n",
			   num_online_cpus(), mp->mp_rc);
	} else {
	        splat_vprint(file, SPLAT_MUTEX_TEST2_NAME, "%d racing threads "
			   "only processed %d/%d mutex work items\n",
			   num_online_cpus(), mp->mp_rc, SPLAT_MUTEX_TEST_COUNT);
		rc = -EINVAL;
	}

	mutex_destroy(&(mp->mp_mtx));
	destroy_workqueue(wq);
out:
	kfree(mp);
	return rc;
}

static int
splat_mutex_test3(struct file *file, void *arg)
{
        kmutex_t mtx;
	int rc = 0;

	mutex_init(&mtx, SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);

	mutex_enter(&mtx);

	/* Mutex should be owned by current */
	if (!mutex_owned(&mtx)) {
	        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex should "
			   "be owned by pid %d but is owned by pid %d\n",
			   current->pid, mtx.km_owner ?  mtx.km_owner->pid : -1);
		rc = -EINVAL;
		goto out;
	}

	mutex_exit(&mtx);

	/* Mutex should not be owned by any task */
	if (mutex_owned(&mtx)) {
	        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex should "
			   "not be owned but is owned by pid %d\n",
			   mtx.km_owner ?  mtx.km_owner->pid : -1);
		rc = -EINVAL;
		goto out;
	}

        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "%s",
		   "Correct mutex_owned() behavior\n");
out:
	mutex_destroy(&mtx);

	return rc;
}

static int
splat_mutex_test4(struct file *file, void *arg)
{
        kmutex_t mtx;
	kthread_t *owner;
	int rc = 0;

	mutex_init(&mtx, SPLAT_MUTEX_TEST_NAME, MUTEX_DEFAULT, NULL);

	mutex_enter(&mtx);

	/* Mutex should be owned by current */
	owner = mutex_owner(&mtx);
	if (current != owner) {
	        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex should "
			   "be owned by pid %d but is owned by pid %d\n",
			   current->pid, owner ? owner->pid : -1);
		rc = -EINVAL;
		goto out;
	}

	mutex_exit(&mtx);

	/* Mutex should not be owned by any task */
	owner = mutex_owner(&mtx);
	if (owner) {
	        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "Mutex should not "
			   "be owned but is owned by pid %d\n", owner->pid);
		rc = -EINVAL;
		goto out;
	}

        splat_vprint(file, SPLAT_MUTEX_TEST3_NAME, "%s",
		   "Correct mutex_owner() behavior\n");
out:
	mutex_destroy(&mtx);

	return rc;
}

splat_subsystem_t *
splat_mutex_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_MUTEX_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_MUTEX_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_MUTEX;

        SPLAT_TEST_INIT(sub, SPLAT_MUTEX_TEST1_NAME, SPLAT_MUTEX_TEST1_DESC,
                      SPLAT_MUTEX_TEST1_ID, splat_mutex_test1);
        SPLAT_TEST_INIT(sub, SPLAT_MUTEX_TEST2_NAME, SPLAT_MUTEX_TEST2_DESC,
                      SPLAT_MUTEX_TEST2_ID, splat_mutex_test2);
        SPLAT_TEST_INIT(sub, SPLAT_MUTEX_TEST3_NAME, SPLAT_MUTEX_TEST3_DESC,
                      SPLAT_MUTEX_TEST3_ID, splat_mutex_test3);
        SPLAT_TEST_INIT(sub, SPLAT_MUTEX_TEST4_NAME, SPLAT_MUTEX_TEST4_DESC,
                      SPLAT_MUTEX_TEST4_ID, splat_mutex_test4);

        return sub;
}

void
splat_mutex_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_MUTEX_TEST4_ID);
        SPLAT_TEST_FINI(sub, SPLAT_MUTEX_TEST3_ID);
        SPLAT_TEST_FINI(sub, SPLAT_MUTEX_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_MUTEX_TEST1_ID);

        kfree(sub);
}

int
splat_mutex_id(void) {
        return SPLAT_SUBSYSTEM_MUTEX;
}
