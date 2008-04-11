#include <sys/thread.h>
#include <sys/kmem.h>

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
} thread_priv_t;

static int
thread_generic_wrapper(void *arg)
{
	thread_priv_t *tp = (thread_priv_t *)arg;
	void (*func)(void *);
	void *args;

	BUG_ON(tp->tp_magic != TP_MAGIC);
	func = tp->tp_func;
	args = tp->tp_args;
	set_current_state(tp->tp_state);
	set_user_nice((kthread_t *)get_current(), PRIO_TO_NICE(tp->tp_pri));
	kmem_free(arg, sizeof(thread_priv_t));

	if (func)
		func(args);

	return 0;
}

void
__thread_exit(void)
{
	do_exit(0);
	return;
}
EXPORT_SYMBOL(__thread_exit);

/* thread_create() may block forever if it cannot create a thread or
 * allocate memory.  This is preferable to returning a NULL which Solaris
 * style callers likely never check for... since it can't fail. */
kthread_t *
__thread_create(caddr_t stk, size_t  stksize, thread_func_t func,
		const char *name, void *args, size_t len, int *pp,
		int state, pri_t pri)
{
	thread_priv_t *tp;
	DEFINE_WAIT(wait);
	struct task_struct *tsk;

	/* Option pp is simply ignored */
	/* Variable stack size unsupported */
	BUG_ON(stk != NULL);
	BUG_ON(stk != 0);

	tp = kmem_alloc(sizeof(thread_priv_t), KM_SLEEP);
	if (tp == NULL)
		return NULL;

	tp->tp_magic = TP_MAGIC;
	tp->tp_func  = func;
	tp->tp_args  = args;
	tp->tp_len   = len;
	tp->tp_state = state;
	tp->tp_pri   = pri;

	tsk = kthread_create(thread_generic_wrapper, (void *)tp, "%s", name);
	if (IS_ERR(tsk)) {
		printk("spl: Failed to create thread: %ld\n", PTR_ERR(tsk));
		return NULL;
	}

	wake_up_process(tsk);
	return (kthread_t *)tsk;
}
EXPORT_SYMBOL(__thread_create);
