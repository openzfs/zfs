// SPDX-License-Identifier: CDDL-1.0
/*
 * This file and its contents are supplied under the terms of the
 * Common Development and Distribution License ("CDDL"), version 1.0.
 * You may only use this file in accordance with the terms of version
 * 1.0 of the CDDL.
 *
 * A full copy of the text of the CDDL should have accompanied this
 * source.  A copy of the CDDL is also available via the Internet at
 * https://opensource.org/license/CDDL-1.0.
 */

#ifndef	_SYS_ZFS_FILE_H
#define	_SYS_ZFS_FILE_H

#include <sys/zfs_context.h>

/*
 * loff_t is a Linux kernel/VFS type. glibc and musl expose it to user
 * space via <fcntl.h>, but FreeBSD libc does not. For FreeBSD user
 * space we map loff_t to off_t so the shared interfaces that use the
 * loff_t name still compile. The FreeBSD kernel gets loff_t from its
 * own linux-compat headers.
 */
#if !defined(_KERNEL) && defined(__FreeBSD__)
typedef off_t loff_t;
#endif

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

int zfs_file_open(const char *path, int flags, int mode, cred_t *cr,
    zfs_file_t **fp);
void zfs_file_close(zfs_file_t *fp);

int zfs_file_write(zfs_file_t *fp, const void *buf, size_t len, ssize_t *resid);
int zfs_file_pwrite(zfs_file_t *fp, const void *buf, size_t len, loff_t off,
    uint8_t ashift, ssize_t *resid);
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
