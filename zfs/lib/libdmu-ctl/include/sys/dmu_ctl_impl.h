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

#ifndef _SYS_DMU_CTL_IMPL_H
#define _SYS_DMU_CTL_IMPL_H

#include <sys/list.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>

#define SOCKNAME "dmu_socket"

#define DCTL_PROTOCOL_VER 1
#define DCTL_MAGIC 0xdc71b1070c01dc71ll

/* Message types */
enum {
	DCTL_IOCTL,
	DCTL_IOCTL_REPLY,
	DCTL_COPYIN,
	DCTL_COPYINSTR,
	DCTL_COPYOUT,
	DCTL_FD_READ,
	DCTL_FD_WRITE,
	DCTL_GEN_REPLY /* generic reply */
};

/* On-the-wire message */
typedef struct dctl_cmd {
	uint64_t dcmd_magic;
	int8_t   dcmd_version;
	int8_t   dcmd_msg;
	uint8_t  dcmd_pad[6];
	union {
		struct dcmd_ioctl {
			uint64_t arg;
			int32_t cmd;
			uint8_t pad[4];
		} dcmd_ioctl;

		struct dcmd_copy_req {
			uint64_t ptr;
			uint64_t size;
		} dcmd_copy;

		struct dcmd_fd_req {
			int64_t size;
			int32_t fd;
			uint8_t pad[4];
		} dcmd_fd_io;

		struct dcmd_reply {
			uint64_t size;  /* used by reply to DCTL_COPYINSTR,
			                   DCTL_FD_READ and DCTL_FD_WRITE */
			int32_t rc;     /* return code */
			uint8_t pad[4];
		} dcmd_reply;
	} u;
} dctl_cmd_t;

#define DCTL_CMD_HEADER_SIZE (sizeof(uint64_t) + sizeof(uint8_t))

/*
 * The following definitions are only used by the server code.
 */

#define LISTEN_BACKLOG 5

/* Worker thread data */
typedef struct wthr_info {
	list_node_t wthr_node;
	pthread_t   wthr_id;
	boolean_t   wthr_exit; /* termination flag */
	boolean_t   wthr_free;
} wthr_info_t;

/* Control socket data */
typedef struct dctl_sock_info {
	pthread_mutex_t    dsi_mtx;
	char               *dsi_path;
	struct sockaddr_un dsi_addr;
	int                dsi_fd;
} dctl_sock_info_t;

typedef void *thr_func_t(void *);

/* Thread pool data */
typedef struct dctl_thr_info {
	thr_func_t *dti_thr_func;

	pthread_mutex_t dti_mtx; /* protects the thread lists and dti_free */
	list_t dti_list;         /* list of threads in the thread pool */
	list_t dti_join_list;    /* list of threads that are waiting to be
	                            joined */
	int    dti_free;         /* number of free worker threads */

	int dti_min;
	int dti_max_free;

	boolean_t dti_exit; /* global termination flag */
} dctl_thr_info_t;

/* Messaging functions functions */
int dctl_read_msg(int fd, dctl_cmd_t *cmd);
int dctl_send_msg(int fd, dctl_cmd_t *cmd);

int dctl_read_data(int fd, void *ptr, size_t size);
int dctl_send_data(int fd, const void *ptr, size_t size);

/* Thread pool functions */
int dctl_thr_pool_create(int min_thr, int max_free_thr,
    thr_func_t *thr_func);
void dctl_thr_pool_stop();

void dctl_thr_join();
void dctl_thr_die(wthr_info_t *thr);
void dctl_thr_rebalance(wthr_info_t *thr, boolean_t set_free);

#endif
