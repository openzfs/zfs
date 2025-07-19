// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License (the "License").
 * You may not use this file except in compliance with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
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
 * Copyright (c) 2020 by Lawrence Livermore National Security, LLC.
 */

#include <stdio.h>
#include <zlib.h>
#include <zfs_fletcher.h>
#include <sys/vdev_draid.h>
#include <sys/nvpair.h>
#include <sys/stat.h>

/*
 * The number of rows to generate for new permutation maps.
 */
#define	MAP_ROWS_DEFAULT	256

/*
 * Key values for dRAID maps when stored as nvlists.
 */
#define	MAP_SEED		"seed"
#define	MAP_CHECKSUM		"checksum"
#define	MAP_WORST_RATIO		"worst_ratio"
#define	MAP_AVG_RATIO		"avg_ratio"
#define	MAP_CHILDREN		"children"
#define	MAP_NPERMS		"nperms"
#define	MAP_PERMS		"perms"

static void
draid_usage(void)
{
	(void) fprintf(stderr,
	    "usage: draid command args ...\n"
	    "Available commands are:\n"
	    "\n"
	    "\tdraid generate [-cv] [-m min] [-n max] [-p passes] FILE\n"
	    "\tdraid verify [-rv] FILE\n"
	    "\tdraid dump [-v] [-m min] [-n max] FILE\n"
	    "\tdraid table FILE\n"
	    "\tdraid merge FILE SRC SRC...\n");
	exit(1);
}

static int
read_map(const char *filename, nvlist_t **allcfgs)
{
	int block_size = 131072;
	int buf_size = 131072;
	int tmp_size, error;
	char *tmp_buf;

	struct stat64 stat;
	if (lstat64(filename, &stat) != 0)
		return (errno);

	if (stat.st_size == 0 ||
	    !(S_ISREG(stat.st_mode) || S_ISLNK(stat.st_mode))) {
		return (EINVAL);
	}

	gzFile fp = gzopen(filename, "rb");
	if (fp == Z_NULL)
		return (errno);

	char *buf = malloc(buf_size);
	if (buf == NULL) {
		(void) gzclose(fp);
		return (ENOMEM);
	}

	ssize_t rc, bytes = 0;
	while (!gzeof(fp)) {
		rc = gzread(fp, buf + bytes, block_size);
		if ((rc < 0) || (rc == 0 && !gzeof(fp))) {
			free(buf);
			(void) gzerror(fp, &error);
			(void) gzclose(fp);
			return (error);
		} else {
			bytes += rc;

			if (bytes + block_size >= buf_size) {
				tmp_size = 2 * buf_size;
				tmp_buf = malloc(tmp_size);
				if (tmp_buf == NULL) {
					free(buf);
					(void) gzclose(fp);
					return (ENOMEM);
				}

				memcpy(tmp_buf, buf, bytes);
				free(buf);
				buf = tmp_buf;
				buf_size = tmp_size;
			}
		}
	}

	(void) gzclose(fp);

	error = nvlist_unpack(buf, bytes, allcfgs, 0);
	free(buf);

	return (error);
}

/*
 * Read a map from the specified filename.  A file contains multiple maps
 * which are indexed by the number of children. The caller is responsible
 * for freeing the configuration returned.
 */
static int
read_map_key(const char *filename, const char *key, nvlist_t **cfg)
{
	nvlist_t *allcfgs, *foundcfg = NULL;
	int error;

	error = read_map(filename, &allcfgs);
	if (error != 0)
		return (error);

	(void) nvlist_lookup_nvlist(allcfgs, key, &foundcfg);
	if (foundcfg != NULL) {
		nvlist_dup(foundcfg, cfg, KM_SLEEP);
		error = 0;
	} else {
		error = ENOENT;
	}

	nvlist_free(allcfgs);

	return (error);
}

/*
 * Write all mappings to the map file.
 */
