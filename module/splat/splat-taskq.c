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
 *  Solaris Porting LAyer Tests (SPLAT) Task Queue Tests.
\*****************************************************************************/

#include <sys/taskq.h>
#include <sys/kmem.h>
#include "splat-internal.h"

#define SPLAT_TASKQ_NAME		"taskq"
#define SPLAT_TASKQ_DESC		"Kernel Task Queue Tests"

#define SPLAT_TASKQ_TEST1_ID		0x0201
#define SPLAT_TASKQ_TEST1_NAME		"single"
#define SPLAT_TASKQ_TEST1_DESC		"Single task queue, single task"

#define SPLAT_TASKQ_TEST2_ID		0x0202
#define SPLAT_TASKQ_TEST2_NAME		"multiple"
#define SPLAT_TASKQ_TEST2_DESC		"Multiple task queues, multiple tasks"

#define SPLAT_TASKQ_TEST3_ID		0x0203
#define SPLAT_TASKQ_TEST3_NAME		"system"
#define SPLAT_TASKQ_TEST3_DESC		"System task queue, multiple tasks"

#define SPLAT_TASKQ_TEST4_ID		0x0204
#define SPLAT_TASKQ_TEST4_NAME		"wait"
#define SPLAT_TASKQ_TEST4_DESC		"Multiple task waiting"

#define SPLAT_TASKQ_TEST5_ID		0x0205
#define SPLAT_TASKQ_TEST5_NAME		"order"
#define SPLAT_TASKQ_TEST5_DESC		"Correct task ordering"

#define SPLAT_TASKQ_TEST6_ID		0x0206
#define SPLAT_TASKQ_TEST6_NAME		"front"
#define SPLAT_TASKQ_TEST6_DESC		"Correct ordering with TQ_FRONT flag"

#define SPLAT_TASKQ_TEST7_ID		0x0207
#define SPLAT_TASKQ_TEST7_NAME		"recurse"
#define SPLAT_TASKQ_TEST7_DESC		"Single task queue, recursive dispatch"

#define SPLAT_TASKQ_TEST8_ID		0x0208
#define SPLAT_TASKQ_TEST8_NAME		"contention"
#define SPLAT_TASKQ_TEST8_DESC		"1 queue, 100 threads, 131072 tasks"

#define SPLAT_TASKQ_ORDER_MAX		8
#define SPLAT_TASKQ_DEPTH_MAX		16


typedef struct splat_taskq_arg {
	int flag;
	int id;
	atomic_t count;
	int order[SPLAT_TASKQ_ORDER_MAX];
	unsigned int depth;
	taskq_t *tq;
	taskq_ent_t *tqe;
	spinlock_t lock;
	struct file *file;
	const char *name;
} splat_taskq_arg_t;

typedef struct splat_taskq_id {
	int id;
	splat_taskq_arg_t *arg;
} splat_taskq_id_t;

/*
 * Create a taskq, queue a task, wait until task completes, ensure
 * task ran properly, cleanup taskq.
 */
static void
splat_taskq_test13_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' setting flag\n",
	           tq_arg->name, sym2str(splat_taskq_test13_func));
	tq_arg->flag = 1;
}

static int
splat_taskq_test1_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t tqe;

	taskq_init_ent(&tqe);

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		     "Taskq '%s' creating (%s dispatch)\n",
	             SPLAT_TASKQ_TEST1_NAME,
		     prealloc ? "prealloc" : "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST1_NAME, 1, maxclsyspri,
			       50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		           "Taskq '%s' create failed\n",
		           SPLAT_TASKQ_TEST1_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST1_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' dispatching\n",
	           tq_arg.name, sym2str(splat_taskq_test13_func));
	if (prealloc) {
		taskq_dispatch_ent(tq, splat_taskq_test13_func,
		                   &tq_arg, TQ_SLEEP, &tqe);
		id = tqe.tqent_id;
	} else {
		id = taskq_dispatch(tq, splat_taskq_test13_func,
				    &tq_arg, TQ_SLEEP);
	}

	if (id == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST1_NAME,
		             "Taskq '%s' function '%s' dispatch failed\n",
		             tq_arg.name, sym2str(splat_taskq_test13_func));
		taskq_destroy(tq);
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' waiting\n",
	           tq_arg.name);
	taskq_wait(tq);
	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);

	taskq_destroy(tq);

	return (tq_arg.flag) ? 0 : -EINVAL;
}

