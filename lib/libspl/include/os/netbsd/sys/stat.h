// SPDX-License-Identifier: CDDL-1.0
#ifndef _LIBSPL_SYS_STAT_H
#define	_LIBSPL_SYS_STAT_H

#include_next <sys/stat.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/dkio.h>

#define	stat64	stat

#ifndef MAXOFFSET_T
#ifdef OFF_MAX
#define	MAXOFFSET_T	OFF_MAX
#else
#define	MAXOFFSET_T	((off_t)(((uint64_t)1 << 63) - 1))
#endif
#endif

#ifndef _KERNEL
static __inline int
fstat64(int fd, struct stat *sb)
{
	int ret;

	ret = fstat(fd, sb);
	if (ret == 0 && (S_ISCHR(sb->st_mode) || S_ISBLK(sb->st_mode))) {
		off_t ms = 0;

		if (ioctl(fd, DIOCGMEDIASIZE, &ms) == 0 && ms > 0)
			sb->st_size = ms;
	}
	return (ret);
}
#endif

static inline int
fstat64_blk(int fd, struct stat64 *st)
{
	if (fstat64(fd, st) == -1)
		return (-1);
	return (0);
}

#endif /* _LIBSPL_SYS_STAT_H */
