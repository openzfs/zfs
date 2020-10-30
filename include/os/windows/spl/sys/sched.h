#ifndef OSX_SCHED_H
#define OSX_SCHED_H

#include <sys/mutex.h>


#if 0
typedef void (task_func_t)(void *);

typedef struct task {
        struct task     *task_next;
        struct task     *task_prev;
        task_func_t     *task_func;
        void            *task_arg;
} task_t;

#define TASKQ_ACTIVE    0x00010000

struct taskq {
        kmutex_t        tq_lock;
        krwlock_t       tq_threadlock;
        kcondvar_t      tq_dispatch_cv;
        kcondvar_t      tq_wait_cv;
        thread_t        *tq_threadlist;
        int             tq_flags;
        int             tq_active;
        int             tq_nthreads;
        int             tq_nalloc;
        int             tq_minalloc;
        int             tq_maxalloc;
        task_t          *tq_freelist;
        task_t          tq_task;
};

#endif

#endif