static int
write_map(const char *filename, nvlist_t *allcfgs)
{
	size_t buflen = 0;
	int error;

	error = nvlist_size(allcfgs, &buflen, NV_ENCODE_XDR);
	if (error)
		return (error);

	char *buf = malloc(buflen);
	if (buf == NULL)
		return (ENOMEM);

	error = nvlist_pack(allcfgs, &buf, &buflen, NV_ENCODE_XDR, KM_SLEEP);
	if (error) {
		free(buf);
		return (error);
	}

	/*
	 * Atomically update the file using a temporary file and the
	 * traditional unlink then rename steps.  This code provides
	 * no locking, it only guarantees the packed nvlist on disk
	 * is updated atomically and is internally consistent.
	 */
	char *tmpname = calloc(1, MAXPATHLEN);
	if (tmpname == NULL) {
		free(buf);
		return (ENOMEM);
	}

	snprintf(tmpname, MAXPATHLEN - 1, "%s.XXXXXX", filename);

	int fd = mkstemp(tmpname);
	if (fd < 0) {
		error = errno;
		free(buf);
		free(tmpname);
		return (error);
	}
	(void) close(fd);

	gzFile fp = gzopen(tmpname, "w9b");
	if (fp == Z_NULL) {
		error = errno;
		free(buf);
		free(tmpname);
		return (error);
	}

	ssize_t rc, bytes = 0;
	while (bytes < buflen) {
		size_t size = MIN(buflen - bytes, 131072);
		rc = gzwrite(fp, buf + bytes, size);
		if (rc < 0) {
			free(buf);
			(void) gzerror(fp, &error);
			(void) gzclose(fp);
			(void) unlink(tmpname);
			free(tmpname);
			return (error);
		} else if (rc == 0) {
			break;
		} else {
			bytes += rc;
		}
	}

	free(buf);
	(void) gzclose(fp);

	if (bytes != buflen) {
		(void) unlink(tmpname);
		free(tmpname);
		return (EIO);
	}

	/*
	 * Unlink the previous config file and replace it with the updated
	 * version.  If we're able to unlink the file then directory is
	 * writable by us and the subsequent rename should never fail.
	 */
	error = unlink(filename);
	if (error != 0 && errno != ENOENT) {
		error = errno;
		(void) unlink(tmpname);
		free(tmpname);
		return (error);
	}

	error = rename(tmpname, filename);
	if (error != 0) {
		error = errno;
		(void) unlink(tmpname);
		free(tmpname);
		return (error);
	}

	free(tmpname);

	return (0);
}

/*
 * Add the dRAID map to the file and write it out.
 */
static int
write_map_key(const char *filename, char *key, draid_map_t *map,
    double worst_ratio, double avg_ratio)
{
	nvlist_t *nv_cfg, *allcfgs;
	int error;

	/*
	 * Add the configuration to an existing or new file.  The new
	 * configuration will replace an existing configuration with the
	 * same key if it has a lower ratio and is therefore better.
	 */
	error = read_map(filename, &allcfgs);
	if (error == ENOENT) {
		allcfgs = fnvlist_alloc();
	} else if (error != 0) {
		return (error);
	}

	error = nvlist_lookup_nvlist(allcfgs, key, &nv_cfg);
	if (error == 0) {
		uint64_t nv_cfg_worst_ratio = fnvlist_lookup_uint64(nv_cfg,
		    MAP_WORST_RATIO);
		double nv_worst_ratio = (double)nv_cfg_worst_ratio / 1000.0;

		if (worst_ratio < nv_worst_ratio) {
			/* Replace old map with the more balanced new map. */
			fnvlist_remove(allcfgs, key);
		} else {
			/* The old map is preferable, keep it. */
			nvlist_free(allcfgs);
			return (EEXIST);
		}
	}

	nvlist_t *cfg = fnvlist_alloc();
	fnvlist_add_uint64(cfg, MAP_SEED, map->dm_seed);
	fnvlist_add_uint64(cfg, MAP_CHECKSUM, map->dm_checksum);
	fnvlist_add_uint64(cfg, MAP_CHILDREN, map->dm_children);
	fnvlist_add_uint64(cfg, MAP_NPERMS, map->dm_nperms);
	fnvlist_add_uint8_array(cfg, MAP_PERMS,  map->dm_perms,
	    map->dm_children * map->dm_nperms * sizeof (uint8_t));

	fnvlist_add_uint64(cfg, MAP_WORST_RATIO,
	    (uint64_t)(worst_ratio * 1000.0));
	fnvlist_add_uint64(cfg, MAP_AVG_RATIO,
	    (uint64_t)(avg_ratio * 1000.0));

	error = nvlist_add_nvlist(allcfgs, key, cfg);
	if (error == 0)
		error = write_map(filename, allcfgs);

	nvlist_free(cfg);
	nvlist_free(allcfgs);
	return (error);
}

static void
dump_map(draid_map_t *map, const char *key, double worst_ratio,
    double avg_ratio, int verbose)
{
	if (verbose == 0) {
		return;
	} else if (verbose == 1) {
		printf("    \"%s\": seed: 0x%016llx worst_ratio: %2.03f "
		    "avg_ratio: %2.03f\n", key, (u_longlong_t)map->dm_seed,
		    worst_ratio, avg_ratio);
		return;
	} else {
		printf("    \"%s\":\n"
		    "        seed: 0x%016llx\n"
		    "        checksum: 0x%016llx\n"
		    "        worst_ratio: %2.03f\n"
		    "        avg_ratio: %2.03f\n"
		    "        children: %llu\n"
		    "        nperms: %llu\n",
		    key, (u_longlong_t)map->dm_seed,
		    (u_longlong_t)map->dm_checksum, worst_ratio, avg_ratio,
		    (u_longlong_t)map->dm_children,
		    (u_longlong_t)map->dm_nperms);

		if (verbose > 2) {
			printf("        perms = {\n");
			for (int i = 0; i < map->dm_nperms; i++) {
				printf("            { ");
				for (int j = 0; j < map->dm_children; j++) {
					printf("%3d%s ", map->dm_perms[
					    i * map->dm_children + j],
					    j < map->dm_children - 1 ?
					    "," : "");
				}
				printf(" },\n");
			}
			printf("        }\n");
		} else if (verbose == 2) {
			printf("        draid_perms = <omitted>\n");
		}
	}
}

