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

#ifndef _LIBUZFS_H_
#define	_LIBUZFS_H_ extern __attribute__((visibility("default")))

#include <sys/zfs_ioctl.h>
#include <libzfs.h>

#ifdef __cplusplus
extern "C" {
#endif

#define	PEND_CONNECTIONS 10

#define	UZFS_SOCK "/tmp/uzfs.sock"

// LOCK_FILE to prevent parallel run of uzfs
#define	LOCK_FILE "/tmp/uzfs.lock"

#define	SET_ERR(err) (errno = err, -1)

typedef struct uzfs_ioctl {
	uint64_t packet_size;
	uint64_t ioc_num;
	uint64_t his_len;
	int ioc_ret;
} uzfs_ioctl_t;

typedef struct uzfs_info {
	uzfs_ioctl_t uzfs_cmd;
	int uzfs_recvfd;
} uzfs_info_t;

typedef struct uzfs_monitor {
	int mon_fd;
	int mon_reserved;
	pthread_t mon_tid;
	void (*mon_action)(int, void *);
	void *mon_arg;
} uzfs_mon_t;

/* _UZFS_IOC(ioctl_number, is_config_command, smush, description) */
#define	UZFS_IOCTL_LIST                                         \
    _UZFS_IOC(ZFS_IOC_POOL_CREATE, 1, 0, " create pool")

#define	MAX_NVLIST_SRC_SIZE (128 * 1024 * 1024)

_LIBUZFS_H_ int uzfs_handle_ioctl(const char *pool, zfs_cmd_t *zc,
    uzfs_info_t *ucmd_info);
_LIBUZFS_H_ int uzfs_recv_ioctl(int fd, zfs_cmd_t *zc, uzfs_info_t *ucmd_info);
_LIBUZFS_H_ int uzfs_send_response(int fd, zfs_cmd_t *zc,
    uzfs_info_t *ucmd_info);
_LIBUZFS_H_ int uzfs_send_ioctl(int fd, unsigned long request, zfs_cmd_t *zc);
_LIBUZFS_H_ int libuzfs_run_ioctl_server(void);
_LIBUZFS_H_ int libuzfs_client_init(libzfs_handle_t *g_zfs);
extern int uzfs_recv_response(int fd, zfs_cmd_t *zc);
extern int uzfs_client_init(const char *sock_path);
extern int is_main_thread(void);

static inline int
is_config_command(unsigned long ioc_num)
{
	switch (ioc_num) {

#define	_UZFS_IOC(ioc, config, smush, desc) \
	case ioc:                           \
		return (config);            \
		break;

		UZFS_IOCTL_LIST

#undef _UZFS_IOC
	}
	return (0);
}

static inline int
should_smush_nvlist(unsigned long ioc_num)
{
	switch (ioc_num) {

#define	_UZFS_IOC(ioc, config, smush, desc) \
	case ioc:                           \
		return (smush);             \
		break;

		UZFS_IOCTL_LIST

#undef	_UZFS_IOC
	}
	return (0);
}

#ifdef __cplusplus
}
#endif

#endif /* _LIBUZFS_H */
