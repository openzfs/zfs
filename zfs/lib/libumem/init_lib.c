/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
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
 * Copyright 2004 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */
/*
 * Portions Copyright 2006 OmniTI, Inc.
 */

/* #pragma ident	"@(#)init_lib.c	1.2	05/06/08 SMI" */

/*
 * Initialization routines for the library version of libumem.
 */

#include "config.h"
#include "umem_base.h"
#include "vmem_base.h"

#if HAVE_UNISTD_H
#include <unistd.h>
#endif
#if HAVE_DLFCN_H
#include <dlfcn.h>
#endif

#include <fcntl.h>
#include <string.h>

#ifdef __FreeBSD__
#include <machine/param.h>
#endif

void
vmem_heap_init(void)
{
#ifdef _WIN32
	vmem_backend = VMEM_BACKEND_MMAP;
#else
#if 0
	void *handle = dlopen("libmapmalloc.so.1", RTLD_NOLOAD);

	if (handle != NULL) {
#endif
		log_message("sbrk backend disabled\n");
		vmem_backend = VMEM_BACKEND_MMAP;
#if 0
	}
#endif
#endif

	if ((vmem_backend & VMEM_BACKEND_MMAP) != 0) {
		vmem_backend = VMEM_BACKEND_MMAP;
		(void) vmem_mmap_arena(NULL, NULL);
	} else {
#ifndef _WIN32
		vmem_backend = VMEM_BACKEND_SBRK;
		(void) vmem_sbrk_arena(NULL, NULL);
#endif
	}
}

/*ARGSUSED*/
void
umem_type_init(caddr_t start, size_t len, size_t pgsize)
{
#ifdef _WIN32
	SYSTEM_INFO info;
	GetSystemInfo(&info);
	pagesize = info.dwPageSize;
#elif !defined(__FreeBSD__)
	pagesize = _sysconf(_SC_PAGESIZE);
#else
	pagesize = PAGE_SIZE;
#endif
}

int
umem_get_max_ncpus(void)
{
#ifdef linux
  /*
   * HACK: sysconf() will invoke malloc() on Linux as part of reading
   * in /proc/stat. To avoid recursion in the malloc replacement
   * version of libumem, read /proc/stat into a static buffer.
   */
  static char proc_stat[8192];
  int fd;
  int ncpus = 1;

  fd = open("/proc/stat", O_RDONLY);
  if (fd >= 0) {
    const ssize_t n = read(fd, proc_stat, sizeof(proc_stat) - 1);
    if (n >= 0) {
      const char *cur;
      const char *next;

      proc_stat[n] = '\0';
      cur = proc_stat;
      while (*cur && (next = strstr(cur + 3, "cpu"))) {
        cur = next;
      }

      if (*cur)
        ncpus = atoi(cur + 3) + 1;
    }

    close(fd);
  }

  return ncpus;

#else /* !linux */

#if _SC_NPROCESSORS_ONLN
  return (2 * sysconf(_SC_NPROCESSORS_ONLN));
#elif defined(_SC_NPROCESSORS_CONF)
  return (2 * sysconf(_SC_NPROCESSORS_CONF));
#elif defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwNumberOfProcessors;
#else
  /* XXX: determine CPU count on other platforms */
  return (1);
#endif

#endif /* linux */
}
