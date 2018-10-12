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
draidcfg_find(const uint64_t data, const uint64_t parity,
    const uint64_t spare, const uint64_t children)
{
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
	    .dcf_data = 2, .dcf_parity = 1, .dcf_spare = 1, .dcf_children = 7,
	    .dcf_bases = 1, .dcf_base_perms = &bases7[0][0]
	    },
	    {
	    .dcf_data = 4, .dcf_parity = 1, .dcf_spare = 1, .dcf_children = 11,
	    .dcf_bases = 1, .dcf_base_perms = &bases11[0][0]
	    },
	    {
	    .dcf_data = 8, .dcf_parity = 1, .dcf_spare = 1, .dcf_children = 19,
	    .dcf_bases = 1, .dcf_base_perms = &bases19[0][0]
	    },
	    {
	    .dcf_data = 8, .dcf_parity = 3, .dcf_spare = 1, .dcf_children = 23,
	    .dcf_bases = 1, .dcf_base_perms = &bases23[0][0]
	    },
	    {
	    .dcf_data = 4, .dcf_parity = 1, .dcf_spare = 1, .dcf_children = 31,
	    .dcf_bases = 1, .dcf_base_perms = &bases31[0][0]
	    },
	    {
	    .dcf_data = 8, .dcf_parity = 2, .dcf_spare = 1, .dcf_children = 41,
	    .dcf_bases = 1, .dcf_base_perms = &bases41[0][0]
	    },
	};

	int i;

	for (i = 0; i < sizeof (known_cfgs) / sizeof (known_cfgs[0]); i++) {
		struct vdev_draid_configuration *cfg = &known_cfgs[i];

		if (data == cfg->dcf_data && parity == cfg->dcf_parity &&
		    spare == cfg->dcf_spare && children == cfg->dcf_children)
			return (cfg);
	}

	return (NULL);
}

static struct vdev_draid_configuration *
draidcfg_create(const uint64_t data, const uint64_t parity,
    const uint64_t spare, const uint64_t children)
{
	struct vdev_draid_configuration *cfg = calloc(1, sizeof (*cfg));

	assert(cfg != NULL);
	cfg->dcf_data = data;
	cfg->dcf_parity = parity;
	cfg->dcf_spare = spare;
	cfg->dcf_children = children;

	cfg->dcf_bases = 0;
	cfg->dcf_base_perms = NULL;
	if (draid_permutation_generate(cfg) != 0) {
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
	free((void *)cfg->dcf_base_perms);
	free(cfg);
}

static int
draidcfg_create_file(const uint64_t data, const uint64_t parity,
    const uint64_t spare, const uint64_t children, const char *path)
{
	FILE *fp;
	size_t len;
	int ret = 0;
	void *packed;
	nvlist_t *nvl;
	boolean_t freecfg = B_FALSE;
	struct vdev_draid_configuration *cfg;

	ASSERT(children != 0);
	ASSERT3U(children, <=, VDEV_DRAID_MAX_CHILDREN);

	if (children - 1 > VDEV_DRAID_U8_MAX) {
		fprintf(stderr, "Configuration for over %u children "
		    "is not supported\n", VDEV_DRAID_U8_MAX + 1);
		return (1);
	}

	cfg = draidcfg_find(data, parity, spare, children);
	if (cfg == NULL) {
		cfg = draidcfg_create(data, parity, spare, children);
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
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_DATA, data);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_PARITY, parity);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_SPARE, spare);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_CHILDREN, children);
	fnvlist_add_uint64(nvl, ZPOOL_CONFIG_DRAIDCFG_BASE, cfg->dcf_bases);

	if (children - 1 <= VDEV_DRAID_U8_MAX) {
		int i, j;
		uint8_t *val = calloc(children * cfg->dcf_bases, sizeof (*val));

		for (i = 0; i < cfg->dcf_bases; i++) {
			for (j = 0; j < children; j++) {
				uint64_t c =
				    cfg->dcf_base_perms[i * children + j];

				ASSERT3U(c, <, children);
				ASSERT3U(c, <=, VDEV_DRAID_U8_MAX);
				val[i * children + j] = (uint8_t)c;
			}
		}

		fnvlist_add_uint8_array(nvl, ZPOOL_CONFIG_DRAIDCFG_PERM,
		    val, children * cfg->dcf_bases);
		free(val);
	} else {
		ASSERT3U(children, ==, 0); /* not supported yet */
	}

	assert(vdev_draid_config_validate(NULL, nvl));

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
	uint8_t *perm = NULL;
	uint64_t n, d, p, s, b, i;

	n = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_CHILDREN);
	d = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_DATA);
	p = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_PARITY);
	s = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_SPARE);
	b = fnvlist_lookup_uint64(config, ZPOOL_CONFIG_DRAIDCFG_BASE);

	printf("dRAID%lu vdev of %lu child drives: %lu x (%lu data + "
	    "%lu parity) and %lu distributed spare\n",
	    p, n, (n - s) / (d + p), d, p, s);
	printf("Using %lu base permutation%s\n", b, b > 1 ? "s" : "");

	VERIFY0(nvlist_lookup_uint8_array(config,
	    ZPOOL_CONFIG_DRAIDCFG_PERM, &perm, &c));
	ASSERT3U(c, ==, b * n);

	for (i = 0; i < b; i++) {
		int j;

		printf("  ");
		for (j = 0; j < n; j++)
			printf("%*u,", n > 99 ? 3 : 2, perm[i * n + j]);
		printf("\n");
	}
}

static inline int usage(void)
{
	printf(gettext("draidcfg [-r] [-n children] [-d data] [-p parity]"
	    " [-s spare] <configfile>\n"));
	return (1);
}

int
main(int argc, char **argv)
{
	boolean_t read = B_FALSE;
	char *cfg = NULL;
	uint64_t data = 0, parity = 0, spare = 0, children = 0;
	int c;

	while ((c = getopt(argc, argv, "rn:d:p:s:")) != -1) {
		char *endptr;
		uint64_t *p = NULL;

		switch (c) {
		case 'r':
			read = B_TRUE;
			break;
		case 'n':
			p = &children;
		case 'd':
			if (p == NULL)
				p = &data;
		case 'p':
			if (p == NULL)
				p = &parity;
		case 's':
			if (p == NULL)
				p = &spare;

			errno = 0;
			*p = strtoull(optarg, &endptr, 0);
			if (errno != 0 || *endptr != '\0') {
				fprintf(stderr,
				    gettext("Invalid -%c value: %s\n"),
				    c, optarg);
				return (usage());
			}
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

		if (nvl == NULL) {
			return (1);
		} else {
			draidcfg_print(nvl);
			nvlist_free(nvl);
			return (0);
		}
	}

	assert(!read);

	if (data == 0 || parity == 0 || spare == 0 || children == 0) {
		fprintf(stderr,
		    gettext("Missing data/parity/spare/children argument\n"));
		return (usage());
	}

	if (parity > VDEV_RAIDZ_MAXPARITY) {
		fprintf(stderr, gettext("Invalid parity %lu\n"), parity);
		return (usage());
	}

	if (children % (data + parity) != spare) {
		fprintf(stderr, gettext("Invalid draid configration\n"));
		return (usage());
	}

	return (draidcfg_create_file(data, parity, spare, children, cfg));
}
