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

#ifdef _KERNEL
#include <linux/kernel.h>
#else
#include <sys/stat.h>
#include <libintl.h>
#endif

boolean_t
vdev_draid_config_validate(const vdev_t *vd, nvlist_t *config)
{
	int i;
	uint_t c;
	uint8_t *perm = NULL;
	uint64_t n, d, p, s, b;

	if (nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_DRAIDCFG_CHILDREN, &n) != 0) {
		draid_dbg(0, "Missing %s in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_CHILDREN);
		return (B_FALSE);
	}

	if (n - 1 > VDEV_DRAID_U8_MAX) {
		draid_dbg(0, "%s configuration too large: "U64FMT"\n",
		    ZPOOL_CONFIG_DRAIDCFG_CHILDREN, n);
		return (B_FALSE);
	}
	if (vd != NULL && n != vd->vdev_children)
		return (B_FALSE);

	if (nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_DRAIDCFG_PARITY, &p) != 0) {
		draid_dbg(0, "Missing %s in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_PARITY);
		return (B_FALSE);
	}

	if (vd != NULL && p != vd->vdev_nparity)
		return (B_FALSE);

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_DATA, &d) != 0) {
		draid_dbg(0, "Missing %s in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_DATA);
		return (B_FALSE);
	}

	if (nvlist_lookup_uint64(config,
	    ZPOOL_CONFIG_DRAIDCFG_SPARE, &s) != 0) {
		draid_dbg(0, "Missing %s in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_SPARE);
		return (B_FALSE);
	}

	if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_BASE, &b) != 0) {
		draid_dbg(0, "Missing %s in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_BASE);
		return (B_FALSE);
	}

	if (n == 0 || d == 0 || p == 0 || s == 0 || b == 0) {
		draid_dbg(0, "Zero n/d/p/s/b\n");
		return (B_FALSE);
	}

	if (p > VDEV_RAIDZ_MAXPARITY) {
		draid_dbg(0, "Invalid parity "U64FMT"\n", p);
		return (B_FALSE);
	}

	if ((n - s) % (p + d) != 0) {
		draid_dbg(0, U64FMT" mod "U64FMT" is not 0\n", n - s, p + d);
		return (B_FALSE);
	}

	if (nvlist_lookup_uint8_array(config,
	    ZPOOL_CONFIG_DRAIDCFG_PERM, &perm, &c) != 0) {
		draid_dbg(0, "Missing %s in configuration\n",
		    ZPOOL_CONFIG_DRAIDCFG_PERM);
		return (B_FALSE);
	}

	if (c != b * n) {
		draid_dbg(0,
		    "Permutation array has %u items, but "U64FMT" expected\n",
		    c, b * n);
		return (B_FALSE);
	}

	for (i = 0; i < b; i++) {
		int j, k;
		for (j = 0; j < n; j++) {
			uint64_t val = perm[i * n + j];

			if (val >= n) {
				draid_dbg(0,
				    "Invalid value "U64FMT" in "
				    "permutation %d\n", val, i);
				return (B_FALSE);
			}

			for (k = 0; k < j; k++) {
				if (val == perm[i * n + k]) {
					draid_dbg(0,
					    "Duplicated value "U64FMT" in "
					    "permutation %d\n",
					    val, i);
					return (B_FALSE);
				}
			}
		}
	}

	return (B_TRUE);
}

#if !defined(_KERNEL)
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

	if (!vdev_draid_config_validate(NULL, config)) {
		nvlist_free(config);
		return (NULL);
	}

	return (config);
}
#endif /* _KERNEL */

#if defined(_KERNEL)
EXPORT_SYMBOL(vdev_draid_config_validate);
#endif
