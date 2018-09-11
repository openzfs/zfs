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
 * Copyright (c) 2018 Intel Corporation.
 */

#include <sys/zfs_context.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_draid_impl.h>
#include <sys/nvpair.h>

draidcfg_err_t
vdev_draid_config_validate(const vdev_t *vd, nvlist_t *config)
{
	uint_t c;
	uint8_t *data = NULL;
	uint8_t *perm = NULL;
	uint64_t n, g, p, s, b;

	/* Validate configuration children exists and is within range. */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_CHILDREN, &n))
		return (DRAIDCFG_ERR_CHILDREN_MISSING);

	if (n == 0 || (n - 1) > VDEV_DRAID_U8_MAX)
		return (DRAIDCFG_ERR_CHILDREN_INVALID);

	if (vd != NULL && n != vd->vdev_children)
		return (DRAIDCFG_ERR_CHILDREN_MISMATCH);

	/* Validate configuration parity exists and is within range. */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_PARITY, &p))
		return (DRAIDCFG_ERR_PARITY_MISSING);

	if (p == 0 || p > VDEV_RAIDZ_MAXPARITY)
		return (DRAIDCFG_ERR_PARITY_INVALID);

	if (vd != NULL && p != vd->vdev_nparity)
		return (DRAIDCFG_ERR_PARITY_MISMATCH);

	/* Validate configuration groups exists and is within range. */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_GROUPS, &g))
		return (DRAIDCFG_ERR_GROUPS_MISSING);

	if (g == 0)
		return (DRAIDCFG_ERR_GROUPS_INVALID);

	/* Validate configuration spares exists and is within range. */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_SPARE, &s))
		return (DRAIDCFG_ERR_SPARES_MISSING);

	if (s == 0)
		return (DRAIDCFG_ERR_SPARES_INVALID);

	/*
	 * Validate configuration data array exists and that the array size
	 * matches the expected number of groups.  Furthermore, verify the
	 * number of devices in each group is below average (plus one) to
	 * confirm the group sizes are approximately equal in size.
	 */
	if (nvlist_lookup_uint8_array(config,
	    ZPOOL_CONFIG_DRAIDCFG_DATA, &data, &c)) {
		return (DRAIDCFG_ERR_DATA_MISSING);
	}

	if (c != g)
		return (DRAIDCFG_ERR_DATA_MISMATCH);

	uint64_t total_d_p = 0;
	uint64_t max = (n - s) / g + 1;

	for (uint64_t i = 0; i < g; i++) {
		uint64_t val = data[i] + p;

		if (val > max)
			return (DRAIDCFG_ERR_DATA_INVALID);

		total_d_p += val;
	}

	/* Validate configuration base exists and is within range. */
	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_BASE, &b))
		return (DRAIDCFG_ERR_BASE_MISSING);

	if (b == 0)
		return (DRAIDCFG_ERR_BASE_INVALID);

	/*
	 * Validate that the total number of dRAID children minus the number
	 * of distributed spares equals the number of data and parity devices.
	 * This is a hard constraint of the distribution parity implementation.
	 */
	if ((n - s) != total_d_p)
		return (DRAIDCFG_ERR_LAYOUT);

	if (nvlist_lookup_uint8_array(config,
	    ZPOOL_CONFIG_DRAIDCFG_PERM, &perm, &c)) {
		return (DRAIDCFG_ERR_PERM_MISSING);
	}

	/*
	 * Validate the permutation array size matches the expected size,
	 * that its elements are within the allowed range, and that there
	 * are no duplicates.
	 */
	if (c != b * n)
		return (DRAIDCFG_ERR_PERM_MISMATCH);

	for (uint64_t i = 0; i < b; i++) {
		for (uint64_t j = 0; j < n; j++) {
			uint64_t val = perm[i * n + j];

			if (val >= n)
				return (DRAIDCFG_ERR_PERM_INVALID);

			for (uint64_t k = 0; k < j; k++) {
				if (val == perm[i * n + k])
					return (DRAIDCFG_ERR_PERM_DUPLICATE);
			}
		}
	}

	return (DRAIDCFG_OK);
}

#if !defined(_KERNEL)
#include <sys/stat.h>

boolean_t
vdev_draid_config_add(nvlist_t *top, nvlist_t *draidcfg)
{
	char *type;
	uint64_t parity;
	nvlist_t **children = NULL;
	uint_t c = 0;

	if (draidcfg == NULL)
		return (B_FALSE);

	type = fnvlist_lookup_string(top, ZPOOL_CONFIG_TYPE);
	if (strcmp(type, VDEV_TYPE_DRAID) != 0)
		return (B_FALSE);

	parity = fnvlist_lookup_uint64(top, ZPOOL_CONFIG_NPARITY);
	if (parity != fnvlist_lookup_uint64(draidcfg,
	    ZPOOL_CONFIG_DRAIDCFG_PARITY))
		return (B_FALSE);

	VERIFY0(nvlist_lookup_nvlist_array(top,
	    ZPOOL_CONFIG_CHILDREN, &children, &c));
	if (c !=
	    fnvlist_lookup_uint64(draidcfg, ZPOOL_CONFIG_DRAIDCFG_CHILDREN))
		return (B_FALSE);

	/* HH: todo: check permutation array csum */
	fnvlist_add_nvlist(top, ZPOOL_CONFIG_DRAIDCFG, draidcfg);
	return (B_TRUE);
}

