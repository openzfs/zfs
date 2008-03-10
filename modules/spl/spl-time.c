#include <sys/sysmacros.h>
#include <sys/time.h>
#include "config.h"

void
__gethrestime(timestruc_t *ts)
{
	getnstimeofday((struct timespec *)ts);
}

EXPORT_SYMBOL(__gethrestime);
