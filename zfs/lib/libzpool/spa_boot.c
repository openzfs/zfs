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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#pragma ident	"@(#)spa_boot.c	1.1	08/04/09 SMI"

#include <sys/spa.h>
#include <sys/sunddi.h>

char *
spa_get_bootfs()
{
	char *zfs_bp;

	if (ddi_prop_lookup_string(DDI_DEV_T_ANY, ddi_root_node(),
	    DDI_PROP_DONTPASS, "zfs-bootfs", &zfs_bp) !=
	    DDI_SUCCESS)
		return (NULL);
	return (zfs_bp);
}

void
spa_free_bootfs(char *bootfs)
{
	ddi_prop_free(bootfs);
}

/*
 * Calculate how many device pathnames are in devpath_list.
 * The devpath_list could look like this:
 *
 *	"/pci@1f,0/ide@d/disk@0,0:a /pci@1f,o/ide@d/disk@2,0:a"
 */
static int
spa_count_devpath(char *devpath_list)
{
	int numpath;
	char *tmp_path, *blank;

	numpath = 0;
	tmp_path = devpath_list;

	/* skip leading blanks */
	while (*tmp_path == ' ')
		tmp_path++;

	while ((blank = strchr(tmp_path, ' ')) != NULL) {

		numpath++;
		/* skip contiguous blanks */
		while (*blank == ' ')
			blank++;
		tmp_path = blank;
	}

	if (strlen(tmp_path) > 0)
		numpath++;

	return (numpath);
}

/*
 * Only allow booting the device if it has the same vdev information as
 * the most recently updated vdev (highest txg) and is in a valid state.
 *
 * GRUB passes online/active device path names, e.g.
 *	"/pci@1f,0/ide@d/disk@0,0:a /pci@1f,o/ide@d/disk@2,0:a"
 * to the kernel. The best vdev should have the same matching online/active
 * list as what GRUB passes in.
 */
static int
spa_check_devstate(char *devpath_list, char *dev, nvlist_t *conf)
{
	nvlist_t *nvtop, **child;
	uint_t label_path, grub_path, c, children;
	char *type;

	VERIFY(nvlist_lookup_nvlist(conf, ZPOOL_CONFIG_VDEV_TREE,
	    &nvtop) == 0);
	VERIFY(nvlist_lookup_string(nvtop, ZPOOL_CONFIG_TYPE, &type) == 0);

	if (strcmp(type, VDEV_TYPE_DISK) == 0)
		return (spa_rootdev_validate(nvtop)? 0 : EINVAL);

	ASSERT(strcmp(type, VDEV_TYPE_MIRROR) == 0);

	VERIFY(nvlist_lookup_nvlist_array(nvtop, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) == 0);

	/*
	 * Check if the devpath_list is the same as the path list in conf.
	 * If these two lists are different, then the booting device is not an
	 * up-to-date device that can be booted.
	 */
	label_path = 0;
	for (c = 0; c < children; c++) {
		char *physpath;

		if (nvlist_lookup_string(child[c], ZPOOL_CONFIG_PHYS_PATH,
		    &physpath) != 0)
			return (EINVAL);

		if (spa_rootdev_validate(child[c])) {
			if (strstr(devpath_list, physpath) == NULL)
				return (EINVAL);
			label_path++;
		} else {
			char *blank;

			if (blank = strchr(dev, ' '))
				*blank = '\0';
			if (strcmp(physpath, dev) == 0)
				return (EINVAL);
			if (blank)
				*blank = ' ';
		}
	}

	grub_path = spa_count_devpath(devpath_list);

	if (label_path != grub_path)
		return (EINVAL);

	return (0);
}

/*
 * Given a list of vdev physpath names, pick the vdev with the most recent txg,
 * and return the point of the device's physpath in the list and the device's
 * label configuration. The content of the label would be the most recent
 * updated information.
 */
int
spa_get_rootconf(char *devpath_list, char **bestdev, nvlist_t **bestconf)
{
	nvlist_t *conf = NULL;
	char *dev = NULL;
	uint64_t txg = 0;
	char *devpath, *blank;

	devpath = devpath_list;
	dev = devpath;

	while (devpath[0] == ' ')
		devpath++;

	while ((blank = strchr(devpath, ' ')) != NULL) {
		*blank = '\0';
		spa_check_rootconf(devpath, &dev, &conf, &txg);
		*blank = ' ';

		while (*blank == ' ')
			blank++;
		devpath = blank;
	}

	/* for the only or the last devpath in the devpath_list */
	if (strlen(devpath) > 0)
		spa_check_rootconf(devpath, &dev, &conf, &txg);

	if (conf == NULL)
		return (EINVAL);

	/*
	 * dev/conf is the vdev with the most recent txg.
	 * Check if the device is in a bootable state.
	 * dev may have a trailing blank since it points to a string
	 * in the devpath_list.
	 */
	if (spa_check_devstate(devpath_list, dev, conf) != 0)
		return (EINVAL);

	*bestdev = dev;
	*bestconf = conf;
	return (0);
}
