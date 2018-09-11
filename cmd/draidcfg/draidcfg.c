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
 * Copyright (c) 2016 Intel Corporation.
 */


#include <libzfs.h>
#include <libnvpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <libintl.h>

#include "draid_config.h"
#include "draid_permutation.h"


static struct vdev_draid_configuration *
find_known_config(const uint64_t data, const uint64_t parity,
    const uint64_t spare, const uint64_t children)
{
	static const uint64_t data22[2] = {2, 2};
	static const uint64_t data24[2] = {4, 4};
	static const uint64_t data28[2] = {8, 8};
	static const uint64_t data64[6] = {4, 4, 4, 4, 4, 4};
	static const uint64_t data48[4] = {8, 8, 8, 8};

	/* P  D  D...  P  D  D...  S */
	static const uint64_t bases7[1][7] = {{1, 2, 4, 3, 6, 5, 0}};

	static const uint64_t bases11[1][11] = {{
	    1, 4, 5, 9, 3, 2, 8, 10, 7, 6, 0}};

	static const uint64_t bases19[1][19] = {{
	    1, 5, 6, 11, 17, 9, 7, 16, 4, 10, 12, 3, 15, 18, 14, 13, 8, 2, 0}};

	static const uint64_t bases23[1][23] = {{
	    1, 8, 18, 6, 2, 16, 13, 12, 4, 9, 3, 10, 11, 19, 14, 20, 22,
	    15, 5, 17, 21, 7, 0}};

	static const uint64_t bases31[1][31] = {{
	    1, 8, 2, 16, 4, 17, 12, 3, 24, 6, 10, 18, 20, 5, 9, 15, 27, 30, 23,
	    29, 7, 25, 14, 19, 28, 26, 22, 21, 13, 11, 0}};

	static const uint64_t bases41[1][41] = {{
	    1, 25, 10, 4, 18, 40, 16, 31, 37, 23, 6, 27, 19,
	    24, 26, 35, 14, 22, 17, 15, 36, 39, 32, 21, 33,
	    5, 2, 9, 20, 8, 11, 29, 28, 3, 34, 30, 12, 13, 38, 7, 0}};

	static struct vdev_draid_configuration known_cfgs[6] = {
	    {
	    .dcf_groups = 2, .dcf_data = data22, .dcf_parity = 1,
	    .dcf_spare = 1, .dcf_children = 7,
	    .dcf_bases = 1, .dcf_base_perms = &bases7[0][0]
	    },
	    {
	    .dcf_groups = 2, .dcf_data = data24, .dcf_parity = 1,
	    .dcf_spare = 1, .dcf_children = 11,
	    .dcf_bases = 1, .dcf_base_perms = &bases11[0][0]
	    },
	    {
	    .dcf_groups = 2, .dcf_data = data28, .dcf_parity = 1,
	    .dcf_spare = 1, .dcf_children = 19,
	    .dcf_bases = 1, .dcf_base_perms = &bases19[0][0]
	    },
	    {
	    .dcf_groups = 2, .dcf_data = data28, .dcf_parity = 3,
	    .dcf_spare = 1, .dcf_children = 23,
	    .dcf_bases = 1, .dcf_base_perms = &bases23[0][0]
	    },
	    {
	    .dcf_groups = 6, .dcf_data = data64, .dcf_parity = 1,
	    .dcf_spare = 1, .dcf_children = 31,
	    .dcf_bases = 1, .dcf_base_perms = &bases31[0][0]
	    },
	    {
	    .dcf_groups = 4, .dcf_data = data48, .dcf_parity = 2,
	    .dcf_spare = 1, .dcf_children = 41,
	    .dcf_bases = 1, .dcf_base_perms = &bases41[0][0]
	    },
	};

	int i;

	for (i = 0; i < sizeof (known_cfgs) / sizeof (known_cfgs[0]); i++) {
		struct vdev_draid_configuration *cfg = &known_cfgs[i];

		if (data == cfg->dcf_data[0] && parity == cfg->dcf_parity &&
		    spare == cfg->dcf_spare && children == cfg->dcf_children)
			return (cfg);
	}

	return (NULL);
}