static int
splat_taskq_test1(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test1_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test1_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create multiple taskq's, each with multiple tasks, wait until
 * all tasks complete, ensure all tasks ran properly and in the
 * correct order.  Run order must be the same as the order submitted
 * because we only have 1 thread per taskq.  Finally cleanup the taskq.
 */
static void
splat_taskq_test2_func1(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d * 2\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(splat_taskq_test2_func1),
	           tq_arg->flag * 2, tq_arg->flag);
	tq_arg->flag *= 2;
}

static void
splat_taskq_test2_func2(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d + 1\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(splat_taskq_test2_func2),
	           tq_arg->flag + 1, tq_arg->flag);
	tq_arg->flag += 1;
}

#define TEST2_TASKQS                    8
#define TEST2_THREADS_PER_TASKQ         1

static int
splat_taskq_test2_impl(struct file *file, void *arg, boolean_t prealloc) {
	taskq_t *tq[TEST2_TASKQS] = { NULL };
	taskqid_t id;
	splat_taskq_arg_t tq_args[TEST2_TASKQS];
	taskq_ent_t *func1_tqes = NULL;
	taskq_ent_t *func2_tqes = NULL;
	int i, rc = 0;

	func1_tqes = kmalloc(sizeof(*func1_tqes) * TEST2_TASKQS, GFP_KERNEL);
	if (func1_tqes == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	func2_tqes = kmalloc(sizeof(*func2_tqes) * TEST2_TASKQS, GFP_KERNEL);
	if (func2_tqes == NULL) {
		rc = -ENOMEM;
		goto out;
	}

	for (i = 0; i < TEST2_TASKQS; i++) {
		taskq_init_ent(&func1_tqes[i]);
		taskq_init_ent(&func2_tqes[i]);

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			     "Taskq '%s/%d' creating (%s dispatch)\n",
			     SPLAT_TASKQ_TEST2_NAME, i,
			     prealloc ? "prealloc" : "dynamic");
		if ((tq[i] = taskq_create(SPLAT_TASKQ_TEST2_NAME,
			                  TEST2_THREADS_PER_TASKQ,
					  maxclsyspri, 50, INT_MAX,
					  TASKQ_PREPOPULATE)) == NULL) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' create failed\n",
				   SPLAT_TASKQ_TEST2_NAME, i);
			rc = -EINVAL;
			break;
		}

		tq_args[i].flag = i;
		tq_args[i].id   = i;
		tq_args[i].file = file;
		tq_args[i].name = SPLAT_TASKQ_TEST2_NAME;

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
			   tq_args[i].name, tq_args[i].id,
		           sym2str(splat_taskq_test2_func1));
		if (prealloc) {
			taskq_dispatch_ent(tq[i], splat_taskq_test2_func1,
			                 &tq_args[i], TQ_SLEEP, &func1_tqes[i]);
			id = func1_tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq[i], splat_taskq_test2_func1,
					    &tq_args[i], TQ_SLEEP);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch "
			           "failed\n", tq_args[i].name, tq_args[i].id,
			           sym2str(splat_taskq_test2_func1));
			rc = -EINVAL;
			break;
		}

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
			   tq_args[i].name, tq_args[i].id,
		           sym2str(splat_taskq_test2_func2));
		if (prealloc) {
			taskq_dispatch_ent(tq[i], splat_taskq_test2_func2,
			                &tq_args[i], TQ_SLEEP, &func2_tqes[i]);
			id = func2_tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq[i], splat_taskq_test2_func2,
			                    &tq_args[i], TQ_SLEEP);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME, "Taskq "
				     "'%s/%d' function '%s' dispatch failed\n",
			             tq_args[i].name, tq_args[i].id,
			             sym2str(splat_taskq_test2_func2));
			rc = -EINVAL;
			break;
		}
	}

	/* When rc is set we're effectively just doing cleanup here, so
	 * ignore new errors in that case.  They just cause noise. */
	for (i = 0; i < TEST2_TASKQS; i++) {
		if (tq[i] != NULL) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' waiting\n",
			           tq_args[i].name, tq_args[i].id);
			taskq_wait(tq[i]);
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d; destroying\n",
			          tq_args[i].name, tq_args[i].id);

			taskq_destroy(tq[i]);

			if (!rc && tq_args[i].flag != ((i * 2) + 1)) {
				splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
				           "out of order; %d != %d\n",
				           tq_args[i].name, tq_args[i].id,
				           tq_args[i].flag, i * 2 + 1);
				rc = -EINVAL;
			} else {
				splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
					   "in the correct order; %d == %d\n",
				           tq_args[i].name, tq_args[i].id,
				           tq_args[i].flag, i * 2 + 1);
			}
		}
	}
