// SPDX-License-Identifier: CDDL-1.0
/*
 * ${NETBSDSRCDIR}/sys is first on the kmod include path, so
 * <sys/sha2.h> would bind to NetBSD (SHA256_CTX / SHA256_Init).
 * OpenZFS wants SHA2_CTX / SHA2Init from include/sys/sha2.h.
 */
#ifndef	_OPENSOLARIS_NETBSD_SYS_SHA2_H_
#define	_OPENSOLARIS_NETBSD_SYS_SHA2_H_

#ifndef	__SHA2_H__
#define	__SHA2_H__	/* poison NetBSD's guard */
#endif

#include "../../../../sys/sha2.h"

#endif /* _OPENSOLARIS_NETBSD_SYS_SHA2_H_ */
