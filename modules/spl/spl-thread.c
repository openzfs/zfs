#include <sys/thread.h>

/*
 * Thread interfaces
 */
typedef struct thread_priv_s {
	unsigned long tp_magic;		/* Magic */
	void (*tp_func)(void *);	/* Registered function */
	void *tp_args;			/* Args to be passed to function */
	size_t tp_len;			/* Len to be passed to function */
	int tp_state;			/* State to start thread at */
	pri_t tp_pri;			/* Priority to start threat at */
	volatile kthread_t *tp_task;	/* Task pointer for new thread */
	spinlock_t tp_lock;		/* Syncronization lock */
        wait_queue_head_t tp_waitq;	/* Syncronization wait queue */
} thread_priv_t;

static int
thread_generic_wrapper(void *arg)
{
	thread_priv_t *tp = (thread_priv_t *)arg;
	void (*func)(void *);
	void *args;
	char name[16];

	/* Use the truncated function name as thread name */
	snprintf(name, sizeof(name), "%s", "kthread");
	daemonize(name);

        spin_lock(&tp->tp_lock);
	BUG_ON(tp->tp_magic != TP_MAGIC);
	func = tp->tp_func;
	args = tp->tp_args;
	tp->tp_task = get_current();
	set_current_state(tp->tp_state);
	set_user_nice((kthread_t *)tp->tp_task, PRIO_TO_NICE(tp->tp_pri));

        spin_unlock(&tp->tp_lock);
	wake_up(&tp->tp_waitq);

	/* DO NOT USE 'ARG' AFTER THIS POINT, EVER, EVER, EVER!
	 * Local variables are used here because after the calling thread
	 * has been woken up it will exit and this memory will no longer
	 * be safe to access since it was declared on the callers stack. */
	if (func)
		func(args);

	return 0;
}

void
__thread_exit(void)
{
	return;
}
EXPORT_SYMBOL(__thread_exit);

/* thread_create() may block forever if it cannot create a thread or
 * allocate memory.  This is preferable to returning a NULL which Solaris
 * style callers likely never check for... since it can't fail. */
kthread_t *
__thread_create(caddr_t stk, size_t  stksize, void (*proc)(void *),
		void *args, size_t len, proc_t *pp, int state, pri_t pri)
{
	thread_priv_t tp;
	DEFINE_WAIT(wait);
	long pid;

	/* Option pp is simply ignored */
	/* Variable stack size unsupported */
	BUG_ON(stk != NULL);
	BUG_ON(stk != 0);

	/* Variable tp is located on the stack and not the heap because I want
	 * to minimize any chance of a failure, since the Solaris code is designed
	 * such that this function cannot fail.  This is a little dangerous since
	 * we're passing a stack address to a new thread but correct locking was
	 * added to ensure the callee can use the data safely until wake_up(). */
	tp.tp_magic = TP_MAGIC;
	tp.tp_func  = proc;
	tp.tp_args  = args;
	tp.tp_len   = len;
	tp.tp_state = state;
	tp.tp_pri   = pri;
	tp.tp_task  = NULL;
	spin_lock_init(&tp.tp_lock);
        init_waitqueue_head(&tp.tp_waitq);

	spin_lock(&tp.tp_lock);

	/* Solaris says this must never fail so we try forever */
	while ((pid = kernel_thread(thread_generic_wrapper, (void *)&tp, 0)) < 0)
		printk(KERN_ERR "Unable to create thread; pid = %ld\n", pid);

	/* All signals are ignored due to sleeping TASK_UNINTERRUPTIBLE */
	for (;;) {
		prepare_to_wait(&tp.tp_waitq, &wait, TASK_UNINTERRUPTIBLE);
		if (tp.tp_task != NULL)
			break;

		spin_unlock(&tp.tp_lock);
		schedule();
		spin_lock(&tp.tp_lock);
	}

	/* Verify the pid retunred matches the pid in the task struct */
	BUG_ON(pid != (tp.tp_task)->pid);

	spin_unlock(&tp.tp_lock);

	return (kthread_t *)tp.tp_task;
}
EXPORT_SYMBOL(__thread_create);
