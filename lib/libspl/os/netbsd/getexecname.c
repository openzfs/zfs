// SPDX-License-Identifier: CDDL-1.0
#include <stdint.h>
#include <limits.h>
#include <sys/param.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#include <unistd.h>
#include "../../libspl_impl.h"

__attribute__((visibility("hidden"))) ssize_t
getexecname_impl(char *execname)
{
	size_t len = PATH_MAX;
	int name[4] = { CTL_KERN, KERN_PROC_ARGS, (int)getpid(),
	    KERN_PROC_PATHNAME };

	if (sysctl(name, 4, execname, &len, NULL, 0) != 0)
		return (-1);

	return ((ssize_t)len);
}