static void
dump_map_nv(const char *key, nvlist_t *cfg, int verbose)
{
	draid_map_t map;
	uint_t c;

	uint64_t worst_ratio = fnvlist_lookup_uint64(cfg, MAP_WORST_RATIO);
	uint64_t avg_ratio = fnvlist_lookup_uint64(cfg, MAP_AVG_RATIO);

	map.dm_seed = fnvlist_lookup_uint64(cfg, MAP_SEED);
	map.dm_checksum = fnvlist_lookup_uint64(cfg, MAP_CHECKSUM);
	map.dm_children = fnvlist_lookup_uint64(cfg, MAP_CHILDREN);
	map.dm_nperms = fnvlist_lookup_uint64(cfg, MAP_NPERMS);
	map.dm_perms = fnvlist_lookup_uint8_array(cfg, MAP_PERMS, &c);

	dump_map(&map, key, (double)worst_ratio / 1000.0,
	    avg_ratio / 1000.0, verbose);
}

/*
 * Print a summary of the mapping.
 */
static int
dump_map_key(const char *filename, const char *key, int verbose)
{
	nvlist_t *cfg;
	int error;

	error = read_map_key(filename, key, &cfg);
	if (error != 0)
		return (error);

	dump_map_nv(key, cfg, verbose);

	return (0);
}

/*
 * Allocate a new permutation map for evaluation.
 */
static int
alloc_new_map(uint64_t children, uint64_t nperms, uint64_t seed,
    draid_map_t **mapp)
{
	draid_map_t *map;
	int error;

	map = malloc(sizeof (draid_map_t));
	if (map == NULL)
		return (ENOMEM);

	map->dm_children = children;
	map->dm_nperms = nperms;
	map->dm_seed = seed;
	map->dm_checksum = 0;

	error = vdev_draid_generate_perms(map, &map->dm_perms);
	if (error) {
		free(map);
		return (error);
	}

	*mapp = map;

	return (0);
}

/*
 * Allocate the fixed permutation map for N children.
 */
static int
alloc_fixed_map(uint64_t children, draid_map_t **mapp)
{
	const draid_map_t *fixed_map;
	draid_map_t *map;
	int error;

	error = vdev_draid_lookup_map(children, &fixed_map);
	if (error)
		return (error);

	map = malloc(sizeof (draid_map_t));
	if (map == NULL)
		return (ENOMEM);

	memcpy(map, fixed_map, sizeof (draid_map_t));
	VERIFY3U(map->dm_checksum, !=, 0);

	error = vdev_draid_generate_perms(map, &map->dm_perms);
	if (error) {
		free(map);
		return (error);
	}

	*mapp = map;

	return (0);
}

/*
 * Free a permutation map.
 */
static void
free_map(draid_map_t *map)
{
	free(map->dm_perms);
	free(map);
}

/*
 * Check if dev is in the provided list of faulted devices.
 */
static inline boolean_t
is_faulted(int *faulted_devs, int nfaulted, int dev)
{
	for (int i = 0; i < nfaulted; i++)
		if (faulted_devs[i] == dev)
			return (B_TRUE);

	return (B_FALSE);
}

/*
 * Evaluate how resilvering I/O will be distributed given a list of faulted
 * vdevs.  As a simplification we assume one IO is sufficient to repair each
 * damaged device in a group.
 */
static double
eval_resilver(draid_map_t *map, uint64_t groupwidth, uint64_t nspares,
    int *faulted_devs, int nfaulted, int *min_child_ios, int *max_child_ios)
{
	uint64_t children = map->dm_children;
	uint64_t ngroups = 1;
	uint64_t ndisks = children - nspares;

	/*
	 * Calculate the minimum number of groups required to fill a slice.
	 */
	while (ngroups * (groupwidth) % (children - nspares) != 0)
		ngroups++;

	int *ios = calloc(map->dm_children, sizeof (uint64_t));

	ASSERT3P(ios, !=, NULL);

	/* Resilver all rows */
	for (int i = 0; i < map->dm_nperms; i++) {
		uint8_t *row = &map->dm_perms[i * map->dm_children];

		/* Resilver all groups with faulted drives */
		for (int j = 0; j < ngroups; j++) {
			uint64_t spareidx = map->dm_children - nspares;
			boolean_t repair_needed = B_FALSE;

			/* See if any devices in this group are faulted */
			uint64_t groupstart = (j * groupwidth) % ndisks;

			for (int k = 0; k < groupwidth; k++) {
				uint64_t groupidx = (groupstart + k) % ndisks;

				repair_needed = is_faulted(faulted_devs,
				    nfaulted, row[groupidx]);
				if (repair_needed)
					break;
			}

			if (repair_needed == B_FALSE)
				continue;

			/*
			 * This group is degraded. Calculate the number of
			 * reads the non-faulted drives require and the number
			 * of writes to the distributed hot spare for this row.
			 */
			for (int k = 0; k < groupwidth; k++) {
				uint64_t groupidx = (groupstart + k) % ndisks;

				if (!is_faulted(faulted_devs, nfaulted,
				    row[groupidx])) {
					ios[row[groupidx]]++;
				} else if (nspares > 0) {
					while (is_faulted(faulted_devs,
					    nfaulted, row[spareidx])) {
						spareidx++;
					}

					ASSERT3U(spareidx, <, map->dm_children);
					ios[row[spareidx]]++;
					spareidx++;
				}
			}
		}
	}

	*min_child_ios = INT_MAX;
	*max_child_ios = 0;

	/*
	 * Find the drives with fewest and most required I/O.  These values
	 * are used to calculate the imbalance ratio.  To avoid returning an
	 * infinite value for permutations which have children that perform
	 * no IO a floor of 1 IO per child is set.  This ensures a meaningful
	 * ratio is returned for comparison and it is not an uncommon when
	 * there are a large number of children.
	 */
	for (int i = 0; i < map->dm_children; i++) {

		if (is_faulted(faulted_devs, nfaulted, i)) {
			ASSERT0(ios[i]);
			continue;
		}

		if (ios[i] == 0)
			ios[i] = 1;

		if (ios[i] < *min_child_ios)
			*min_child_ios = ios[i];

		if (ios[i] > *max_child_ios)
			*max_child_ios = ios[i];
	}

	ASSERT3S(*min_child_ios, !=, INT_MAX);
	ASSERT3S(*max_child_ios, !=, 0);

	double ratio = (double)(*max_child_ios) / (double)(*min_child_ios);

	free(ios);

	return (ratio);
}

