#include <splat-ctl.h>

#define KZT_SUBSYSTEM_TASKQ		0x0200
#define KZT_TASKQ_NAME			"taskq"
#define KZT_TASKQ_DESC			"Kernel Task Queue Tests"

#define KZT_TASKQ_TEST1_ID		0x0201
#define KZT_TASKQ_TEST1_NAME		"single"
#define KZT_TASKQ_TEST1_DESC		"Single task queue, single task"

#define KZT_TASKQ_TEST2_ID              0x0202
#define KZT_TASKQ_TEST2_NAME		"multiple"
#define KZT_TASKQ_TEST2_DESC		"Multiple task queues, multiple tasks"

typedef struct kzt_taskq_arg {
	int flag;
	int id;
	struct file *file;
	const char *name;
} kzt_taskq_arg_t;

/* Validation Test 1 - Create a taskq, queue a task, wait until
 * task completes, ensure task ran properly, cleanup taskq,
 */
static void
kzt_taskq_test1_func(void *arg)
{
	kzt_taskq_arg_t *tq_arg = (kzt_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	kzt_vprint(tq_arg->file, KZT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' setting flag\n",
	           tq_arg->name, sym2str(kzt_taskq_test1_func));
	tq_arg->flag = 1;
}

static int
kzt_taskq_test1(struct file *file, void *arg)
{
	taskq_t *tq;
	taskqid_t id;
	kzt_taskq_arg_t tq_arg;

	kzt_vprint(file, KZT_TASKQ_TEST1_NAME, "Taskq '%s' creating\n",
	           KZT_TASKQ_TEST1_NAME);
	if ((tq = taskq_create(KZT_TASKQ_TEST1_NAME, 1, 0, 0, 0, 0)) == NULL) {
		kzt_vprint(file, KZT_TASKQ_TEST1_NAME,
		           "Taskq '%s' create failed\n",
		           KZT_TASKQ_TEST1_NAME);
		return -EINVAL;
	}

	tq_arg.flag = 0;
	tq_arg.id   = 0;
	tq_arg.file = file;
	tq_arg.name = KZT_TASKQ_TEST1_NAME;

	kzt_vprint(file, KZT_TASKQ_TEST1_NAME,
	           "Taskq '%s' function '%s' dispatching\n",
	           tq_arg.name, sym2str(kzt_taskq_test1_func));
	if ((id = taskq_dispatch(tq, kzt_taskq_test1_func, &tq_arg, 0)) == 0) {
		kzt_vprint(file, KZT_TASKQ_TEST1_NAME,
		           "Taskq '%s' function '%s' dispatch failed\n",
		           tq_arg.name, sym2str(kzt_taskq_test1_func));
		taskq_destory(tq);
		return -EINVAL;
	}

	kzt_vprint(file, KZT_TASKQ_TEST1_NAME, "Taskq '%s' waiting\n",
	           tq_arg.name);
	taskq_wait(tq);
	kzt_vprint(file, KZT_TASKQ_TEST1_NAME, "Taskq '%s' destroying\n",
	           tq_arg.name);
	taskq_destory(tq);

	return (tq_arg.flag) ? 0 : -EINVAL;
}

/* Validation Test 2 - Create multiple taskq's, each with multiple tasks,
 * wait until all tasks complete, ensure all tasks ran properly and in the
 * the correct order, cleanup taskq's
 */
static void
kzt_taskq_test2_func1(void *arg)
{
	kzt_taskq_arg_t *tq_arg = (kzt_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	kzt_vprint(tq_arg->file, KZT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d * 2\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(kzt_taskq_test2_func1),
	           tq_arg->flag * 2, tq_arg->flag);
	tq_arg->flag *= 2;
}

static void
kzt_taskq_test2_func2(void *arg)
{
	kzt_taskq_arg_t *tq_arg = (kzt_taskq_arg_t *)arg;

	ASSERT(tq_arg);
	kzt_vprint(tq_arg->file, KZT_TASKQ_TEST2_NAME,
	           "Taskq '%s/%d' function '%s' flag = %d = %d + 1\n",
	           tq_arg->name, tq_arg->id,
	           sym2str(kzt_taskq_test2_func2),
	           tq_arg->flag + 1, tq_arg->flag);
	tq_arg->flag += 1;
}

#define TEST2_TASKQS                    8
static int
kzt_taskq_test2(struct file *file, void *arg) {
	taskq_t *tq[TEST2_TASKQS] = { NULL };
	taskqid_t id;
	kzt_taskq_arg_t tq_args[TEST2_TASKQS];
	int i, rc = 0;

	for (i = 0; i < TEST2_TASKQS; i++) {

		kzt_vprint(file, KZT_TASKQ_TEST2_NAME, "Taskq '%s/%d' "
		           "creating\n", KZT_TASKQ_TEST2_NAME, i);
		if ((tq[i] = taskq_create(KZT_TASKQ_TEST2_NAME,
			                  1, 0, 0, 0, 0)) == NULL) {
			kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' create failed\n",
		        	   KZT_TASKQ_TEST2_NAME, i);
			rc = -EINVAL;
			break;
		}

		tq_args[i].flag = i;
		tq_args[i].id   = i;
		tq_args[i].file = file;
		tq_args[i].name = KZT_TASKQ_TEST2_NAME;

		kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
	        	   tq_args[i].name, tq_args[i].id,
		           sym2str(kzt_taskq_test2_func1));
		if ((id = taskq_dispatch(
		     tq[i], kzt_taskq_test2_func1, &tq_args[i], 0)) == 0) {
			kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch "
			           "failed\n", tq_args[i].name, tq_args[i].id,
			           sym2str(kzt_taskq_test2_func1));
			rc = -EINVAL;
			break;
		}

		kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
		           "Taskq '%s/%d' function '%s' dispatching\n",
	        	   tq_args[i].name, tq_args[i].id,
		           sym2str(kzt_taskq_test2_func2));
		if ((id = taskq_dispatch(
		     tq[i], kzt_taskq_test2_func2, &tq_args[i], 0)) == 0) {
			kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' function '%s' dispatch failed\n",
			           tq_args[i].name, tq_args[i].id,
			           sym2str(kzt_taskq_test2_func2));
			rc = -EINVAL;
			break;
		}
	}

	/* When rc is set we're effectively just doing cleanup here, so
	 * ignore new errors in that case.  They just cause noise. */
	for (i = 0; i < TEST2_TASKQS; i++) {
		if (tq[i] != NULL) {
			kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d' waiting\n",
			           tq_args[i].name, tq_args[i].id);
			taskq_wait(tq[i]);
			kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
			           "Taskq '%s/%d; destroying\n",
			          tq_args[i].name, tq_args[i].id);
			taskq_destory(tq[i]);

			if (!rc && tq_args[i].flag != ((i * 2) + 1)) {
				kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
				           "out of order; %d != %d\n",
				           tq_args[i].name, tq_args[i].id,
				           tq_args[i].flag, i * 2 + 1);
				rc = -EINVAL;
			} else {
				kzt_vprint(file, KZT_TASKQ_TEST2_NAME,
				           "Taskq '%s/%d' processed tasks "
					   "in the correct order; %d == %d\n",
				           tq_args[i].name, tq_args[i].id,
				           tq_args[i].flag, i * 2 + 1);
			}
		}
	}

	return rc;
}

