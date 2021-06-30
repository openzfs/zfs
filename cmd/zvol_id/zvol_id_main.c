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
/*
 * Copyright (c) 2011, Fajar A. Nugraha.  All rights reserved.
 * Use is subject to license terms.
 */

#include <ctype.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <linux/ioctl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/zfs_znode.h>
#include <sys/fs/zfs.h>

static int
ioctl_get_msg(char *var, int fd)
{
	int ret;
	char msg[ZFS_MAX_DATASET_NAME_LEN];

	ret = ioctl(fd, BLKZNAME, msg);
	if (ret < 0) {
		return (ret);
	}

	snprintf(var, ZFS_MAX_DATASET_NAME_LEN, "%s", msg);
	return (ret);
}

int
main(int argc, char **argv)
{
	int fd = -1, ret = 0, status = EXIT_FAILURE;
	char zvol_name[ZFS_MAX_DATASET_NAME_LEN];
	char *zvol_name_part = NULL;
	char *dev_name;
	struct stat64 statbuf;
	int dev_minor, dev_part;
	int i;

	if (argc < 2) {
		fprintf(stderr, "Usage: %s /dev/zvol_device_node\n", argv[0]);
		goto fail;
	}

	dev_name = argv[1];
	ret = stat64(dev_name, &statbuf);
	if (ret != 0) {
		fprintf(stderr, "Unable to access device file: %s\n", dev_name);
		goto fail;
	}

	dev_minor = minor(statbuf.st_rdev);
	dev_part = dev_minor % ZVOL_MINORS;

	fd = open(dev_name, O_RDONLY);
	if (fd < 0) {
		fprintf(stderr, "Unable to open device file: %s\n", dev_name);
		goto fail;
	}

	ret = ioctl_get_msg(zvol_name, fd);
	if (ret < 0) {
		fprintf(stderr, "ioctl_get_msg failed: %s\n", strerror(errno));
		goto fail;
	}
	if (dev_part > 0)
		ret = asprintf(&zvol_name_part, "%s-part%d", zvol_name,
		    dev_part);
	else
		ret = asprintf(&zvol_name_part, "%s", zvol_name);

	if (ret == -1 || zvol_name_part == NULL)
		goto fail;

	for (i = 0; i < strlen(zvol_name_part); i++) {
		if (isblank(zvol_name_part[i]))
			zvol_name_part[i] = '+';
	}

	printf("%s\n", zvol_name_part);
	status = EXIT_SUCCESS;

fail:
	if (zvol_name_part)
		free(zvol_name_part);
	if (fd >= 0)
		close(fd);

	return (status);
}