/*
 * Evaluate the quality of the permutation mapping by considering possible
 * device failures.  Returns the imbalance ratio for the worst mapping which
 * is defined to be the largest number of child IOs over the fewest number
 * child IOs. A value of 1.0 indicates the mapping is perfectly balance and
 * all children perform an equal amount of work during reconstruction.
 */
static void
eval_decluster(draid_map_t *map, double *worst_ratiop, double *avg_ratiop)
{
	uint64_t children = map->dm_children;
	double worst_ratio = 1.0;
	double sum = 0;
	int worst_min_ios = 0, worst_max_ios = 0;
	int n = 0;

	/*
	 * When there are only 2 children there can be no distributed
	 * spare and no resilver to evaluate.  Default to a ratio of 1.0
	 * for this degenerate case.
	 */
	if (children == VDEV_DRAID_MIN_CHILDREN) {
		*worst_ratiop = 1.0;
		*avg_ratiop = 1.0;
		return;
	}

	/*
	 * Score the mapping as if it had either 1 or 2 distributed spares.
	 */
	for (int nspares = 1; nspares <= 2; nspares++) {
		uint64_t faults = nspares;

		/*
		 * Score groupwidths up to 19.  This value was chosen as the
		 * largest reasonable width (16d+3p).  dRAID pools may be still
		 * be created with wider stripes but they are not considered in
		 * this analysis in order to optimize for the most common cases.
		 */
		for (uint64_t groupwidth = 2;
		    groupwidth <= MIN(children - nspares, 19);
		    groupwidth++) {
			int faulted_devs[2];
			int min_ios, max_ios;

			/*
			 * Score possible devices faults.  This is limited
			 * to exactly one fault per distributed spare for
			 * the purposes of this similation.
			 */
			for (int f1 = 0; f1 < children; f1++) {
				faulted_devs[0] = f1;
				double ratio;

				if (faults == 1) {
					ratio = eval_resilver(map, groupwidth,
					    nspares, faulted_devs, faults,
					    &min_ios, &max_ios);

					if (ratio > worst_ratio) {
						worst_ratio = ratio;
						worst_min_ios = min_ios;
						worst_max_ios = max_ios;
					}

					sum += ratio;
					n++;
				} else if (faults == 2) {
					for (int f2 = f1 + 1; f2 < children;
					    f2++) {
						faulted_devs[1] = f2;

						ratio = eval_resilver(map,
						    groupwidth, nspares,
						    faulted_devs, faults,
						    &min_ios, &max_ios);

						if (ratio > worst_ratio) {
							worst_ratio = ratio;
							worst_min_ios = min_ios;
							worst_max_ios = max_ios;
						}

						sum += ratio;
						n++;
					}
				}
			}
		}
	}

	*worst_ratiop = worst_ratio;
	*avg_ratiop = sum / n;

	/*
	 * Log the min/max io values for particularly unbalanced maps.
	 * Since the maps are generated entirely randomly these are possible
	 * be exceedingly unlikely.  We log it for possible investigation.
	 */
	if (worst_ratio > 100.0) {
		dump_map(map, "DEBUG", worst_ratio, *avg_ratiop, 2);
		printf("worst_min_ios=%d worst_max_ios=%d\n",
		    worst_min_ios, worst_max_ios);
	}
}

