/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or http://www.opensolaris.org/os/licensing.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */

/*
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"%Z%%M%	%I%	%E% SMI"

/*
 * This file is used to verify that the standalone's external dependencies
 * haven't changed in a way that'll break things that use it.
 */

void __umem_assert_failed(void) {}
void atomic_add_64(void) {}
void atomic_add_32_nv(void) {}
void dladdr1(void) {}
void bcopy(void) {}
void bzero(void) {}
void exit(void) {}
void getenv(void) {}
void gethrtime(void) {}
void membar_producer(void) {}
void memcpy(void) {}
void _memcpy(void) {}
void memset(void) {}
void snprintf(void) {}
void strchr(void) {}
void strcmp(void) {}
void strlen(void) {}
void strncpy(void) {}
void strrchr(void) {}
void strtoul(void) {}
void umem_err_recoverable(void) {}
void umem_panic(void) {}
void vsnprintf(void) {}

#ifdef	__i386
void __mul64(void) {}
void __rem64(void) {}
void __div64(void) {}

#ifdef	__GNUC__
void __divdi3(void) {}
void __moddi3(void) {}
#endif	/* __GNUC__ */

#endif	/* __i386 */

int __ctype;
int errno;
