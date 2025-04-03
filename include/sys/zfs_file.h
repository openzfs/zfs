// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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

#ifndef	_SYS_ZFS_FILE_H
#define	_SYS_ZFS_FILE_H

#include <sys/zfs_context.h>

#ifndef _KERNEL
typedef struct zfs_file {
	int f_fd;
	int f_dump_fd;
} zfs_file_t;
#elif defined(__linux__) || defined(__FreeBSD__)
typedef struct file zfs_file_t;
#else
#error "unknown OS"
#endif

typedef struct zfs_file_attr {
	uint64_t	zfa_size;	/* file size */
	mode_t		zfa_mode;	/* file type */
} zfs_file_attr_t;

int zfs_file_open(const char *path, int flags, int mode, zfs_file_t **fp);
void zfs_file_close(zfs_file_t *fp);

int zfs_file_write(zfs_file_t *fp, const void *buf, size_t len, ssize_t *resid);
int zfs_file_pwrite(zfs_file_t *fp, const void *buf, size_t len, loff_t off,
    ssize_t *resid);
int zfs_file_read(zfs_file_t *fp, void *buf, size_t len, ssize_t *resid);
int zfs_file_pread(zfs_file_t *fp, void *buf, size_t len, loff_t off,
    ssize_t *resid);

int zfs_file_seek(zfs_file_t *fp, loff_t *offp, int whence);
int zfs_file_getattr(zfs_file_t *fp, zfs_file_attr_t *zfattr);
int zfs_file_fsync(zfs_file_t *fp, int flags);
int zfs_file_deallocate(zfs_file_t *fp, loff_t offset, loff_t len);
loff_t zfs_file_off(zfs_file_t *fp);
int zfs_file_unlink(const char *);

zfs_file_t *zfs_file_get(int fd);
void zfs_file_put(zfs_file_t *fp);
void *zfs_file_private(zfs_file_t *fp);

#endif /* _SYS_ZFS_FILE_H */
