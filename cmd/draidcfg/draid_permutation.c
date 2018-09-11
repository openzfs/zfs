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

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <sys/stat.h>

#include "draid_permutation.h"


#define	MAX_GROUPSIZE	32
#define	MAX_GROUPS	128
#define	MAX_SPARES 	100
#define	MAX_DEVS	(MAX_GROUPSIZE * MAX_GROUPS + MAX_SPARES)
#define	MAX_ROWS	16384

#define	UNOPT		0
#define	EVAL_WORST	1
#define	EVAL_MEAN	2
#define	EVAL_RMS	3

static int verbose = 0;

typedef struct
{
	int  ngroups;
	int *groupsz;
	int  nspares;
	int  ndevs;
	int  nrows;
	/* each row maps all drives, groups from 0, spares down from ndevs-1 */
	int **rows;
	int   nbroken; /* # broken drives */
	int  *broken; /* which drives are broken */
} map_t;

typedef struct
{
	int  value;
	int  order;
} pair_t;

static void
permute_devs(int *in, int *out, int ndevs)
{
	pair_t tmp[MAX_DEVS];
	int    i;
	int    j;

	if (ndevs == 2) { /* swap */
		i = in[0];
		j = in[1];
		out[0] = j;
		out[1] = i;
		return;
	}

	for (i = 0; i < ndevs; i++) { /* assign random order */
		tmp[i].value = in[i];
		tmp[i].order = mrand48();
	}

	for (i = 1; i < ndevs; i++) /* sort */
		for (j = 0; j < i; j++)
			if (tmp[i].order < tmp[j].order) {
				pair_t t = tmp[i];
				tmp[i] = tmp[j];
				tmp[j] = t;
			}

	for (i = 0; i < ndevs; i++)
		out[i] = tmp[i].value;
}

static void
print_map(map_t *map)
{
	int i;
	int j;

	for (i = 0; i < map->nrows; i++) {
		for (j = 0; j < map->ndevs; j++) {
			if (j == map->ndevs - map->nspares)
				printf("S ");

			printf("%2d ", map->rows[i][j]);
		}
		printf("\n");
	}
}

static void
check_map(map_t *map)
{
	int   i;
	int   j;
	int   nrows = map->nrows;
	int   ndevs = map->ndevs;
	int **rows = map->rows;
	int   devcounts[MAX_DEVS];
	int   brokencounts[MAX_DEVS];

	ASSERT(map->ngroups <= MAX_GROUPS);
	ASSERT(map->nspares <= MAX_SPARES);
	ASSERT(map->nrows <= MAX_ROWS);
	ASSERT(map->nbroken <= MAX_SPARES);

	/* Ensure each dev appears once in every row */
	memset(devcounts, 0, sizeof (int) * map->ndevs);

	for (i = 0; i < nrows; i++) {
		int *row = rows[i];

		for (j = 0; j < ndevs; j++) {
			int dev = row[j];

			ASSERT(0 <= dev && dev < ndevs);
			ASSERT(devcounts[dev] == i);
			devcounts[dev] = i+1;
		}
	}

	/* Ensure broken drives only appear once */
	memset(brokencounts, 0, sizeof (int) * map->ndevs);

	for (i = 0; i < map->nbroken; i++) {
		ASSERT3S(0, <=, map->broken[i]);
		ASSERT3S(map->broken[i], <, map->ndevs);
		ASSERT0(brokencounts[i]); /* not used already */
		brokencounts[i] = 1;
	}
}

static map_t *
dup_map(map_t *oldmap)
{
	map_t *map = malloc(sizeof (map_t));

	map->groupsz = malloc(sizeof (int) * oldmap->ngroups);

	for (int i = 0; i < oldmap->ngroups; i++)
		map->groupsz[i] = oldmap->groupsz[i];

	map->ngroups = oldmap->ngroups;
	map->nspares = oldmap->nspares;
	map->ndevs = oldmap->ndevs;
	map->nrows = oldmap->nrows;
	map->rows = malloc(sizeof (int *) * map->nrows);

	for (int i = 0; i < map->nrows; i++) {
		map->rows[i] = malloc(sizeof (int) * map->ndevs);
		memcpy(map->rows[i], oldmap->rows[i],
		    sizeof (int) * map->ndevs);
	}

	/* Init to no failures (nothing broken) */
	map->broken = malloc(sizeof (int) * map->nspares);
	map->nbroken = 0;

	check_map(map);
	return (map);
}