out:
	if (func1_tqes)
		kfree(func1_tqes);

	if (func2_tqes)
		kfree(func2_tqes);

	return rc;
}

static int
splat_taskq_test2(struct file *file, void *arg) {
	int rc;

	rc = splat_taskq_test2_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test2_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Use the global system task queue with a single task, wait until task
 * completes, ensure task ran properly.
 */
static int
splat_taskq_test3_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t tqe;

	taskq_init_ent(&tqe);

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST3_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
	           "Taskq '%s' function '%s' %s dispatch\n",
	           tq_arg.name, sym2str(splat_taskq_test13_func),
		   prealloc ? "prealloc" : "dynamic");
	if (prealloc) {
		taskq_dispatch_ent(system_taskq, splat_taskq_test13_func,
		                   &tq_arg, TQ_SLEEP, &tqe);
		id = tqe.tqent_id;
	} else {
		id = taskq_dispatch(system_taskq, splat_taskq_test13_func,
				    &tq_arg, TQ_SLEEP);
	}

	if (id == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
		           "Taskq '%s' function '%s' dispatch failed\n",
		           tq_arg.name, sym2str(splat_taskq_test13_func));
		return -EINVAL;
	}

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME, "Taskq '%s' waiting\n",
	           tq_arg.name);
	taskq_wait(system_taskq);

	return (tq_arg.flag) ? 0 : -EINVAL;
}

static int
splat_taskq_test3(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test3_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test3_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a taskq and dispatch a large number of tasks to the queue.
 * Then use taskq_wait() to block until all the tasks complete, then
 * cross check that all the tasks ran by checking tg_arg->count which
 * is incremented in the task function.  Finally cleanup the taskq.
 *
 * First we try with a large 'maxalloc' value, then we try with a small one.
 * We should not drop tasks when TQ_SLEEP is used in taskq_dispatch(), even
 * if the number of pending tasks is above maxalloc.
 */
static void
splat_taskq_test4_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	ASSERT(tq_arg);

	atomic_inc(&tq_arg->count);
}

static int
splat_taskq_test4_common(struct file *file, void *arg, int minalloc,
                         int maxalloc, int nr_tasks, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t *tqes;
	int i, j, rc = 0;

	tqes = kmalloc(sizeof(*tqes) * nr_tasks, GFP_KERNEL);
	if (tqes == NULL)
		return -ENOMEM;

	splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		     "Taskq '%s' creating (%s dispatch) (%d/%d/%d)\n",
		     SPLAT_TASKQ_TEST4_NAME,
		     prealloc ? "prealloc" : "dynamic",
		     minalloc, maxalloc, nr_tasks);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST4_NAME, 1, maxclsyspri,
		               minalloc, maxalloc, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST4_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST4_NAME;

	for (i = 1; i <= nr_tasks; i *= 2) {
		atomic_set(&tq_arg.count, 0);
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
		             "Taskq '%s' function '%s' dispatched %d times\n",
		             tq_arg.name, sym2str(splat_taskq_test4_func), i);

		for (j = 0; j < i; j++) {
			taskq_init_ent(&tqes[j]);

			if (prealloc) {
				taskq_dispatch_ent(tq, splat_taskq_test4_func,
				                   &tq_arg, TQ_SLEEP, &tqes[j]);
				id = tqes[j].tqent_id;
			} else {
				id = taskq_dispatch(tq, splat_taskq_test4_func,
						    &tq_arg, TQ_SLEEP);
			}

			if (id == 0) {
				splat_vprint(file, SPLAT_TASKQ_TEST4_NAME,
				        "Taskq '%s' function '%s' dispatch "
					"%d failed\n", tq_arg.name,
					sym2str(splat_taskq_test4_func), j);
					rc = -EINVAL;
					goto out;
			}
		}

		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' "
			     "waiting for %d dispatches\n", tq_arg.name, i);
		taskq_wait(tq);
		splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' "
			     "%d/%d dispatches finished\n", tq_arg.name,
			     atomic_read(&tq_arg.count), i);
		if (atomic_read(&tq_arg.count) != i) {
			rc = -ERANGE;
			goto out;

		}
	}
