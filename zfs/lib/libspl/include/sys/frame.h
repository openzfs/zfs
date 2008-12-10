/*
 * This header file distributed under the terms of the CDDL.
 * Portions Copyright 2008 Sun Microsystems, Inc. All Rights reserved.
 */
#ifndef _SYS_FRAME_H
#define _SYS_FRAME_H

#include <sys/types.h>

#if defined(_LP64) || defined(_I32LPx)
typedef long	greg_t;
#else
typedef int	greg_t;
#endif

struct frame {
	greg_t fr_savfp;  /* saved frame pointer */
	greg_t fr_savpc;  /* saved program counter */
};

#if defined(_SYSCALL32)

typedef int32_t greg32_t;
typedef int64_t greg64_t;

/*
 * Kernel's view of a 32-bit stack frame.
 */
struct frame32 {
	greg32_t fr_savfp;  /* saved frame pointer */
	greg32_t fr_savpc;  /* saved program counter */
};

#endif  /* _SYSCALL32 */
#endif /* _SYS_FRAME_H */