static map_t *
new_map(int ndevs, int ngroups, int nspares, int nrows)
{
	map_t *map = malloc(sizeof (map_t));
	int groupsz = (ndevs - nspares) / ngroups;
	int extra = (ndevs - nspares) % ngroups;

	ASSERT(nrows <= MAX_ROWS);
	ASSERT(ndevs <= MAX_DEVS);

	map->ngroups = ngroups;
	map->groupsz = malloc(sizeof (int) * ngroups);

	for (int i = 0; i < ngroups; i++) {
		map->groupsz[i] = groupsz;
		if (i < extra)
			map->groupsz[i] += 1;
	}

	map->nspares = nspares;
	map->ndevs = ndevs;
	map->nrows = nrows;
	map->rows = malloc(sizeof (int *) * nrows);

	for (int i = 0; i < nrows; i++) {
		map->rows[i] = malloc(sizeof (int) * ndevs);

		if (i == 0)
			for (int j = 0; j < ndevs; j++)
				map->rows[i][j] = j;
		else
			permute_devs(map->rows[i-1], map->rows[i], ndevs);
	}

	/* Init to no failures (nothing broken) */
	map->broken = malloc(sizeof (int) * nspares);
	map->nbroken = 0;

	check_map(map);
	return (map);
}

static void
free_map(map_t *map)
{
	free(map->broken);
	for (int i = 0; i < map->nrows; i++)
		free(map->rows[i]);
	free(map->rows);
	free(map);
}

static inline int
is_broken(map_t *map, int dev)
{
	for (int i = 0; i < map->nbroken; i++)
		if (dev == map->broken[i])
			return (1);

	return (0);
}

static int
eval_resilver(map_t *map, int print)
{
	/* Evaluate how resilvering I/O will be distributed */
	int  i;
	int  j;
	int  k;
	int  spare;
	int  ndevs = map->ndevs;
	int  nspares = map->nspares;
	int  ngroups = map->ngroups;
	int  groupsz;
	int  nrows = map->nrows;
	int  writes[MAX_DEVS];
	int  reads[MAX_DEVS];
	int  max_reads = 0;
	int  max_writes = 0;
	int  max_ios = 0;

	memset(reads, 0, sizeof (int) * ndevs);
	memset(writes, 0, sizeof (int) * ndevs);

	/* resilver all rows */
	for (i = 0; i < nrows; i++) {
		int *row = map->rows[i];

		/* resilver all groups with broken drives */
		int index = 0;
		for (j = 0; j < ngroups; j++) {
			int  fix = 0;

			/* See if any disk in this group is broken */
			groupsz = map->groupsz[j];
			ASSERT(index < ndevs - groupsz);
			for (k = 0; k < groupsz && !fix; k++)
				fix = is_broken(map, row[index + k]);

			if (!fix) {
				index += groupsz;
				continue;
			}

			/*
			 * This group needs fixing
			 * Read all the non-broken drives and write all the
			 * broken drives to their hot spare for this row
			 */
			spare = ndevs - nspares;
			for (k = 0; k < groupsz; k++) {
				int dev = row[index+k];

				if (!is_broken(map, dev)) {
					reads[dev]++;
				} else {
					ASSERT(spare < ndevs);

					while (is_broken(map, row[spare])) {
						spare++;
						ASSERT(spare < ndevs);
					}
					writes[row[spare++]]++;
				}
			}
			index += groupsz;
		}
	}

	/* find drives with most I/O */
	for (i = 0; i < ndevs; i++) {
		if (reads[i] > max_reads)
			max_reads = reads[i];
		if (writes[i] > max_writes)
			max_writes = writes[i];

		if (reads[i] + writes[i] > max_ios)
			max_ios = reads[i] + writes[i];
	}

	if (print) {
		printf("Reads:  ");
		for (i = 0; i < ndevs; i++)
			printf(" %5.3f", ((double)reads[i]*ngroups)/nrows);
		printf("\n");
		printf("Writes: ");
		for (i = 0; i < ndevs; i++)
			printf(" %5.3f", ((double)writes[i]*ngroups)/nrows);
		printf("\n");
	}

	return (max_ios);
}