out:
	splat_vprint(file, SPLAT_TASKQ_TEST4_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);
	taskq_destroy(tq);

out_free:
	kfree(tqes);

	return rc;
}

static int
splat_taskq_test4_impl(struct file *file, void *arg, boolean_t prealloc)
{
	int rc;

	rc = splat_taskq_test4_common(file, arg, 50, INT_MAX, 1024, prealloc);
	if (rc)
		return rc;

	rc = splat_taskq_test4_common(file, arg, 1, 1, 32, prealloc);

	return rc;
}

static int
splat_taskq_test4(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test4_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test4_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a taskq and dispatch a specific sequence of tasks carefully
 * crafted to validate the order in which tasks are processed.  When
 * there are multiple worker threads each thread will process the
 * next pending task as soon as it completes its current task.  This
 * means that tasks do not strictly complete in order in which they
 * were dispatched (increasing task id).  This is fine but we need to
 * verify that taskq_wait_id() blocks until the passed task id and all
 * lower task ids complete.  We do this by dispatching the following
 * specific sequence of tasks each of which block for N time units.
 * We then use taskq_wait_id() to unblock at specific task id and
 * verify the only the expected task ids have completed and in the
 * correct order.  The two cases of interest are:
 *
 * 1) Task ids larger than the waited for task id can run and
 *    complete as long as there is an available worker thread.
 * 2) All task ids lower than the waited one must complete before
 *    unblocking even if the waited task id itself has completed.
 *
 * The following table shows each task id and how they will be
 * scheduled.  Each rows represent one time unit and each column
 * one of the three worker threads.  The places taskq_wait_id()
 * must unblock for a specific id are identified as well as the
 * task ids which must have completed and their order.
 *
 *       +-----+       <--- taskq_wait_id(tq, 8) unblocks
 *       |     |            Required Completion Order: 1,2,4,5,3,8,6,7
 * +-----+     |
 * |     |     |
 * |     |     +-----+
 * |     |     |  8  |
 * |     |     +-----+ <--- taskq_wait_id(tq, 3) unblocks
 * |     |  7  |     |      Required Completion Order: 1,2,4,5,3
 * |     +-----+     |
 * |  6  |     |     |
 * +-----+     |     |
 * |     |  5  |     |
 * |     +-----+     |
 * |  4  |     |     |
 * +-----+     |     |
 * |  1  |  2  |  3  |
 * +-----+-----+-----+
 *
 */
static void
splat_taskq_test5_func(void *arg)
{
	splat_taskq_id_t *tq_id = (splat_taskq_id_t *)arg;
	splat_taskq_arg_t *tq_arg = tq_id->arg;
	int factor;

	/* Delays determined by above table */
	switch (tq_id->id) {
		default:		factor = 0;	break;
		case 1: case 8:		factor = 1;	break;
		case 2: case 4: case 5:	factor = 2;	break;
		case 6: case 7:		factor = 4;	break;
		case 3:			factor = 5;	break;
	}

	msleep(factor * 100);
	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskqid %d complete for taskq '%s'\n",
		     tq_id->id, tq_arg->name);

	spin_lock(&tq_arg->lock);
	tq_arg->order[tq_arg->flag] = tq_id->id;
	tq_arg->flag++;
	spin_unlock(&tq_arg->lock);
}