static int
eval_maps(uint64_t children, int passes, uint64_t *map_seed,
    draid_map_t **best_mapp, double *best_ratiop, double *avg_ratiop)
{
	draid_map_t *best_map = NULL;
	double best_worst_ratio = 1000.0;
	double best_avg_ratio = 1000.0;

	/*
	 * Perform the requested number of passes evaluating randomly
	 * generated permutation maps.  Only the best version is kept.
	 */
	for (int i = 0; i < passes; i++) {
		double worst_ratio, avg_ratio;
		draid_map_t *map;
		int error;

		/*
		 * Calculate the next seed and generate a new candidate map.
		 */
		error = alloc_new_map(children, MAP_ROWS_DEFAULT,
		    vdev_draid_rand(map_seed), &map);
		if (error) {
			if (best_map != NULL)
				free_map(best_map);
			return (error);
		}

		/*
		 * Consider maps with a lower worst_ratio to be of higher
		 * quality.  Some maps may have a lower avg_ratio but they
		 * are discarded since they might include some particularly
		 * imbalanced permutations.  The average is tracked to in
		 * order to get a sense of the average permutation quality.
		 */
		eval_decluster(map, &worst_ratio, &avg_ratio);

		if (best_map == NULL || worst_ratio < best_worst_ratio) {

			if (best_map != NULL)
				free_map(best_map);

			best_map = map;
			best_worst_ratio = worst_ratio;
			best_avg_ratio = avg_ratio;
		} else {
			free_map(map);
		}
	}

	/*
	 * After determining the best map generate a checksum over the full
	 * permutation array.  This checksum is verified when opening a dRAID
	 * pool to ensure the generated in memory permutations are correct.
	 */
	zio_cksum_t cksum;
	fletcher_4_native_varsize(best_map->dm_perms,
	    sizeof (uint8_t) * best_map->dm_children * best_map->dm_nperms,
	    &cksum);
	best_map->dm_checksum = cksum.zc_word[0];

	*best_mapp = best_map;
	*best_ratiop = best_worst_ratio;
	*avg_ratiop = best_avg_ratio;

	return (0);
}

static int
draid_generate(int argc, char *argv[])
{
	char filename[MAXPATHLEN] = {0};
	uint64_t map_seed[2];
	int c, fd, error, verbose = 0, passes = 1, continuous = 0;
	int min_children = VDEV_DRAID_MIN_CHILDREN;
	int max_children = VDEV_DRAID_MAX_CHILDREN;
	int restarts = 0;

	while ((c = getopt(argc, argv, ":cm:n:p:v")) != -1) {
		switch (c) {
		case 'c':
			continuous++;
			break;
		case 'm':
			min_children = (int)strtol(optarg, NULL, 0);
			if (min_children < VDEV_DRAID_MIN_CHILDREN) {
				(void) fprintf(stderr, "A minimum of 2 "
				    "children are required.\n");
				return (1);
			}

			break;
		case 'n':
			max_children = (int)strtol(optarg, NULL, 0);
			if (max_children > VDEV_DRAID_MAX_CHILDREN) {
				(void) fprintf(stderr, "A maximum of %d "
				    "children are allowed.\n",
				    VDEV_DRAID_MAX_CHILDREN);
				return (1);
			}
			break;
		case 'p':
			passes = (int)strtol(optarg, NULL, 0);
			break;
		case 'v':
			/*
			 * 0 - Only log when a better map is added to the file.
			 * 1 - Log the current best map for each child count.
			 *     Minimal output on a single summary line.
			 * 2 - Log the current best map for each child count.
			 *     More verbose includes most map fields.
			 * 3 - Log the current best map for each child count.
			 *     Very verbose all fields including the full map.
			 */
			verbose++;
			break;
		case ':':
			(void) fprintf(stderr,
			    "missing argument for '%c' option\n", optopt);
			draid_usage();
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			draid_usage();
			break;
		}
	}

	if (argc > optind)
		strlcpy(filename, argv[optind], sizeof (filename));
	else {
		(void) fprintf(stderr, "A FILE must be specified.\n");
		return (1);
	}

restart:
	/*
	 * Start with a fresh seed from /dev/urandom.
	 */
	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0) {
		printf("Unable to open /dev/urandom: %s\n:", strerror(errno));
		return (1);
	} else {
		ssize_t bytes = sizeof (map_seed);
		ssize_t bytes_read = 0;

		while (bytes_read < bytes) {
			ssize_t rc = read(fd, ((char *)map_seed) + bytes_read,
			    bytes - bytes_read);
			if (rc < 0) {
				printf("Unable to read /dev/urandom: %s\n:",
				    strerror(errno));
				close(fd);
				return (1);
			}
			bytes_read += rc;
		}

		(void) close(fd);
	}

	if (restarts == 0)
		printf("Writing generated mappings to '%s':\n", filename);

	/*
	 * Generate maps for all requested child counts. The best map for
	 * each child count is written out to the specified file.  If the file
	 * already contains a better mapping this map will not be added.
	 */
	for (uint64_t children = min_children;
	    children <= max_children; children++) {
		char key[8] = { 0 };
		draid_map_t *map;
		double worst_ratio = 1000.0;
		double avg_ratio = 1000.0;

		error = eval_maps(children, passes, map_seed, &map,
		    &worst_ratio, &avg_ratio);
		if (error) {
			printf("Error eval_maps(): %s\n", strerror(error));
			return (1);
		}

		if (worst_ratio < 1.0 || avg_ratio < 1.0) {
			printf("Error ratio < 1.0: worst_ratio = %2.03f "
			    "avg_ratio = %2.03f\n", worst_ratio, avg_ratio);
			return (1);
		}

		snprintf(key, 7, "%llu", (u_longlong_t)children);
		error = write_map_key(filename, key, map, worst_ratio,
		    avg_ratio);
		if (error == 0) {
			/* The new map was added to the file. */
			dump_map(map, key, worst_ratio, avg_ratio,
			    MAX(verbose, 1));
		} else if (error == EEXIST) {
			/* The existing map was preferable and kept. */
			if (verbose > 0)
				dump_map_key(filename, key, verbose);
		} else {
			printf("Error write_map_key(): %s\n", strerror(error));
			return (1);
		}

		free_map(map);
	}

	/*
	 * When the continuous option is set restart at the minimum number of
	 * children instead of exiting. This option is useful as a mechanism
	 * to continuous try and refine the discovered permutations.
	 */
	if (continuous) {
		restarts++;
		printf("Restarting by request (-c): %d\n", restarts);
		goto restart;
	}

	return (0);
}

