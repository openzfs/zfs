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
 * Copyright 2009 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include <errno.h>
#include <libgen.h>
#include <libintl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "zpool_util.h"

/*
 * Utility function to guarantee malloc() success.
 */
void *
safe_malloc(size_t size)
{
	void *data;

	if ((data = calloc(1, size)) == NULL) {
		(void) fprintf(stderr, "internal error: out of memory\n");
		exit(1);
	}

	return (data);
}

/*
 * Utility function to guarantee realloc() success.
 */
void *
safe_realloc(void *from, size_t size)
{
	void *data;

	if ((data = realloc(from, size)) == NULL) {
		(void) fprintf(stderr, "internal error: out of memory\n");
		exit(1);
	}

	return (data);
}

/*
 * Display an out of memory error message and abort the current program.
 */
void
zpool_no_memory(void)
{
	assert(errno == ENOMEM);
	(void) fprintf(stderr,
	    gettext("internal error: out of memory\n"));
	exit(1);
}

/*
 * Return the number of logs in supplied nvlist
 */
uint_t
num_logs(nvlist_t *nv)
{
	uint_t nlogs = 0;
	uint_t c, children;
	nvlist_t **child;

	if (nvlist_lookup_nvlist_array(nv, ZPOOL_CONFIG_CHILDREN,
	    &child, &children) != 0)
		return (0);

	for (c = 0; c < children; c++) {
		uint64_t is_log = B_FALSE;

		(void) nvlist_lookup_uint64(child[c], ZPOOL_CONFIG_IS_LOG,
		    &is_log);
		if (is_log)
			nlogs++;
	}
	return (nlogs);
}

/* Find the max element in an array of uint64_t values */
uint64_t
array64_max(uint64_t array[], unsigned int len)
{
	uint64_t max = 0;
	int i;
	for (i = 0; i < len; i++)
		max = MAX(max, array[i]);

	return (max);
}

/*
 * Find highest one bit set.
 * Returns bit number + 1 of highest bit that is set, otherwise returns 0.
 */
int
highbit64(uint64_t i)
{
	if (i == 0)
		return (0);

	return (NBBY * sizeof (uint64_t) - __builtin_clzll(i));
}

/*
 * Find lowest one bit set.
 * Returns bit number + 1 of lowest bit that is set, otherwise returns 0.
 */
int
lowbit64(uint64_t i)
{
	if (i == 0)
		return (0);

	return (__builtin_ffsll(i));
}

/*
 * Given a string of comma-separated flag names, set or clear the corresponding
 * flag variables, as defined in flagspec. If an unknown flag name is offered,
 * returns it. On succes, returns NULL.
 *
 * This is most useful when combined with getopt, to allow related options to
 * be set at once.
 *
 * Example:
 *
 * int main(int arvc, char **argv) {
 *   struct option long_options[] = {
 *     {"frobnicate", required_argument, NULL, 1},
 *     {0, 0, 0, 0},
 *   };
 *   struct zpool_option_flag frobnicate_flags[] = {
 *     {"gizmo",  &frobnicate_gizmo},
 *     {"widget", &frobnicate_widget},
 *     {"thingy", &frobnicate_thingy},
 *   };
 *   int do_gizmo = 0, do_widget = 1, do_thingy = 0;
 *
 *   while ((c = getopt_long(argc, argv, "", long_options, NULL)) != -1) {
 *     switch (c) {
 *     case 1:
 *       char *unknown = zpool_option_flag_apply(optarg, frobnicate_flags);
 *       if (unknown)
 *         printf("unknown flag to --frobnicate: %s\n", unknown);
 *   }
 *
 *   printf("gizmo %d widget %d thingy %d\n", do_gizmo, do_widget, do_thingy);
 *   return (0);
 * }
 *
 * $ myprogram --frobnicate='!widget,thingy'
 * gizmo 0 widget 0 thingy 1
 *
 *
 */
char *
zpool_option_flag_apply(char *argstr, struct zpool_option_flag *flagspec)
{
	char *name, *tmp = NULL;
	while ((name = strtok_r(argstr, ",", &tmp)) != NULL) {
		argstr = NULL;
		int newval = 1;
		if (*name == '!') {
			newval = 0;
			name++;
		}
		int found = 0;
		for (struct zpool_option_flag *f = flagspec; f->name; f++) {
			if (strcmp(name, f->name) == 0) {
				if (f->flag)
					*f->flag = newval;
				found = 1;
				break;
			}
		}
		if (!found)
			return (name);
	}

	return (NULL);
}