static int
splat_taskq_test_order(splat_taskq_arg_t *tq_arg, int *order)
{
	int i, j;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		if (tq_arg->order[i] != order[i]) {
			splat_vprint(tq_arg->file, tq_arg->name,
				     "Taskq '%s' incorrect completion "
				     "order\n", tq_arg->name);
			splat_vprint(tq_arg->file, tq_arg->name,
				     "%s", "Expected { ");

			for (j = 0; j < SPLAT_TASKQ_ORDER_MAX; j++)
				splat_print(tq_arg->file, "%d ", order[j]);

			splat_print(tq_arg->file, "%s", "}\n");
			splat_vprint(tq_arg->file, tq_arg->name,
				     "%s", "Got      { ");

			for (j = 0; j < SPLAT_TASKQ_ORDER_MAX; j++)
				splat_print(tq_arg->file, "%d ",
					    tq_arg->order[j]);

			splat_print(tq_arg->file, "%s", "}\n");
			return -EILSEQ;
		}
	}

	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskq '%s' validated correct completion order\n",
		     tq_arg->name);

	return 0;
}

static int
splat_taskq_test5_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_id_t tq_id[SPLAT_TASKQ_ORDER_MAX];
	splat_taskq_arg_t tq_arg;
	int order1[SPLAT_TASKQ_ORDER_MAX] = { 1,2,4,5,3,0,0,0 };
	int order2[SPLAT_TASKQ_ORDER_MAX] = { 1,2,4,5,3,8,6,7 };
	taskq_ent_t tqes[SPLAT_TASKQ_ORDER_MAX];
	int i, rc = 0;

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		     "Taskq '%s' creating (%s dispatch)\n",
		     SPLAT_TASKQ_TEST5_NAME,
		     prealloc ? "prealloc" : "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST5_NAME, 3, maxclsyspri,
		               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST5_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	memset(&tq_arg.order, 0, sizeof(int) * SPLAT_TASKQ_ORDER_MAX);
	spin_lock_init(&tq_arg.lock);
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST5_NAME;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		taskq_init_ent(&tqes[i]);

		tq_id[i].id = i + 1;
		tq_id[i].arg = &tq_arg;

		if (prealloc) {
			taskq_dispatch_ent(tq, splat_taskq_test5_func,
			               &tq_id[i], TQ_SLEEP, &tqes[i]);
			id = tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq, splat_taskq_test5_func,
					    &tq_id[i], TQ_SLEEP);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
			        "Taskq '%s' function '%s' dispatch failed\n",
				tq_arg.name, sym2str(splat_taskq_test5_func));
				rc = -EINVAL;
				goto out;
		}

		if (tq_id[i].id != id) {
			splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
			        "Taskq '%s' expected taskqid %d got %d\n",
				tq_arg.name, (int)tq_id[i].id, (int)id);
				rc = -EINVAL;
				goto out;
		}
	}

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name, 3);
	taskq_wait_id(tq, 3);
	if ((rc = splat_taskq_test_order(&tq_arg, order1)))
		goto out;

	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name, 8);
	taskq_wait_id(tq, 8);
	rc = splat_taskq_test_order(&tq_arg, order2);

out:
	splat_vprint(file, SPLAT_TASKQ_TEST5_NAME,
		     "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);

	return rc;
}