nvlist_t *
draidcfg_read_file(const char *path)
{
	int fd;
	struct stat64 sb;
	char *buf;
	nvlist_t *config;

	if ((fd = open(path, O_RDONLY)) < 0) {
		(void) fprintf(stderr, "Cannot open '%s'\n", path);
		return (NULL);
	}

	if (fstat64(fd, &sb) != 0) {
		(void) fprintf(stderr, "Failed to stat '%s'\n", path);
		close(fd);
		return (NULL);
	}

	if (!S_ISREG(sb.st_mode)) {
		(void) fprintf(stderr, "Not a regular file '%s'\n", path);
		close(fd);
		return (NULL);
	}

	if ((buf = malloc(sb.st_size)) == NULL) {
		(void) fprintf(stderr, "Failed to allocate %llu bytes\n",
		    (u_longlong_t)sb.st_size);
		close(fd);
		return (NULL);
	}

	if (read(fd, buf, sb.st_size) != sb.st_size) {
		(void) fprintf(stderr, "Failed to read %llu bytes\n",
		    (u_longlong_t)sb.st_size);
		close(fd);
		free(buf);
		return (NULL);
	}

	(void) close(fd);

	if (nvlist_unpack(buf, sb.st_size, &config, 0) != 0) {
		(void) fprintf(stderr, "Failed to unpack nvlist\n");
		free(buf);
		return (NULL);
	}

	free(buf);

	switch (vdev_draid_config_validate(NULL, config)) {
	case DRAIDCFG_OK:
		return (config);
	case DRAIDCFG_ERR_CHILDREN_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_CHILDREN);
		break;
	case DRAIDCFG_ERR_CHILDREN_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_CHILDREN);
		break;
	case DRAIDCFG_ERR_CHILDREN_MISMATCH:
		(void) fprintf(stderr, "Inconsistent %s value in "
		    "configuration\n", ZPOOL_CONFIG_DRAIDCFG_CHILDREN);
		break;
	case DRAIDCFG_ERR_PARITY_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_PARITY);
		break;
	case DRAIDCFG_ERR_PARITY_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_PARITY);
		break;
	case DRAIDCFG_ERR_PARITY_MISMATCH:
		(void) fprintf(stderr, "Inconsistent %s value in "
		    "configuration\n", ZPOOL_CONFIG_DRAIDCFG_PARITY);
		break;
	case DRAIDCFG_ERR_GROUPS_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_GROUPS);
		break;
	case DRAIDCFG_ERR_GROUPS_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_GROUPS);
		break;
	case DRAIDCFG_ERR_SPARES_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_SPARE);
		break;
	case DRAIDCFG_ERR_SPARES_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_SPARE);
		break;
	case DRAIDCFG_ERR_DATA_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_DATA);
		break;
	case DRAIDCFG_ERR_DATA_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_DATA);
		break;
	case DRAIDCFG_ERR_DATA_MISMATCH:
		(void) fprintf(stderr, "Inconsistent %s value in "
		    "configuration\n", ZPOOL_CONFIG_DRAIDCFG_DATA);
		break;
	case DRAIDCFG_ERR_BASE_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_BASE);
		break;
	case DRAIDCFG_ERR_BASE_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_BASE);
		break;
	case DRAIDCFG_ERR_PERM_MISSING:
		(void) fprintf(stderr, "Missing %s key in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_PERM);
		break;
	case DRAIDCFG_ERR_PERM_INVALID:
		(void) fprintf(stderr, "Invalid %s value in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_PERM);
		break;
	case DRAIDCFG_ERR_PERM_MISMATCH:
		(void) fprintf(stderr, "Inconsistent %s value in "
		    "configuration\n", ZPOOL_CONFIG_DRAIDCFG_PERM);
		break;
	case DRAIDCFG_ERR_PERM_DUPLICATE:
		(void) fprintf(stderr, "Duplicate %s value in "
		    "configuration\n", ZPOOL_CONFIG_DRAIDCFG_PERM);
		break;
	case DRAIDCFG_ERR_LAYOUT:
		(void) fprintf(stderr, "Invalid dRAID layout "
		    "(n -s) != (d + p)\n");
		break;
	}

	nvlist_free(config);
	return (NULL);
}
#endif /* _KERNEL */

#if defined(_KERNEL)
EXPORT_SYMBOL(vdev_draid_config_validate);
#endif
