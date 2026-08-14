// SPDX-License-Identifier: BSD-2-Clause
/*
 * Prefer NetBSD's kernel <sys/sdt.h> (KDTRACE_HOOKS, link-set probes).
 * include_next walks past this overlay to /usr/src/sys/sys/sdt.h.
 * OpenZFS DTRACE_PROBE* then expands to live sdt:::name probes.
 */

#ifndef _OPENSOLARIS_SYS_SDT_H_
#define	_OPENSOLARIS_SYS_SDT_H_

#ifdef _KERNEL
/*
 * ccompile.h may have stubbed SET_ERROR; NetBSD sys/sdt.h (KDTRACE)
 * defines it as a different helper.  Drop the stub so include_next
 * sees a clean namespace.
 */
#ifdef SET_ERROR
#undef SET_ERROR
#endif
#include_next <sys/sdt.h>
#endif

#endif /* _OPENSOLARIS_SYS_SDT_H_ */