kzt_subsystem_t *
kzt_taskq_init(void)
{
        kzt_subsystem_t *sub;

        sub = kmalloc(sizeof(*sub), GFP_KERNEL);
        if (sub == NULL)
                return NULL;

        memset(sub, 0, sizeof(*sub));
        strncpy(sub->desc.name, KZT_TASKQ_NAME, KZT_NAME_SIZE);
        strncpy(sub->desc.desc, KZT_TASKQ_DESC, KZT_DESC_SIZE);
        INIT_LIST_HEAD(&sub->subsystem_list);
	INIT_LIST_HEAD(&sub->test_list);
	spin_lock_init(&sub->test_lock);
        sub->desc.id = KZT_SUBSYSTEM_TASKQ;

	KZT_TEST_INIT(sub, KZT_TASKQ_TEST1_NAME, KZT_TASKQ_TEST1_DESC,
	              KZT_TASKQ_TEST1_ID, kzt_taskq_test1);
	KZT_TEST_INIT(sub, KZT_TASKQ_TEST2_NAME, KZT_TASKQ_TEST2_DESC,
	              KZT_TASKQ_TEST2_ID, kzt_taskq_test2);

        return sub;
}

void
kzt_taskq_fini(kzt_subsystem_t *sub)
{
        ASSERT(sub);
	KZT_TEST_FINI(sub, KZT_TASKQ_TEST2_ID);
	KZT_TEST_FINI(sub, KZT_TASKQ_TEST1_ID);

        kfree(sub);
}

int
kzt_taskq_id(void) {
        return KZT_SUBSYSTEM_TASKQ;
}
