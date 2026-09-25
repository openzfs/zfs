
#ifndef	_SPL_PROCESSOR_H
#define	_SPL_PROCESSOR_H

#include <sys/types.h>

extern uint32_t getcpuid();

typedef int	processorid_t;

#if defined(__amd64__) || defined(__x86_64__)

#endif // x86

typedef int	processorid_t;
extern int spl_processor_init(void);

#endif /* _SPL_PROCESSOR_H */