static double
eval_decluster(map_t *map, int how, int faults, int print)
{
	int f1;
	int f2;
	int ios;
	int worst1 = -1;
	int worst2 = -1;
	int n = 0;
	long sum = 0;
	long sumsq = 0;
	long max_ios = 0;
	double val = 0;

	ASSERT(eval_resilver(map, 0) == 0); /* not broken already */
	ASSERT(faults == 1 || faults == 2);

	map->nbroken = faults;

	for (f1 = 0; f1 < map->ndevs; f1++) {
		map->broken[0] = f1;

		if (faults < 2) {
			ios = eval_resilver(map, 0); /* eval single failure */
			n++;
			sum += ios;
			sumsq += ios*ios;
			if (max_ios < ios) {
				worst1 = f1;
				max_ios = ios;
			}
		} else { /* eval double failure */
			for (f2 = f1 + 1; f2 < map->ndevs; f2++) {
				map->broken[1] = f2; /* use 2nd hot spare */

				ios = eval_resilver(map, 0);
				n++;
				sum += ios;
				sumsq += ios*ios;
				if (max_ios < ios) {
					worst1 = f1;
					worst2 = f2;
					max_ios = ios;
				}
			}
		}
	}
	map->nbroken = 0;

	if (print) {
		map->nbroken = faults;
		map->broken[0] = worst1;
		map->broken[2] = worst2;

		eval_resilver(map, 1);

		map->nbroken = 0;
	}

	switch (how) {
	case EVAL_WORST:
		/*
		 * imbalance from worst possible drive failure
		 * insensitive to failures handled better
		 */
		val = max_ios;
		break;
	case EVAL_MEAN:
		/*
		 * average over all possible drive failures
		 * sensitive to all possible failures
		 */
		val = ((double)sum)/n;
		break;
	case EVAL_RMS:
		/*
		 * root mean square over all possible drive failures
		 * penalizes higher imbalance more
		 */
		val = sqrt(((double)sumsq)/n);
		break;
	default:
		ASSERT(0);
	}
	return ((val/map->nrows)*map->ngroups);
}

static int
rand_in_range(int min, int count)
{
	return (min + drand48()*count);
}

static void
permute_map(map_t *map, int temp)
{
	static int prev_temp;

	int nrows = (temp < 1) ? 1 : (temp > 100) ?
	    map->nrows : rand_in_range(1, (map->nrows * temp)/100);
	int row   = rand_in_range(0, map->nrows - nrows);
	int ncols = map->ndevs;
	int col   = rand_in_range(0, map->ndevs - ncols);
	int i;

	if (verbose > 0 &&
	    temp != prev_temp &&
	    (temp < 10 || (temp % 10 == 0)))
		printf("Permute t %3d (%d-%d, %d-%d)\n",
		    temp, col, ncols, row, nrows);
	prev_temp = temp;

	for (i = row; i < row + nrows; i++)
		permute_devs(&map->rows[i][col], &map->rows[i][col], ncols);
}

static map_t *
develop_map(map_t *map)
{
	map_t *dmap = new_map(map->ndevs, map->ngroups,
	    map->nspares, map->nrows * map->ndevs);
	int    base;
	int    dev;
	int    i;

	for (base = 0; base < map->nrows; base++)
		for (dev = 0; dev < map->ndevs; dev++)
			for (i = 0; i < map->ndevs; i++)
				dmap->rows[base*map->ndevs + dev][i] =
				    (map->rows[base][i] + dev) % map->ndevs;

	return (dmap);
}