/*
 * Verify each map in the file by generating its in-memory permutation array
 * and comfirming its checksum is correct.
 */
static int
draid_verify(int argc, char *argv[])
{
	char filename[MAXPATHLEN] = {0};
	int n = 0, c, error, verbose = 1;
	int check_ratios = 0;

	while ((c = getopt(argc, argv, ":rv")) != -1) {
		switch (c) {
		case 'r':
			check_ratios++;
			break;
		case 'v':
			verbose++;
			break;
		case ':':
			(void) fprintf(stderr,
			    "missing argument for '%c' option\n", optopt);
			draid_usage();
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			draid_usage();
			break;
		}
	}

	if (argc > optind) {
		char *abspath = malloc(MAXPATHLEN);
		if (abspath == NULL)
			return (ENOMEM);

		if (realpath(argv[optind], abspath) != NULL)
			strlcpy(filename, abspath, sizeof (filename));
		else
			strlcpy(filename, argv[optind], sizeof (filename));

		free(abspath);
	} else {
		(void) fprintf(stderr, "A FILE must be specified.\n");
		return (1);
	}

	printf("Verifying permutation maps: '%s'\n", filename);

	/*
	 * Lookup hardcoded permutation map for each valid number of children
	 * and verify a generated map has the correct checksum.  Then compare
	 * the generated map values with the nvlist map values read from the
	 * reference file to cross-check the permutation.
	 */
	for (uint64_t children = VDEV_DRAID_MIN_CHILDREN;
	    children <= VDEV_DRAID_MAX_CHILDREN;
	    children++) {
		draid_map_t *map;
		char key[8] = {0};

		snprintf(key, 8, "%llu", (u_longlong_t)children);

		error = alloc_fixed_map(children, &map);
		if (error) {
			printf("Error alloc_fixed_map() failed: %s\n",
			    error == ECKSUM ? "Invalid checksum" :
			    strerror(error));
			return (1);
		}

		uint64_t nv_seed, nv_checksum, nv_children, nv_nperms;
		uint8_t *nv_perms;
		nvlist_t *cfg;
		uint_t c;

		error = read_map_key(filename, key, &cfg);
		if (error != 0) {
			printf("Error read_map_key() failed: %s\n",
			    strerror(error));
			free_map(map);
			return (1);
		}

		nv_seed = fnvlist_lookup_uint64(cfg, MAP_SEED);
		nv_checksum = fnvlist_lookup_uint64(cfg, MAP_CHECKSUM);
		nv_children = fnvlist_lookup_uint64(cfg, MAP_CHILDREN);
		nv_nperms = fnvlist_lookup_uint64(cfg, MAP_NPERMS);
		nvlist_lookup_uint8_array(cfg, MAP_PERMS, &nv_perms, &c);

		/*
		 * Compare draid_map_t and nvlist reference values.
		 */
		if (map->dm_seed != nv_seed) {
			printf("Error different seeds: 0x%016llx != "
			    "0x%016llx\n", (u_longlong_t)map->dm_seed,
			    (u_longlong_t)nv_seed);
			error = EINVAL;
		}

		if (map->dm_checksum != nv_checksum) {
			printf("Error different checksums: 0x%016llx "
			    "!= 0x%016llx\n",
			    (u_longlong_t)map->dm_checksum,
			    (u_longlong_t)nv_checksum);
			error = EINVAL;
		}

		if (map->dm_children != nv_children) {
			printf("Error different children: %llu "
			    "!= %llu\n", (u_longlong_t)map->dm_children,
			    (u_longlong_t)nv_children);
			error = EINVAL;
		}

		if (map->dm_nperms != nv_nperms) {
			printf("Error different nperms: %llu "
			    "!= %llu\n", (u_longlong_t)map->dm_nperms,
			    (u_longlong_t)nv_nperms);
			error = EINVAL;
		}

		for (uint64_t i = 0; i < nv_children * nv_nperms; i++) {
			if (map->dm_perms[i] != nv_perms[i]) {
				printf("Error different perms[%llu]: "
				    "%d != %d\n", (u_longlong_t)i,
				    (int)map->dm_perms[i],
				    (int)nv_perms[i]);
				error = EINVAL;
				break;
			}
		}

		/*
		 * For good measure recalculate the worst and average
		 * ratios and confirm they match the nvlist values.
		 */
		if (check_ratios) {
			uint64_t nv_worst_ratio, nv_avg_ratio;
			double worst_ratio, avg_ratio;

			eval_decluster(map, &worst_ratio, &avg_ratio);

			nv_worst_ratio = fnvlist_lookup_uint64(cfg,
			    MAP_WORST_RATIO);
			nv_avg_ratio = fnvlist_lookup_uint64(cfg,
			    MAP_AVG_RATIO);

			if (worst_ratio < 1.0 || avg_ratio < 1.0) {
				printf("Error ratio out of range %2.03f, "
				    "%2.03f\n", worst_ratio, avg_ratio);
				error = EINVAL;
			}

			if ((uint64_t)(worst_ratio * 1000.0) !=
			    nv_worst_ratio) {
				printf("Error different worst_ratio %2.03f "
				    "!= %2.03f\n", (double)nv_worst_ratio /
				    1000.0, worst_ratio);
				error = EINVAL;
			}

			if ((uint64_t)(avg_ratio * 1000.0) != nv_avg_ratio) {
				printf("Error different average_ratio %2.03f "
				    "!= %2.03f\n", (double)nv_avg_ratio /
				    1000.0, avg_ratio);
				error = EINVAL;
			}
		}

		if (error) {
			free_map(map);
			nvlist_free(cfg);
			return (1);
		}

		if (verbose > 0) {
			printf("- %llu children: good\n",
			    (u_longlong_t)children);
		}
		n++;

		free_map(map);
		nvlist_free(cfg);
	}

	if (n != (VDEV_DRAID_MAX_CHILDREN - 1)) {
		printf("Error permutation maps missing: %d / %d checked\n",
		    n, VDEV_DRAID_MAX_CHILDREN - 1);
		return (1);
	}

	printf("Successfully verified %d / %d permutation maps\n",
	    n, VDEV_DRAID_MAX_CHILDREN - 1);

	return (0);
}

