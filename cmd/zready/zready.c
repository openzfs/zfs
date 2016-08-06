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

#include <stdio.h>
#include <unistd.h>
#include <stdio_ext.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <sys/zfs_context.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/stat.h>

#define ZPOOL_PATH "/dev/zpool"

static nvlist_t *
get_config(const char *dev)
{
	int fd;
	vdev_label_t label;
	char *path, *buf = label.vl_vdev_phys.vp_nvlist;
	size_t buflen = sizeof (label.vl_vdev_phys.vp_nvlist);
	struct stat64 statbuf;
	uint64_t psize;
	int l;
	int found = 0;
	nvlist_t *best = NULL;
	uint64_t best_txg = 0;
	uint64_t state;
	uint64_t pguid = 0;

	path = strdup(dev);

	if ((fd = open64(path, O_RDONLY)) < 0) {
		(void) printf("cannot open '%s': %s\n", path, strerror(errno));
		free(path);
		exit(1);
	}

	if (fstat64_blk(fd, &statbuf) != 0) {
		(void) printf("failed to stat '%s': %s\n", path,
		    strerror(errno));
		free(path);
		(void) close(fd);
		exit(1);
	}

	psize = statbuf.st_size;
	psize = P2ALIGN(psize, (uint64_t)sizeof (vdev_label_t));

	for (l = 0; l < VDEV_LABELS; l++) {
		uint64_t g, txg;
		nvlist_t *config = NULL;

		if (pread64(fd, &label, sizeof (label),
		    vdev_label_offset(psize, l, 0)) != sizeof (label)) {
			(void) fprintf(stderr, "failed to read label %d\n", l);
			continue;
		}

		if (nvlist_unpack(buf, buflen, &config, 0) != 0) {
			(void) fprintf(stderr, "failed to unpack label %d\n", l);
		} else {
			if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_TXG, &txg) != 0)
				continue;
			if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &g) != 0)
				continue;
			if (nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_STATE, &state) != 0)
				continue;

			if (state == POOL_STATE_DESTROYED)
				continue;
			if (pguid == 0)
				pguid = g;
			else if (pguid != g) {
				fprintf(stderr, "pool guid mismatch between labels\n");
				exit(1);
			}
			if (txg > best_txg) {
				if (best)
					nvlist_free(best);
				best = config;
				best_txg = txg;
			} else {
				nvlist_free(config);
			}
			found++;
		}
	}

	if (found < 3)
		exit(1);

	free(path);
	(void) close(fd);
	return best;
}

static int
build_vdev(nvlist_t *vdev, uint64_t vguid, uint64_t dguid)
{
	char dir[64];
	char *type;
	uint64_t cguid;
	nvlist_t **child;
	uint_t nc;
	int i, ready = 0;
	int ret = 0;

	sprintf(dir, "%llu", (u_longlong_t)vguid);
	if (mkdir(dir, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "failed to mkdir %s: %s\n", dir, strerror(errno));
		exit(1);
	}
	if (chdir(dir) < 0) {
		fprintf(stderr, "failed to enter %s: %s\n", dir, strerror(errno));
		exit(1);
	}

	if (nvlist_lookup_string(vdev, ZPOOL_CONFIG_TYPE, &type) != 0) {
		fprintf(stderr, "fail to get vdev type\n");
		exit(1);
	}

	if (dguid == vguid) {
		ready = 1;
		ret = 1;
	} else if (strcmp(type, "disk") != 0 && strcmp(type, "file") != 0) {
		int found = 0;

		if (nvlist_lookup_nvlist_array(vdev, ZPOOL_CONFIG_CHILDREN, &child, &nc) != 0) {
			fprintf(stderr, "fail to get child vdev\n");
			exit(1);
		}
		for (i = 0; i < nc; i++) {
			if (nvlist_lookup_uint64(child[i], ZPOOL_CONFIG_GUID, &cguid) != 0) {
				fprintf(stderr, "failed to get child guid\n");
				exit(1);
			}
			if ((ret = build_vdev(child[i], cguid, dguid)))
				break;
		}
		if (ret) {
			for (i = 0; i < nc; i++) {
				struct stat sbuf;
				char path[64];
				if (nvlist_lookup_uint64(child[i], ZPOOL_CONFIG_GUID, &cguid) != 0) {
					fprintf(stderr, "failed to get child guid\n");
					exit(1);
				}
				sprintf(path, "%llu/ready", (u_longlong_t)cguid);
				if (stat(path, &sbuf) == 0)
					found++;
			}
			if (found == nc)
				ready = 1;
		}
	}

	if (ready && close(open("ready", O_WRONLY|O_CREAT, 0644)) < 0) {
		fprintf(stderr, "failed to create \"ready\"\n");
		exit(1);
	}
	chdir("..");
	return ret;
}

static void
build_tree(nvlist_t *config)
{
	DIR *dfd;
	struct dirent *dirent;
	nvlist_t *vdev;
	uint64_t pguid, vguid, dguid, nc;
	char *name;
	int found = 0;
	int fd;
	if (nvlist_lookup_nvlist(config, ZPOOL_CONFIG_VDEV_TREE, &vdev) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_VDEV_CHILDREN, &nc) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_POOL_GUID, &pguid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_TOP_GUID, &vguid) != 0 ||
	    nvlist_lookup_uint64(config, ZPOOL_CONFIG_GUID, &dguid) != 0 ||
	    nvlist_lookup_string(config, ZPOOL_CONFIG_POOL_NAME, &name) != 0) {
		fprintf(stderr, "fail to parse pool config\n");
		exit(1);
	}

	/* make pool root dir and cd into it */
	if (mkdir(name, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "failed to mkdir %s: %s\n", name, strerror(errno));
		exit(1);
	}
	if (chdir(name) < 0) {
		fprintf(stderr, "failed to enter %s: %s\n", name, strerror(errno));
		exit(1);
	}

	/* write pool_guid, TODO: we need to check pool_guid is correct */
	if ((fd = open("pool_guid", O_WRONLY|O_CREAT|O_EXCL, 0644)) > 0) {
		char u[64];
		sprintf(u, "%llu", (u_longlong_t)pguid);
		write(fd, u, strlen(u));
		close(fd);
	}

	/* decend into top level vdev */
	build_vdev(vdev, vguid, dguid);

	dfd = opendir(".");
	while ((dirent = readdir(dfd)) != NULL) {
		struct stat sbuf;
		char path[64];
		if (strcmp(dirent->d_name, ".") == 0 || strcmp(dirent->d_name, "..") == 0)
			continue;
		if (snprintf(path, 64, "%s/ready", dirent->d_name) >= 64)
			continue;
		if (stat(path, &sbuf) == 0)
			found++;
	}
	closedir(dfd);

	if ((found == nc) && close(open("ready", O_WRONLY|O_CREAT, 0644)) < 0) {
		fprintf(stderr, "failed to create \"ready\"\n");
		exit(1);
	}
}

int
main(int argc, char **argv)
{
	nvlist_t *config;
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <dev>\n", argv[0]);
		exit(1);
	}
	config = get_config(argv[1]);
	if (!config)
		return (0);

	if (mkdir(ZPOOL_PATH, 0755) < 0 && errno != EEXIST) {
		fprintf(stderr, "failed to mkdir %s: %s\n", ZPOOL_PATH, strerror(errno));
		exit(1);
	}
	if (chdir(ZPOOL_PATH) < 0) {
		fprintf(stderr, "failed to enter %s: %s\n", ZPOOL_PATH, strerror(errno));
		exit(1);
	}
	build_tree(config);
	nvlist_free(config);
	return (0);
}