static struct vdev_draid_configuration *
create_config(const uint64_t groups, const uint64_t parity,
    const uint64_t spare, const uint64_t children)
{
	struct vdev_draid_configuration *cfg = calloc(1, sizeof (*cfg));
	uint64_t *array, data, extra;

	assert(cfg != NULL);

	cfg->dcf_groups = groups;
	array = calloc(groups, sizeof (uint64_t));
	data = (children - spare) / groups - parity;
	extra = (children - spare) % groups;
	for (int i = 0; i < groups; i++) {
		array[i] = data;
		if (i < extra)
			array[i] += 1;
	}
	cfg->dcf_data = array;

	cfg->dcf_parity = parity;
	cfg->dcf_spare = spare;
	cfg->dcf_children = children;

	cfg->dcf_bases = 0;
	cfg->dcf_base_perms = NULL;
	if (draid_permutation_generate(cfg) != 0) {
		free((void *)array);
		free(cfg);
		return (NULL);
	}

	assert(cfg->dcf_bases != 0);
	assert(cfg->dcf_base_perms != NULL);
	return (cfg);
}

static inline void
draidcfg_free(struct vdev_draid_configuration *cfg)
{
	free((void *)cfg->dcf_data);
	free((void *)cfg->dcf_base_perms);
	free(cfg);
}

static int
draidcfg_create_file(const uint64_t groups, const uint64_t parity,
    const uint64_t spare, const uint64_t children, const char *path)
{
	FILE *fp;
	size_t len;
	int ret = 0;
	void *packed;
	nvlist_t *nvl;
	boolean_t freecfg = B_FALSE;
	struct vdev_draid_configuration *cfg;
	uint8_t *val;

	/* Number of drives must fit into an unsigned 8-bit int (<=256) */
	if (children - 1 > VDEV_DRAID_U8_MAX) {
		fprintf(stderr, "Configuration for over %u children "
		    "is not supported\n", VDEV_DRAID_U8_MAX + 1);
		return (1);
	}

	cfg = find_known_config(children / groups, parity, spare, children);
	if (cfg == NULL) {
		cfg = create_config(groups, parity, spare, children);
		if (cfg == NULL) {
			fprintf(stderr, "Cannot create"
			    "supported configuration\n");
			return (1);
		}
		freecfg = B_TRUE;
	}

	fp = fopen(path, "w+");
	if (fp == NULL) {
		fprintf(stderr, "Cannot open file %s for write\n", path);
		if (freecfg)
			draidcfg_free(cfg);
		return (1);
	}

	nvl = fnvlist_alloc();

	/* Store the number groups followed by an array of their sizes */
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_GROUPS, groups);
	val = calloc(groups, sizeof (uint8_t));
	for (int i = 0; i < groups; i++) {
		ASSERT3U(cfg->dcf_data[i], <=, VDEV_DRAID_U8_MAX);
		val[i] = (uint8_t)cfg->dcf_data[i];
	}
	fnvlist_add_uint8_array(nvl, ZPOOL_CONFIG_DRAIDCFG_DATA, val, groups);
	free(val);

	/* Store parity, spare count, and number of drives in config */
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_PARITY, parity);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_SPARE, spare);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_CHILDREN, children);

	/* Store the number of permutations followed by the permutations */
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_BASE, cfg->dcf_bases);
	val = calloc(children * cfg->dcf_bases, sizeof (uint8_t));
	for (int i = 0; i < cfg->dcf_bases; i++) {
		for (int j = 0; j < children; j++) {
			uint64_t c = cfg->dcf_base_perms[i * children + j];

			ASSERT3U(c, <, children);
			ASSERT3U(c, <=, VDEV_DRAID_U8_MAX);
			val[i * children + j] = (uint8_t)c;
		}
	}
	fnvlist_add_uint8_array(nvl, ZPOOL_CONFIG_DRAIDCFG_PERM,
	    val, children * cfg->dcf_bases);
	free(val);

	assert(vdev_draid_config_validate(NULL, nvl) == DRAIDCFG_OK);

	packed = fnvlist_pack_xdr(nvl, &len);
	if (fwrite(packed, 1, len, fp) != len) {
		ret = 1;
		fprintf(stderr, "Cannot write %lu bytes to %s\n", len, path);
	}

	fnvlist_pack_free(packed, len);
	fnvlist_free(nvl);
	if (freecfg)
		draidcfg_free(cfg);
	fclose(fp);
	return (ret);
}

