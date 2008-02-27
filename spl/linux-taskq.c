#include <linux-taskq.h>

/*
 * Task queue interface
 *
 * The taskq_work_wrapper functions are used to manage the work_structs
 * which must be submitted to linux.  The shim layer allocates a wrapper
 * structure for all items which contains a pointer to itself as well as
 * the real work to be performed.  When the work item run the generic
 * handle is called which calls the real work function and then using
 * the self pointer frees the work_struct.
 */
typedef struct taskq_work_wrapper {
        struct work_struct tww_work;
        task_func_t        tww_func;
        void *             tww_priv;
} taskq_work_wrapper_t;

static void
taskq_work_handler(void *priv)
{
        taskq_work_wrapper_t *tww = priv;

        BUG_ON(tww == NULL);
        BUG_ON(tww->tww_func == NULL);

        /* Call the real function and free the wrapper */
        tww->tww_func(tww->tww_priv);
        kfree(tww);
}

/* XXX - All flags currently ignored */
taskqid_t
__taskq_dispatch(taskq_t *tq, task_func_t func, void *priv, uint_t flags)
{
        struct workqueue_struct *wq = tq;
        taskq_work_wrapper_t *tww;
        int rc;


        BUG_ON(in_interrupt());
        BUG_ON(tq == NULL);
        BUG_ON(func == NULL);

        tww = (taskq_work_wrapper_t *)kmalloc(sizeof(*tww), GFP_KERNEL);
        if (!tww)
                return (taskqid_t)0;

        INIT_WORK(&(tww->tww_work), taskq_work_handler, tww);
        tww->tww_func = func;
        tww->tww_priv = priv;

        rc = queue_work(wq, &(tww->tww_work));
        if (!rc) {
                kfree(tww);
                return (taskqid_t)0;
        }

        return (taskqid_t)wq;
}
EXPORT_SYMBOL(__taskq_dispatch);

/* XXX - Most args ignored until we decide if it's worth the effort
 *       to emulate the solaris notion of dynamic thread pools.  For
 *       now we simply serialize everything through one thread which
 *       may come back to bite us as a performance issue.
 * pri   - Ignore priority
 * min   - Ignored until this is a dynamic thread pool
 * max   - Ignored until this is a dynamic thread pool
 * flags - Ignored until this is a dynamic thread_pool
 */
taskq_t *
__taskq_create(const char *name, int nthreads, pri_t pri,
               int minalloc, int maxalloc, uint_t flags)
{
	/* NOTE: Linux workqueue names are limited to 10 chars */

        return create_singlethread_workqueue(name);
}
EXPORT_SYMBOL(__taskq_create);
