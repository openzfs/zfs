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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#ifndef _SYS_DMU_CTL_H
#define _SYS_DMU_CTL_H

#include <sys/types.h>

/* Default directory where the clients search for sockets to connect */
#define DMU_CTL_DEFAULT_DIR "/var/run/zfs/udmu"

/*
 * These functions are called by the server process.
 *
 * kernel_init() must be called before dctl_server_init().
 * kernel_fini() must not be called before dctl_server_fini().
 *
 * All objsets must be closed and object references be released before calling
 * dctl_server_fini(), otherwise it will return EBUSY.
 *
 * Note: On Solaris, it is highly recommended to either catch or ignore the
 * SIGPIPE signal, otherwise the server process will die if the client is
 * killed.
 */
int dctl_server_init(const char *cfg_dir, int min_threads,
    int max_free_threads);
int dctl_server_fini(void);

/*
 * The following functions are called by the DMU from the server process context
 * (in the worker threads).
 */
int dctls_copyin(const void *src, void *dest, size_t size);
int dctls_copyinstr(const char *from, char *to, size_t max,
    size_t *len);
int dctls_copyout(const void *src, void *dest, size_t size);
int dctls_fd_read(int fd, void *buf, ssize_t len, ssize_t *residp);
int dctls_fd_write(int fd, const void *src, ssize_t len);

/*
 * These functions are called by the client process (libzfs).
 */
int dctlc_connect(const char *dir, boolean_t check_subdirs);
void dctlc_disconnect(int fd);

int dctlc_ioctl(int fd, int32_t request, void *arg);

#endif
