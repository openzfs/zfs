#ifndef	_SPL_SEM_H_
#define	_SPL_SEM_H_

#include <linux/semaphore.h>

typedef struct spl_sem {
	struct semaphore s;
} spl_sem_t;

static void __maybe_unused spl_sem_init(spl_sem_t *sem, int n)
{
	sema_init(&sem->s, n);
}

static void __maybe_unused spl_sem_destroy(spl_sem_t *sem) {}

static inline void __maybe_unused spl_sem_wait(spl_sem_t *sem)
{
	down(&sem->s);
}

static inline void __maybe_unused spl_sem_post(spl_sem_t *sem)
{
	up(&sem->s);
}

#endif	/* _SPL_SEM_H_ */