static int
splat_taskq_test5(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test5_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test5_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a single task queue with three threads.  Dispatch 8 tasks,
 * setting TQ_FRONT on only the last three.  Sleep after
 * dispatching tasks 1-3 to ensure they will run and hold the threads
 * busy while we dispatch the remaining tasks.  Verify that tasks 6-8
 * run before task 4-5.
 *
 * The following table shows each task id and how they will be
 * scheduled.  Each rows represent one time unit and each column
 * one of the three worker threads.
 *
 *       +-----+
 *       |     |
 * +-----+     |
 * |     |  5  +-----+
 * |     |     |     |
 * |     +-----|     |
 * |  4  |     |     |
 * +-----+     |  8  |
 * |     |     |     |
 * |     |  7  +-----+
 * |     |     |     |
 * |     |-----+     |
 * |  6  |     |     |
 * +-----+     |     |
 * |     |     |     |
 * |  1  |  2  |  3  |
 * +-----+-----+-----+
 *
 */
static void
splat_taskq_test6_func(void *arg)
{
	splat_taskq_id_t *tq_id = (splat_taskq_id_t *)arg;
	splat_taskq_arg_t *tq_arg = tq_id->arg;
	int factor;

	/* Delays determined by above table */
	switch (tq_id->id) {
		default:		factor = 0;	break;
		case 1:			factor = 2;	break;
		case 2: case 4: case 5:	factor = 4;	break;
		case 6: case 7: case 8:	factor = 5;	break;
		case 3:			factor = 6;	break;
	}

	msleep(factor * 100);

	splat_vprint(tq_arg->file, tq_arg->name,
		     "Taskqid %d complete for taskq '%s'\n",
		     tq_id->id, tq_arg->name);

	spin_lock(&tq_arg->lock);
	tq_arg->order[tq_arg->flag] = tq_id->id;
	tq_arg->flag++;
	spin_unlock(&tq_arg->lock);
}

static int
splat_taskq_test6_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_id_t tq_id[SPLAT_TASKQ_ORDER_MAX];
	splat_taskq_arg_t tq_arg;
	int order[SPLAT_TASKQ_ORDER_MAX] = { 1,2,3,6,7,8,4,5 };
	taskq_ent_t tqes[SPLAT_TASKQ_ORDER_MAX];
	int i, rc = 0;
	uint_t tflags;

	splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
		     "Taskq '%s' creating (%s dispatch)\n",
		     SPLAT_TASKQ_TEST6_NAME,
		     prealloc ? "prealloc" : "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST6_NAME, 3, maxclsyspri,
		               50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST6_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	memset(&tq_arg.order, 0, sizeof(int) * SPLAT_TASKQ_ORDER_MAX);
	spin_lock_init(&tq_arg.lock);
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST6_NAME;

	for (i = 0; i < SPLAT_TASKQ_ORDER_MAX; i++) {
		taskq_init_ent(&tqes[i]);

		tq_id[i].id = i + 1;
		tq_id[i].arg = &tq_arg;
		tflags = TQ_SLEEP;
		if (i > 4)
			tflags |= TQ_FRONT;

		if (prealloc) {
			taskq_dispatch_ent(tq, splat_taskq_test6_func,
			                   &tq_id[i], tflags, &tqes[i]);
			id = tqes[i].tqent_id;
		} else {
			id = taskq_dispatch(tq, splat_taskq_test6_func,
					    &tq_id[i], tflags);
		}

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
			        "Taskq '%s' function '%s' dispatch failed\n",
				tq_arg.name, sym2str(splat_taskq_test6_func));
				rc = -EINVAL;
				goto out;
		}

		if (tq_id[i].id != id) {
			splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
			        "Taskq '%s' expected taskqid %d got %d\n",
				tq_arg.name, (int)tq_id[i].id, (int)id);
				rc = -EINVAL;
				goto out;
		}
		/* Sleep to let tasks 1-3 start executing. */
		if ( i == 2 )
			msleep(100);
	}

	splat_vprint(file, SPLAT_TASKQ_TEST6_NAME, "Taskq '%s' "
		     "waiting for taskqid %d completion\n", tq_arg.name,
		     SPLAT_TASKQ_ORDER_MAX);
	taskq_wait_id(tq, SPLAT_TASKQ_ORDER_MAX);
	rc = splat_taskq_test_order(&tq_arg, order);

out:
	splat_vprint(file, SPLAT_TASKQ_TEST6_NAME,
		     "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);

	return rc;
}

static int
splat_taskq_test6(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test6_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test6_impl(file, arg, B_TRUE);

	return rc;
}

