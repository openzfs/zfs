// SPDX-License-Identifier: CDDL-1.0
#ifndef _OPENSOLARIS_SYS_SIG_H_
#define	_OPENSOLARIS_SYS_SIG_H_

#ifndef _STANDALONE

#ifdef _KERNEL
#include <sys/param.h>
#include <sys/proc.h>
#include <sys/signalvar.h>

static __inline int
issig(void)
{
	struct lwp *l = curlwp;

	if (l == NULL)
		return (0);
	return (sigispending(l, 0) != 0);
}
#endif /* _KERNEL */

#endif /* !_STANDALONE */

#endif	/* _OPENSOLARIS_SYS_SIG_H_ */