static map_t *
optimize_map(map_t *map, int eval, int faults)
{
	double temp    = 100.0;
	double alpha   = 0.995;
	double epsilon = 0.001;
	double val = eval_decluster(map, eval, faults, 0);
	int ups = 0;
	int downs = 0;
	int sames = 0;
	int iter = 0;

	while (temp > epsilon) {
		map_t  *map2 = dup_map(map);
		double  val2;
		double  delta;

		permute_map(map2, (int)temp);

		val2  = eval_decluster(map2, eval, faults, 0);
		delta = (val2 - val);

		if (delta < 0 || exp(-10000*delta/temp) > drand48()) {
			if (delta > 0)
				ups++;
			else if (delta < 0)
				downs++;
			else
				sames++;

			free_map(map);
			map = map2;
			val = val2;
		} else {
			free_map(map2);
		}

		temp *= alpha;

		if ((++iter % 100) == 0) {
			if (verbose > 0)
				printf("%f (%d ups, %d sames, %d downs)\n",
				    val, ups, sames, downs);
			ups = downs = sames = 0;
		}
	}

	if (verbose > 0)
		printf("%d iters, %d ups %d sames %d downs\n",
		    iter, ups, sames, downs);
	return (map);
}

static void
print_map_stats(map_t *map, int optimize, int print_ios)
{
	double score = eval_decluster(map, EVAL_WORST, 1, 0);

	printf("%6s (%2d - %2d / %2d) x %5d: %2.3f\n",
	    (optimize == UNOPT) ? "Unopt" :
	    (optimize == EVAL_WORST) ? "Worst" :
	    (optimize == EVAL_MEAN) ? "Mean"  : "Rms",
	    map->ndevs, map->nspares, map->ngroups, map->nrows, score);

	if (map->ndevs < 80 && score >= 1.05)
		printf("Warning score %6.3f has over 5 percent imbalance!\n",
		    score);
	else if (score >= 1.1)
		printf("Warning score %6.3f has over 10 percent imbalance!\n",
		    score);

#ifdef FOOO
	printf("Single: worst %6.3f mean %6.3f\n",
	    eval_decluster(map, EVAL_WORST, 1, 0),
	    eval_decluster(map, EVAL_MEAN, 1, 0));

	printf("Double: worst %6.3f mean %6.3f\n",
	    eval_decluster(map, EVAL_WORST, 2, 0),
	    eval_decluster(map, EVAL_MEAN, 2, 0));
#endif

	if (print_ios) {
		eval_decluster(map, EVAL_WORST, 1, 1);
		eval_decluster(map, EVAL_WORST, 2, 1);
	}
}