/*
 * Dump the contents of the specified mapping(s) for inspection.
 */
static int
draid_dump(int argc, char *argv[])
{
	char filename[MAXPATHLEN] = {0};
	int c, error, verbose = 1;
	int min_children = VDEV_DRAID_MIN_CHILDREN;
	int max_children = VDEV_DRAID_MAX_CHILDREN;

	while ((c = getopt(argc, argv, ":vm:n:")) != -1) {
		switch (c) {
		case 'm':
			min_children = (int)strtol(optarg, NULL, 0);
			if (min_children < 2) {
				(void) fprintf(stderr, "A minimum of 2 "
				    "children are required.\n");
				return (1);
			}

			break;
		case 'n':
			max_children = (int)strtol(optarg, NULL, 0);
			if (max_children > VDEV_DRAID_MAX_CHILDREN) {
				(void) fprintf(stderr, "A maximum of %d "
				    "children are allowed.\n",
				    VDEV_DRAID_MAX_CHILDREN);
				return (1);
			}
			break;
		case 'v':
			verbose++;
			break;
		case ':':
			(void) fprintf(stderr,
			    "missing argument for '%c' option\n", optopt);
			draid_usage();
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			draid_usage();
			break;
		}
	}

	if (argc > optind)
		strlcpy(filename, argv[optind], sizeof (filename));
	else {
		(void) fprintf(stderr, "A FILE must be specified.\n");
		return (1);
	}

	/*
	 * Dump maps for the requested child counts.
	 */
	for (uint64_t children = min_children;
	    children <= max_children; children++) {
		char key[8] = { 0 };

		snprintf(key, 7, "%llu", (u_longlong_t)children);
		error = dump_map_key(filename, key, verbose);
		if (error) {
			printf("Error dump_map_key(): %s\n", strerror(error));
			return (1);
		}
	}

	return (0);
}

/*
 * Print all of the mappings as a C formatted draid_map_t array.  This table
 * is found in the module/zcommon/zfs_draid.c file and is the definitive
 * source for all mapping used by dRAID.  It cannot be updated without
 * changing the dRAID on disk format.
 */
