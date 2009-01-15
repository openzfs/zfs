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

#define SPLAT_SUBSYSTEM_TASKQ		0x0200
#define SPLAT_TASKQ_NAME		"taskq"
#define SPLAT_TASKQ_DESC		"Kernel Task Queue Tests"

#define SPLAT_TASKQ_TEST1_ID		0x0201
#define SPLAT_TASKQ_TEST1_NAME		"single"
#define SPLAT_TASKQ_TEST1_DESC		"Single task queue, single task"

#define SPLAT_TASKQ_TEST2_ID            0x0202
#define SPLAT_TASKQ_TEST2_NAME		"multiple"
#define SPLAT_TASKQ_TEST2_DESC		"Multiple task queues, multiple tasks"

#define SPLAT_TASKQ_TEST3_ID            0x0203
#define SPLAT_TASKQ_TEST3_NAME		"system"
#define SPLAT_TASKQ_TEST3_DESC		"System task queue, multiple tasks"

typedef struct splat_taskq_arg {
	int flag;
	int id;
	struct file *file;
	const char *name;
} splat_taskq_arg_t;

/* Validation Test 1 - Create a taskq, queue a task, wait until
 * task completes, ensure task ran properly, cleanup taskq,
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
splat_taskq_test1(struct file *file, void *arg)
{
	taskq_t *tq;
	taskqid_t id;
	splat_taskq_arg_t tq_arg;

	splat_vprint(file, SPLAT_TASKQ_TEST1_NAME, "Taskq '%s' creating\n",
	           SPLAT_TASKQ_TEST1_NAME);
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
	if ((id = taskq_dispatch(tq, splat_taskq_test13_func,
				 &tq_arg, TQ_SLEEP)) == 0) {
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

/* Validation Test 2 - Create multiple taskq's, each with multiple tasks,
 * wait until all tasks complete, ensure all tasks ran properly and in the
 * the correct order, cleanup taskq's
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
#define TEST2_THREADS_PER_TASKQ         4

static int
splat_taskq_test2(struct file *file, void *arg) {
	taskq_t *tq[TEST2_TASKQS] = { NULL };
	taskqid_t id;
	splat_taskq_arg_t tq_args[TEST2_TASKQS];
	int i, rc = 0;

	for (i = 0; i < TEST2_TASKQS; i++) {

		splat_vprint(file, SPLAT_TASKQ_TEST2_NAME, "Taskq '%s/%d' "
		           "creating\n", SPLAT_TASKQ_TEST2_NAME, i);
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
		if ((id = taskq_dispatch(
		     tq[i], splat_taskq_test2_func1,
		     &tq_args[i], TQ_SLEEP)) == 0) {
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
		if ((id = taskq_dispatch(
		     tq[i], splat_taskq_test2_func2,
		     &tq_args[i], TQ_SLEEP)) == 0) {
			splat_vprint(file, SPLAT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch failed\n",
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

	return rc;
}

/* Validation Test 3 - Use the global system task queue with a single
 * task, * wait until task completes, ensure task ran properly.
 */
static int
splat_taskq_test3(struct file *file, void *arg)
{
	taskqid_t id;
	splat_taskq_arg_t tq_arg;

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = SPLAT_TASKQ_TEST3_NAME;

	splat_vprint(file, SPLAT_TASKQ_TEST3_NAME,
	           "Taskq '%s' function '%s' dispatching\n",
	           tq_arg.name, sym2str(splat_taskq_test13_func));
	if ((id = taskq_dispatch(system_taskq, splat_taskq_test13_func,
				 &tq_arg, TQ_SLEEP)) == 0) {
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

        return sub;
}

void
splat_taskq_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST3_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST2_ID);
	SPLAT_TEST_FINI(sub, SPLAT_TASKQ_TEST1_ID);

        kfree(sub);
}

int
splat_taskq_id(void) {
        return SPLAT_SUBSYSTEM_TASKQ;
}