int
draid_permutation_generate(struct vdev_draid_configuration *cfg)
{
	const int loop = 16; /* HH: make this a parameter */
	const int faults = 1;
	const int eval = EVAL_WORST;

	int nspares = cfg->dcf_spare;
	int ngroups = cfg->dcf_groups;
	int ndevs = cfg->dcf_children;
	int nrows;
	int i, fd, urand_fd;
	long int best_seed;
	map_t *best_map;

	fd = open("/dev/random", O_RDONLY | O_NONBLOCK);
	if (fd == -1) {
		perror("Cannot open /dev/random\n");
		return (-1);
	}
	urand_fd = open("/dev/urandom", O_RDONLY);

	/* HH: fine tune these heuristics */
	if (cfg->dcf_children - nspares > 80)
		nrows = 128; /* > 80 */
	else if (cfg->dcf_children - nspares > 40)
		nrows = 64;  /* 41 - 80 */
	else
		nrows = 32;  /* 1 - 40 */

	for (i = 0, best_map = NULL; i < loop; i++) {
		int rc;
		long int seed;
		map_t *map, *omap;

		rc = read(fd, &seed, sizeof (seed));
		if (rc != sizeof (seed)) {
			printf("Not enough entropy at /dev/random: read %d, "
			    "wanted %lu.\n", rc, sizeof (seed));
			/* urand_fd may not be valid but it does not matter */
			rc = read(urand_fd, &seed, sizeof (seed));
			if (rc != sizeof (seed))
				break;
			printf("Using /dev/urandom instead.\n");
		}

		srand48(seed);

		map = new_map(ndevs, ngroups, nspares, nrows);
		omap = optimize_map(dup_map(map), eval, faults);
		if (eval_decluster(omap, eval, faults, 0) >
		    eval_decluster(map, eval, faults, 0)) {
			/*
			 * optimize_map() may create a worse map, because the
			 * simulated annealing process may accept worse
			 * neighbors to avoid getting stuck in local optima
			 */
			free_map(omap);
		} else {
			free_map(map);
			map = omap;
		}

		if (best_map == NULL ||
		    eval_decluster(map, eval, faults, 0) <
		    eval_decluster(best_map, eval, faults, 0)) {
			if (best_map != NULL)
				free_map(best_map);
			best_map = map;
			best_seed = seed;
		} else {
			free_map(map);
		}
	}

	close(fd);
	close(urand_fd);
	if (i != loop)
		fprintf(stderr, "Early termination at loop %d. Generated "
		    "permutations may not be optimal!\n", i + 1);

	if (best_map != NULL) {
		int j;
		map_t *dmap;
		uint64_t *perms;

		assert(best_map->nrows == nrows);
		assert(best_map->ndevs == cfg->dcf_children);

		perms = malloc(sizeof (*perms) * nrows * best_map->ndevs);
		assert(perms != NULL);

		for (i = 0; i < nrows; i++)
			for (j = 0; j < best_map->ndevs; j++)
				perms[i * best_map->ndevs + j] =
				    best_map->rows[i][j];

		cfg->dcf_bases = nrows;
		cfg->dcf_base_perms = perms;

		if (verbose > 1)
			print_map(best_map);
		dmap = develop_map(best_map);
		free_map(best_map);
		print_map_stats(dmap, eval, 0);
		printf("Seed chosen: %lx\n", best_seed);
		free_map(dmap);
		return (0);
	} else {
		return (-1);
	}
}

int
debug_main(int argc, char **argv)
{
	int    ngroups = 0;
	int    groupsz = 0;
	int    nspares = 0;
	int    nrows = 0;
	int    optimize = UNOPT;
	int    faults = 1;
	int    develop = 0;
	map_t *map;
	int c;

	while ((c = getopt(argc, argv, "g:d:s:n:vUWMR12D")) != -1)
		switch (c) {
		case 'D':
			develop = 1;
			break;
		case 'g':
			sscanf(optarg, "%d", &ngroups);
			break;
		case 'd':
			sscanf(optarg, "%d", &groupsz);
			break;
		case 's':
			sscanf(optarg, "%d", &nspares);
			break;
		case 'n':
			sscanf(optarg, "%d", &nrows);
			break;
		case 'v':
			verbose++;
			break;
		case 'U':
			optimize = UNOPT;
			break;
		case 'W':
			optimize = EVAL_WORST;
			break;
		case 'M':
			optimize = EVAL_MEAN;
			break;
		case 'R':
			optimize = EVAL_RMS;
			break;
		case '1':
			faults = 1;
			break;
		case '2':
			faults = 2;
			break;
		default:
			fprintf(stderr, "arg???\n");
			return (1);
		}

	if (ngroups <= 0 || groupsz <= 0 || nspares <= 0 || nrows <= 0) {
		fprintf(stderr, "missing arg???\n");
		return (1);
	}

	map = new_map(groupsz * ngroups + nspares, ngroups, nspares, nrows);
	if (verbose > 1)
		print_map(map);

	if (verbose > 0)
		print_map_stats(map, UNOPT, 1);

	if (optimize != UNOPT) {
		map = optimize_map(map, optimize, faults);

		if (verbose > 1)
			print_map(map);
		if (verbose > 0)
			print_map_stats(map, optimize, 1);
	}

	if (develop) {
		map_t *dmap = develop_map(map);

		free_map(map);
		map = dmap;
	}

	print_map_stats(map, optimize, verbose > 0);
	return (0);
}