static void
draidcfg_print(nvlist_t *config)
{
	uint_t c;
	uint8_t *val = NULL;
	uint64_t n, g, p, s, b;

	n = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_CHILDREN);
	g = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_GROUPS);
	p = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_PARITY);
	s = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_SPARE);
	b = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_BASE);

	printf("dRAID%lu vdev of %lu child drives"
	    " in %lu groups with %lu distributed spares\n", p, n, g, s);
	VERIFY0(nvlist_lookup_uint8_array(config,
	    ZPOOL_CONFIG_DRAIDCFG_DATA, &val, &c));
	ASSERT3U(c, ==, g);
	for (int i = 0; i < g; i++)
		printf(" (%u + %lu)\n", val[i], p);

	printf("Using %lu base permutation%s\n", b, b > 1 ? "s" : "");

	VERIFY0(nvlist_lookup_uint8_array(config,
	    ZPOOL_CONFIG_DRAIDCFG_PERM, &val, &c));
	ASSERT3U(c, ==, b * n);

	for (int i = 0; i < b; i++) {
		printf("  ");
		for (int j = 0; j < n; j++)
			printf("%*u,", n > 99 ? 3 : 2, val[i * n + j]);
		printf("\n");
	}
}

static inline int usage(void)
{
	printf("draidcfg -n children -d data -p parity"
	    " -s nspare <configfile>\n");
	printf("draidcfg -n children -g groups -p parity"
	    " -s nspare <configfile>\n");
	printf("draidcfg -r <configfile>\n");
	printf(gettext("Note: (children - nspare) must be a multiple of"
	    " (data + parity)\n"));
	return (1);
}

int
main(int argc, char **argv)
{
	boolean_t read = B_FALSE;
	char *cfg = NULL;
	uint64_t data = 0, parity = 0, spare = 0, children = 0, groups = 0;
	int errors, c;

	while ((c = getopt(argc, argv, "rn:d:p:s:g:")) != -1) {
		char *endptr = NULL;
		errno = 0;

		switch (c) {
		case 'r':
			read = B_TRUE;
			break;
		case 'n':
			children = strtoull(optarg, &endptr, 0);
			break;
		case 'd':
			data = strtoull(optarg, &endptr, 0);
			break;
		case 'p':
			parity = strtoull(optarg, &endptr, 0);
			break;
		case 's':
			spare = strtoull(optarg, &endptr, 0);
			break;
		case 'g':
			groups = strtoull(optarg, &endptr, 0);
			break;
		case ':':
			fprintf(stderr, gettext("Missing argument for "
			    "'%c' option\n"), optopt);
			return (usage());
		case '?':
			fprintf(stderr, gettext("Invalid option '%c'\n"),
			    optopt);
			return (usage());
		}

		if (errno != 0 || (endptr && *endptr != '\0')) {
			fprintf(stderr, gettext("Invalid -%c value: %s\n"),
			    c, optarg);
			return (usage());
		}
	}

	argc -= optind;
	argv += optind;

	if (argc < 1) {
		fprintf(stderr,
		    gettext("Missing configuration file argument\n"));
		return (usage());
	}

	cfg = argv[0];

	if (read) {
		nvlist_t *nvl = draidcfg_read_file(cfg);

		if (optind > 2)
			fprintf(stderr,
			    gettext("Ignoring flags other than -r\n"));
		if (nvl == NULL) {
			fprintf(stderr,
			    gettext("Invalid configuration\n"));
			return (1);
		}
		draidcfg_print(nvl);
		nvlist_free(nvl);
		return (0);
	}

	errors = 0;
	if (optind < 4) {
		fprintf(stderr, gettext("Invalid argument list\n"));
		errors++;
	}
	if (groups == 0 && (data == 0 || data > children)) {
		fprintf(stderr, gettext("Missing or invalid -d argument\n"));
		errors++;
	}
	if (parity <= 0 || parity > VDEV_RAIDZ_MAXPARITY) {
		fprintf(stderr,
		    gettext("Invalid parity %lu, must be [1,%d]\n"),
		    parity, VDEV_RAIDZ_MAXPARITY);
		errors++;
	}

	if (spare == 0) {
		fprintf(stderr, gettext("Missing or invalid -s argument\n"));
		errors++;
	}

	if (groups == 0 && (children % (data + parity) != spare)) {
		fprintf(stderr, gettext("Invalid draid configration\n"));
		errors++;
	}

	if (groups == 0)
		groups = (children - spare) / (data + parity);
	if (errors == 0)
		errors =
		    draidcfg_create_file(groups, parity, spare, children, cfg);

	if (errors)
		return (usage());
	return (0);
}
