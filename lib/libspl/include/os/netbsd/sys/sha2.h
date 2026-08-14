// SPDX-License-Identifier: CDDL-1.0
/*
 * dest --sysroot can make <sys/sha2.h> bind to NetBSD
 * (SHA256_CTX / SHA256_Init).  OpenZFS wants SHA2_CTX / SHA2Init
 * from include/sys/sha2.h (guard _SYS_SHA2_H, not __SHA2_H__).
 *
 * Path is 6x .. from lib/libspl/include/os/netbsd/sys/ to the
 * repo root.  Five levels lands on lib/include/sys/sha2.h, which
 * does not exist.
 */
#ifndef	_LIBSPL_OS_NETBSD_SYS_SHA2_H_
#define	_LIBSPL_OS_NETBSD_SYS_SHA2_H_

#ifndef	__SHA2_H__
#define	__SHA2_H__	/* poison NetBSD's guard */
#endif

#include "../../../../../../include/sys/sha2.h"

#endif /* _LIBSPL_OS_NETBSD_SYS_SHA2_H_ */
