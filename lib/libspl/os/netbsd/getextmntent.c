// SPDX-License-Identifier: CDDL-1.0
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <sys/mnttab.h>
#include <sys/types.h>
#include <sys/sysmacros.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <libzutil.h>

int
getextmntent(const char *path, struct mnttab *entry, struct stat64 *statbuf)
{
	struct statvfs sfs;

	if (strlen(path) >= MAXPATHLEN) {
		(void) fprintf(stderr, "invalid object; pathname too long\n");
		return (-1);
	}

	if (stat64(path, statbuf) != 0) {
		(void) fprintf(stderr, "cannot open '%s': %s\n",
		    path, zfs_strerror(errno));
		return (-1);
	}

	if (statvfs(path, &sfs) != 0) {
		(void) fprintf(stderr, "%s: %s\n", path,
		    zfs_strerror(errno));
		return (-1);
	}
	statfs2mnttab(&sfs, entry);
	return (0);
}
