#include "splat-internal.h"

#define SPLAT_SUBSYSTEM_THREAD		0x0600
#define SPLAT_THREAD_NAME		"thread"
#define SPLAT_THREAD_DESC		"Kernel Thread Tests"

#define SPLAT_THREAD_TEST1_ID		0x0601
#define SPLAT_THREAD_TEST1_NAME		"create"
#define SPLAT_THREAD_TEST1_DESC		"Validate thread creation and destruction"

#define SPLAT_THREAD_TEST_MAGIC		0x4488CC00UL

typedef struct thread_priv {
        unsigned long tp_magic;
        struct file *tp_file;
        spinlock_t tp_lock;
        wait_queue_head_t tp_waitq;
	int tp_rc;
} thread_priv_t;


static void
splat_thread_work(void *priv)
{
	thread_priv_t *tp = (thread_priv_t *)priv;

	spin_lock(&tp->tp_lock);
	ASSERT(tp->tp_magic == SPLAT_THREAD_TEST_MAGIC);
	tp->tp_rc = 1;

	spin_unlock(&tp->tp_lock);
	wake_up(&tp->tp_waitq);

	thread_exit();
}

static int
splat_thread_test1(struct file *file, void *arg)
{
	thread_priv_t tp;
        DEFINE_WAIT(wait);
	kthread_t *thr;
	int rc = 0;

	tp.tp_magic = SPLAT_THREAD_TEST_MAGIC;
	tp.tp_file = file;
        spin_lock_init(&tp.tp_lock);
	init_waitqueue_head(&tp.tp_waitq);
	tp.tp_rc = 0;

	spin_lock(&tp.tp_lock);

	thr = (kthread_t *)thread_create(NULL, 0, splat_thread_work, &tp, 0,
			                 &p0, TS_RUN, minclsyspri);
	/* Must never fail under Solaris, but we check anyway so we can
	 * report an error when this impossible thing happens */
	if (thr == NULL) {
		rc = -ESRCH;
		goto out;
	}

        for (;;) {
                prepare_to_wait(&tp.tp_waitq, &wait, TASK_UNINTERRUPTIBLE);
                if (tp.tp_rc)
                        break;

                spin_unlock(&tp.tp_lock);
                schedule();
                spin_lock(&tp.tp_lock);
        }

        splat_vprint(file, SPLAT_THREAD_TEST1_NAME, "%s",
	           "Thread successfully started and exited cleanly\n");
out:
	spin_unlock(&tp.tp_lock);

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

        return sub;
}

void
splat_thread_fini(splat_subsystem_t *sub)
{
        ASSERT(sub);
        SPLAT_TEST_FINI(sub, SPLAT_THREAD_TEST1_ID);

        kfree(sub);
}

int
splat_thread_id(void) {
        return SPLAT_SUBSYSTEM_THREAD;
}
