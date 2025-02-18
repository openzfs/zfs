// SPDX-License-Identifier: BSD-2-Clause
/*
 * Copyright (c) 2021 iXsystems, Inc.
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
 */

#include <sys/zfs_racct.h>
#include <sys/racct.h>

void
zfs_racct_read(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags)
{
	curthread->td_ru.ru_inblock += iops;
#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(curproc);
		racct_add_force(curproc, RACCT_READBPS, size);
		racct_add_force(curproc, RACCT_READIOPS, iops);
		PROC_UNLOCK(curproc);
	}
#else
	(void) size;
#endif /* RACCT */

	spa_iostats_read_add(spa, size, iops, flags);
}

void
zfs_racct_write(spa_t *spa, uint64_t size, uint64_t iops, uint32_t flags)
{
	curthread->td_ru.ru_oublock += iops;
#ifdef RACCT
	if (racct_enable) {
		PROC_LOCK(curproc);
		racct_add_force(curproc, RACCT_WRITEBPS, size);
		racct_add_force(curproc, RACCT_WRITEIOPS, iops);
		PROC_UNLOCK(curproc);
	}
#else
	(void) size;
#endif /* RACCT */

	spa_iostats_write_add(spa, size, iops, flags);
}