static void
splat_taskq_test7_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	taskqid_t id;

	ASSERT(tq_arg);

	if (tq_arg->depth >= SPLAT_TASKQ_DEPTH_MAX)
		return;

	tq_arg->depth++;

	splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST7_NAME,
	             "Taskq '%s' function '%s' dispatching (depth = %u)\n",
	             tq_arg->name, sym2str(splat_taskq_test7_func),
	             tq_arg->depth);

	if (tq_arg->tqe) {
		VERIFY(taskq_empty_ent(tq_arg->tqe));
		taskq_dispatch_ent(tq_arg->tq, splat_taskq_test7_func,
		                   tq_arg, TQ_SLEEP, tq_arg->tqe);
		id = tq_arg->tqe->tqent_id;
	} else {
		id = taskq_dispatch(tq_arg->tq, splat_taskq_test7_func,
		                    tq_arg, TQ_SLEEP);
	}

	if (id == 0) {
		splat_vprint(tq_arg->file, SPLAT_TASKQ_TEST7_NAME,
		             "Taskq '%s' function '%s' dispatch failed "
		             "(depth = %u)\n", tq_arg->name,
		             sym2str(splat_taskq_test7_func), tq_arg->depth);
		tq_arg->flag = -EINVAL;
		return;
	}
}

static int
splat_taskq_test7_impl(struct file *file, void *arg, boolean_t prealloc)
{
	taskq_t *tq;
	taskq_ent_t tqe;
	splat_taskq_arg_t tq_arg;

	splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
	             "Taskq '%s' creating (%s dispatch)\n",
	             SPLAT_TASKQ_TEST7_NAME,
	             prealloc ? "prealloc" :  "dynamic");
	if ((tq = taskq_create(SPLAT_TASKQ_TEST7_NAME, 1, maxclsyspri,
	                       50, INT_MAX, TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST7_NAME);
		return -EINVAL;
	}

	tq_arg.depth = 0;
	tq_arg.flag  = 0;
	tq_arg.id    = 0;
	tq_arg.file  = file;
	tq_arg.name  = SPLAT_TASKQ_TEST7_NAME;
	tq_arg.tq    = tq;

	if (prealloc) {
		taskq_init_ent(&tqe);
		tq_arg.tqe = &tqe;
	} else {
		tq_arg.tqe = NULL;
	}

	splat_taskq_test7_func(&tq_arg);

	if (tq_arg.flag == 0) {
		splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
		             "Taskq '%s' waiting\n", tq_arg.name);
		taskq_wait_id(tq, SPLAT_TASKQ_DEPTH_MAX);
	}

	splat_vprint(file, SPLAT_TASKQ_TEST7_NAME,
	              "Taskq '%s' destroying\n", tq_arg.name);
	taskq_destroy(tq);

	return tq_arg.depth == SPLAT_TASKQ_DEPTH_MAX ? 0 : -EINVAL;
}

static int
splat_taskq_test7(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test7_impl(file, arg, B_FALSE);
	if (rc)
		return rc;

	rc = splat_taskq_test7_impl(file, arg, B_TRUE);

	return rc;
}

/*
 * Create a taskq with 100 threads and dispatch a huge number of trivial
 * tasks to generate contention on tq->tq_lock.  This test should always
 * pass.  The purpose is to provide a benchmark for measuring the
 * effectiveness of taskq optimizations.
 */
static void
splat_taskq_test8_func(void *arg)
{
	splat_taskq_arg_t *tq_arg = (splat_taskq_arg_t *)arg;
	ASSERT(tq_arg);

	atomic_inc(&tq_arg->count);
}

#define TEST8_NUM_TASKS			0x20000
#define TEST8_THREADS_PER_TASKQ		100

