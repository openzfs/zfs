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
 *  Solaris Porting LAyer Tests (SPLAT) Thread Tests.
\*****************************************************************************/

#include "splat-internal.h"

#define SPLAT_THREAD_NAME		"thread"
#define SPLAT_THREAD_DESC		"Kernel Thread Tests"

#define SPLAT_THREAD_TEST1_ID		0x0601
#define SPLAT_THREAD_TEST1_NAME		"create"
#define SPLAT_THREAD_TEST1_DESC		"Validate thread creation"

#define SPLAT_THREAD_TEST2_ID		0x0602
#define SPLAT_THREAD_TEST2_NAME		"exit"
#define SPLAT_THREAD_TEST2_DESC		"Validate thread exit"

#define SPLAT_THREAD_TEST_MAGIC		0x4488CC00UL

typedef struct thread_priv {
        unsigned long tp_magic;
        struct file *tp_file;
        spinlock_t tp_lock;
        wait_queue_head_t tp_waitq;
	int tp_rc;
} thread_priv_t;

static int
splat_thread_rc(thread_priv_t *tp, int rc)
{
	int ret;

	spin_lock(&tp->tp_lock);
	ret = (tp->tp_rc == rc);
	spin_unlock(&tp->tp_lock);

	return ret;
}

static void
splat_thread_work1(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	spin_lock(&tp->tp_lock);
	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	tp->tp_rc = 1;
	wake_up(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);

	thread_exit();
}

static int
splat_thread_test1(struct file *file, void *arg)
{
	thread_priv_t tp;
	kthread_t *thr;

	tp.tp_magic = SPLAT_THREAD_TEST_MAGIC;
	tp.tp_file = file;
        spin_lock_init(&tp.tp_lock);
	init_waitqueue_head(&tp.tp_waitq);
	tp.tp_rc = 0;

	thr = (kthread_t *)thread_create(NULL, 0, splat_thread_work1, &tp, 0,
			                 &p0, TS_RUN, minclsyspri);
	/* Must never fail under Solaris, but we check anyway since this
	 * can happen in the linux SPL, we may want to change this behavior */
	if (thr == NULL)
		return  -ESRCH;

	/* Sleep until the thread sets tp.tp_rc == 1 */
	wait_event(tp.tp_waitq, splat_thread_rc(&tp, 1));

        splat_vprint(file, SPLAT_THREAD_TEST1_NAME, "%s",
	           "Thread successfully started properly\n");
	return 0;
}

static void
splat_thread_work2(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	spin_lock(&tp->tp_lock);
	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	tp->tp_rc = 1;
	wake_up(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);

	thread_exit();

	/* The following code is unreachable when thread_exit() is
	 * working properly, which is exactly what we're testing */
	spin_lock(&tp->tp_lock);
	tp->tp_rc = 2;
	wake_up(&tp->tp_waitq);
	spin_unlock(&tp->tp_lock);
}

static int
splat_thread_test2(struct file *file, void *arg)
{
	thread_priv_t tp;
	kthread_t *thr;
	int rc = 0;

	tp.tp_magic = SPLAT_THREAD_TEST_MAGIC;
	tp.tp_file = file;
        spin_lock_init(&tp.tp_lock);
	init_waitqueue_head(&tp.tp_waitq);
	tp.tp_rc = 0;

	thr = (kthread_t *)thread_create(NULL, 0, splat_thread_work2, &tp, 0,
			                 &p0, TS_RUN, minclsyspri);
	/* Must never fail under Solaris, but we check anyway since this
	 * can happen in the linux SPL, we may want to change this behavior */
	if (thr == NULL)
		return  -ESRCH;

	/* Sleep until the thread sets tp.tp_rc == 1 */
	wait_event(tp.tp_waitq, splat_thread_rc(&tp, 1));

	/* Sleep until the thread sets tp.tp_rc == 2, or until we hit
	 * the timeout.  If thread exit is working properly we should
	 * hit the timeout and never see to.tp_rc == 2. */
	rc = wait_event_timeout(tp.tp_waitq, splat_thread_rc(&tp, 2), HZ / 10);
	if (rc > 0) {
		rc = -EINVAL;
	        splat_vprint(file, SPLAT_THREAD_TEST2_NAME, "%s",
		           "Thread did not exit properly at thread_exit()\n");
	} else {
	        splat_vprint(file, SPLAT_THREAD_TEST2_NAME, "%s",
		           "Thread successfully exited at thread_exit()\n");
	}

	return rc;
}

splat_subsystem_t *
splat_thread_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_THREAD_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_THREAD_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
        INIT_LIST_HEAD(&sub->test_list);
        spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_THREAD;

        SPLAT_TEST_INIT(sub, SPLAT_THREAD_TEST1_NAME, SPLAT_THREAD_TEST1_DESC,
                      SPLAT_THREAD_TEST1_ID, splat_thread_test1);
        SPLAT_TEST_INIT(sub, SPLAT_THREAD_TEST2_NAME, SPLAT_THREAD_TEST2_DESC,
                      SPLAT_THREAD_TEST2_ID, splat_thread_test2);

        return sub;
}

void
splat_thread_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_THREAD_TEST2_ID);
        SPLAT_TEST_FINI(sub, SPLAT_THREAD_TEST1_ID);

        kfree(sub);
}

int
splat_thread_id(void) {
        return SPLAT_SUBSYSTEM_THREAD;
}
