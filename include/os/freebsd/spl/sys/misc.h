// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2007 Pawel Jakub Dawidek <pjd@FreeBSD.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef _OPENSOLARIS_SYS_MISC_H_
#define	_OPENSOLARIS_SYS_MISC_H_

#include <sys/limits.h>
#include <sys/filio.h>

#define	MAXUID	UID_MAX

#define	_ACL_ACLENT_ENABLED	0x1
#define	_ACL_ACE_ENABLED	0x2

#define	_FIOFFS		(INT_MIN)
#define	_FIOGDIO	(INT_MIN+1)
#define	_FIOSDIO	(INT_MIN+2)

#define	F_SEEK_DATA	FIOSEEKDATA
#define	F_SEEK_HOLE	FIOSEEKHOLE

struct opensolaris_utsname {
	const char	*sysname;
	const char	*nodename;
	const char	*release;
	char		version[32];
	const char	*machine;
};

#define	task_io_account_read(n)
#define	task_io_account_write(n)

#endif	/* _OPENSOLARIS_SYS_MISC_H_ */