static int
splat_taskq_test8_common(struct file *file, void *arg, int minalloc,
                         int maxalloc)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;
	taskq_ent_t **tqes;
	int i, j, rc = 0;

	tqes = vmalloc(sizeof(*tqes) * TEST8_NUM_TASKS);
	if (tqes == NULL)
		return -ENOMEM;
	memset(tqes, 0, sizeof(*tqes) * TEST8_NUM_TASKS);

	splat_vprint(file, SPLAT_TASKQ_TEST8_NAME,
		     "Taskq '%s' creating (%d/%d/%d)\n",
		     SPLAT_TASKQ_TEST8_NAME,
		     minalloc, maxalloc, TEST8_NUM_TASKS);
	if ((tq = taskq_create(SPLAT_TASKQ_TEST8_NAME, TEST8_THREADS_PER_TASKQ,
			       maxclsyspri, minalloc, maxalloc,
			       TASKQ_PREPOPULATE)) == NULL) {
		splat_vprint(file, SPLAT_TASKQ_TEST8_NAME,
		             "Taskq '%s' create failed\n",
		             SPLAT_TASKQ_TEST8_NAME);
		rc = -EINVAL;
		goto out_free;
	}

	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST8_NAME;

	atomic_set(&tq_arg.count, 0);
	for (i = 0; i < TEST8_NUM_TASKS; i++) {
		tqes[i] = kmalloc(sizeof(taskq_ent_t), GFP_KERNEL);
		if (tqes[i] == NULL) {
			rc = -ENOMEM;
			goto out;
		}
		taskq_init_ent(tqes[i]);

		taskq_dispatch_ent(tq, splat_taskq_test8_func,
				   &tq_arg, TQ_SLEEP, tqes[i]);

		id = tqes[i]->tqent_id;

		if (id == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST8_NAME,
			        "Taskq '%s' function '%s' dispatch "
				"%d failed\n", tq_arg.name,
				sym2str(splat_taskq_test8_func), i);
				rc = -EINVAL;
				goto out;
		}
	}

	splat_vprint(file, SPLAT_TASKQ_TEST8_NAME, "Taskq '%s' "
		     "waiting for %d dispatches\n", tq_arg.name,
		     TEST8_NUM_TASKS);
	taskq_wait(tq);
	splat_vprint(file, SPLAT_TASKQ_TEST8_NAME, "Taskq '%s' "
		     "%d/%d dispatches finished\n", tq_arg.name,
		     atomic_read(&tq_arg.count), TEST8_NUM_TASKS);

	if (atomic_read(&tq_arg.count) != TEST8_NUM_TASKS)
		rc = -ERANGE;

out:
	splat_vprint(file, SPLAT_TASKQ_TEST8_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);
	taskq_destroy(tq);
out_free:
	for (j = 0; j < TEST8_NUM_TASKS && tqes[j] != NULL; j++)
		kfree(tqes[j]);
	vfree(tqes);

	return rc;
}

static int
splat_taskq_test8(struct file *file, void *arg)
{
	int rc;

	rc = splat_taskq_test8_common(file, arg, 1, 100);

	return rc;
}

splat_subsystem_t *
splat_taskq_init(void)
{
        splat_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, SPLAT_TASKQ_NAME, SPLAT_NAME_SIZE);
        strncpy(sub->desc.desc, SPLAT_TASKQ_DESC, SPLAT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
	spin_lock_init(&sub->test_lock);
        sub->desc.id = SPLAT_SUBSYSTEM_TASKQ;

	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST1_NAME, SPLAT_TASKQ_TEST1_DESC,
	              SPLAT_TASKQ_TEST1_ID, splat_taskq_test1);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST2_NAME, SPLAT_TASKQ_TEST2_DESC,
	              SPLAT_TASKQ_TEST2_ID, splat_taskq_test2);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST3_NAME, SPLAT_TASKQ_TEST3_DESC,
	              SPLAT_TASKQ_TEST3_ID, splat_taskq_test3);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST4_NAME, SPLAT_TASKQ_TEST4_DESC,
	              SPLAT_TASKQ_TEST4_ID, splat_taskq_test4);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST5_NAME, SPLAT_TASKQ_TEST5_DESC,
	              SPLAT_TASKQ_TEST5_ID, splat_taskq_test5);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST6_NAME, SPLAT_TASKQ_TEST6_DESC,
	              SPLAT_TASKQ_TEST6_ID, splat_taskq_test6);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST7_NAME, SPLAT_TASKQ_TEST7_DESC,
	              SPLAT_TASKQ_TEST7_ID, splat_taskq_test7);
	SPLAT_TEST_INIT(sub, SPLAT_TASKQ_TEST8_NAME, SPLAT_TASKQ_TEST8_DESC,
	              SPLAT_TASKQ_TEST8_ID, splat_taskq_test8);

        return sub;
}

void
splat_taskq_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST8_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST7_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST6_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST5_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST4_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST3_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST2_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST1_ID);

        kfree(sub);
}

int
splat_taskq_id(void) {
        return SPLAT_SUBSYSTEM_TASKQ;
}