static int
draid_table(int argc, char *argv[])
{
	char filename[MAXPATHLEN] = {0};
	int error;

	if (argc > optind)
		strlcpy(filename, argv[optind], sizeof (filename));
	else {
		(void) fprintf(stderr, "A FILE must be specified.\n");
		return (1);
	}

	printf("static const draid_map_t "
	    "draid_maps[VDEV_DRAID_MAX_MAPS] = {\n");

	for (uint64_t children = VDEV_DRAID_MIN_CHILDREN;
	    children <= VDEV_DRAID_MAX_CHILDREN;
	    children++) {
		uint64_t seed, checksum, nperms, avg_ratio;
		nvlist_t *cfg;
		char key[8] = {0};

		snprintf(key, 8, "%llu", (u_longlong_t)children);

		error = read_map_key(filename, key, &cfg);
		if (error != 0) {
			printf("Error read_map_key() failed: %s\n",
			    strerror(error));
			return (1);
		}

		seed = fnvlist_lookup_uint64(cfg, MAP_SEED);
		checksum = fnvlist_lookup_uint64(cfg, MAP_CHECKSUM);
		children = fnvlist_lookup_uint64(cfg, MAP_CHILDREN);
		nperms = fnvlist_lookup_uint64(cfg, MAP_NPERMS);
		avg_ratio = fnvlist_lookup_uint64(cfg, MAP_AVG_RATIO);

		printf("\t{ %3llu, %3llu, 0x%016llx, 0x%016llx },\t"
		    "/* %2.03f */\n", (u_longlong_t)children,
		    (u_longlong_t)nperms, (u_longlong_t)seed,
		    (u_longlong_t)checksum, (double)avg_ratio / 1000.0);

		nvlist_free(cfg);
	}

	printf("};\n");

	return (0);
}

static int
draid_merge_impl(nvlist_t *allcfgs, const char *srcfilename, int *mergedp)
{
	nvlist_t *srccfgs;
	nvpair_t *elem = NULL;
	int error, merged = 0;

	error = read_map(srcfilename, &srccfgs);
	if (error != 0)
		return (error);

	while ((elem = nvlist_next_nvpair(srccfgs, elem)) != NULL) {
		uint64_t nv_worst_ratio;
		uint64_t allcfg_worst_ratio;
		nvlist_t *cfg, *allcfg;
		const char *key;

		switch (nvpair_type(elem)) {
		case DATA_TYPE_NVLIST:

			(void) nvpair_value_nvlist(elem, &cfg);
			key = nvpair_name(elem);

			nv_worst_ratio = fnvlist_lookup_uint64(cfg,
			    MAP_WORST_RATIO);

			error = nvlist_lookup_nvlist(allcfgs, key, &allcfg);
			if (error == 0) {
				allcfg_worst_ratio = fnvlist_lookup_uint64(
				    allcfg, MAP_WORST_RATIO);

				if (nv_worst_ratio < allcfg_worst_ratio) {
					fnvlist_remove(allcfgs, key);
					fnvlist_add_nvlist(allcfgs, key, cfg);
					merged++;
				}
			} else if (error == ENOENT) {
				fnvlist_add_nvlist(allcfgs, key, cfg);
				merged++;
			} else {
				return (error);
			}

			break;
		default:
			continue;
		}
	}

	nvlist_free(srccfgs);

	*mergedp = merged;

	return (0);
}

/*
 * Merge the best map for each child count found in the listed files into
 * a new file.  This allows 'draid generate' to be run in parallel and for
 * the results maps to be combined.
 */
static int
draid_merge(int argc, char *argv[])
{
	char filename[MAXPATHLEN] = {0};
	int c, error, total_merged = 0;
	nvlist_t *allcfgs;

	while ((c = getopt(argc, argv, ":")) != -1) {
		switch (c) {
		case ':':
			(void) fprintf(stderr,
			    "missing argument for '%c' option\n", optopt);
			draid_usage();
			break;
		case '?':
			(void) fprintf(stderr, "invalid option '%c'\n",
			    optopt);
			draid_usage();
			break;
		}
	}

	if (argc < 4) {
		(void) fprintf(stderr,
		    "A FILE and multiple SRCs must be specified.\n");
		return (1);
	}

	strlcpy(filename, argv[optind], sizeof (filename));
	optind++;

	error = read_map(filename, &allcfgs);
	if (error == ENOENT) {
		allcfgs = fnvlist_alloc();
	} else if (error != 0) {
		printf("Error read_map(): %s\n", strerror(error));
		return (error);
	}

	while (optind < argc) {
		char srcfilename[MAXPATHLEN] = {0};
		int merged = 0;

		strlcpy(srcfilename, argv[optind], sizeof (srcfilename));

		error = draid_merge_impl(allcfgs, srcfilename, &merged);
		if (error) {
			printf("Error draid_merge_impl(): %s\n",
			    strerror(error));
			nvlist_free(allcfgs);
			return (1);
		}

		total_merged += merged;
		printf("Merged %d key(s) from '%s' into '%s'\n", merged,
		    srcfilename, filename);

		optind++;
	}

	if (total_merged > 0)
		write_map(filename, allcfgs);

	printf("Merged a total of %d key(s) into '%s'\n", total_merged,
	    filename);

	nvlist_free(allcfgs);

	return (0);
}

int
main(int argc, char *argv[])
{
	if (argc < 2)
		draid_usage();

	char *subcommand = argv[1];

	if (strcmp(subcommand, "generate") == 0) {
		return (draid_generate(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "verify") == 0) {
		return (draid_verify(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "dump") == 0) {
		return (draid_dump(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "table") == 0) {
		return (draid_table(argc - 1, argv + 1));
	} else if (strcmp(subcommand, "merge") == 0) {
		return (draid_merge(argc - 1, argv + 1));
	} else {
		draid_usage();
	}
}
